#include "JsonWriter.h"

#include <BECore/Control/JsonDoubleText.h>

namespace BECore {

    namespace {

        constexpr size_t kIntegerTextBytes = 24;
        constexpr size_t kFloatTextBytes = 32;
        constexpr char kHexDigits[] = "0123456789abcdef";
        constexpr StringView kReplacement = "\xEF\xBF\xBD";
        constexpr uint8_t kMinPrintable = 0x20;
        constexpr uint8_t kDelete = 0x7F;
        constexpr uint8_t kUtf8Accept = 0;
        constexpr uint8_t kUtf8Reject = 1;

        // clang-format off
        /// The UTF-8 decoder of nlohmann's `serializer::decode`: Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>, MIT (the licence
        /// text is in the header of JsonDoubleText.cpp), http://bjoern.hoehrmann.de/utf-8/decoder/dfa/. 256 byte classes, then the transitions
        /// of every state. The replacement of broken sequences below follows nlohmann's `dump_escaped` step by step, so the number of U+FFFD
        /// for each kind of broken sequence is the one the DOM writer gave
        constexpr Array<uint8_t, 400> kUtf8Dfa = {{
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
            8, 8, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
            10, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 3, 3,
            11, 6, 6, 6, 5, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
            0, 1, 2, 3, 5, 8, 7, 1, 1, 1, 4, 6, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1,
            1, 2, 1, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 3, 1, 3, 1, 1, 1, 1, 1, 1,
            1, 3, 1, 1, 1, 1, 1, 3, 1, 3, 1, 1, 1, 1, 1, 1,
            1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        }};
        // clang-format on

        constexpr size_t kStateRow = 16;
        constexpr size_t kClassTable = 256;

        uint8_t DecodeUtf8(uint8_t& state, uint32_t& codepoint, uint8_t byte) {
            constexpr uint32_t kContinuationMask = 0x3F;
            constexpr uint32_t kByteMask = 0xFF;
            constexpr uint32_t kContinuationBits = 6;
            const uint8_t type = kUtf8Dfa[byte];
            codepoint = state != kUtf8Accept ? (byte & kContinuationMask) | (codepoint << kContinuationBits) : (kByteMask >> type) & byte;
            state = kUtf8Dfa[kClassTable + static_cast<size_t>(state) * kStateRow + type];
            return state;
        }

        /// Bytes that are written as they are and need no decoding: printable ASCII except the quote and the backslash
        bool IsPlainByte(char c) {
            const auto byte = static_cast<uint8_t>(c);
            return byte >= kMinPrintable && byte <= kDelete && c != '"' && c != '\\';
        }

        void AppendShortEscape(String& out, char letter) {
            out.push_back('\\');
            out.push_back(letter);
        }

    }  // namespace

    JsonWriter::JsonWriter(String& out, int indent) : _out(out), _indent(indent < 0 ? kCompact : indent) {}

    void JsonWriter::Fail(ControlError error) {
        if (!_failure) {
            _failure = std::move(error);
        }
        _valid = false;
    }

    bool JsonWriter::Misuse([[maybe_unused]] const char* why) {
        EXPECT(false, why);
        _valid = false;
        return false;
    }

    bool JsonWriter::GetFlag(const Array<uint64_t, kFlagWords>& flags, size_t level) const {
        return ((flags[level / kBitsPerWord] >> (level % kBitsPerWord)) & 1u) != 0;
    }

    void JsonWriter::SetFlag(Array<uint64_t, kFlagWords>& flags, size_t level, bool value) {
        const uint64_t bit = uint64_t{1} << (level % kBitsPerWord);
        uint64_t& word = flags[level / kBitsPerWord];
        word = value ? (word | bit) : (word & ~bit);
    }

    bool JsonWriter::InObject() const {
        return _depth > 0 && GetFlag(_isObject, _depth - 1);
    }

    void JsonWriter::NewLine(size_t depth) {
        if (_indent < 0) {
            return;
        }
        _out.push_back('\n');
        _out.append(depth * static_cast<size_t>(_indent), ' ');
    }

    bool JsonWriter::BeginValue() {
        if (_failure) {
            return false;
        }
        if (_depth == 0) {
            return _rootDone ? Misuse("JsonWriter: the root value is already written") : true;
        }
        if (InObject()) {
            if (!_keyPending) {
                return Misuse("JsonWriter: a value in an object needs a Key first");
            }
            _keyPending = false;
            return true;
        }
        if (GetFlag(_hasItems, _depth - 1)) {
            _out.push_back(',');
        }
        NewLine(_depth);
        SetFlag(_hasItems, _depth - 1, true);
        return true;
    }

    void JsonWriter::EndValue() {
        if (_depth == 0) {
            _rootDone = true;
        }
    }

    void JsonWriter::Begin(char open, bool isObject) {
        if (_failure) {
            return;
        }
        if (_depth >= kMaxDepth) {
            Misuse("JsonWriter: nesting is deeper than kMaxDepth");
            return;
        }
        if (!BeginValue()) {
            return;
        }
        _out.push_back(open);
        SetFlag(_isObject, _depth, isObject);
        SetFlag(_hasItems, _depth, false);
        ++_depth;
    }

    void JsonWriter::End(char close, bool isObject) {
        if (_failure) {
            return;
        }
        if (_depth == 0 || GetFlag(_isObject, _depth - 1) != isObject) {
            Misuse("JsonWriter: End without a matching Begin");
            return;
        }
        if (_keyPending) {
            Misuse("JsonWriter: the Key has no value");
            return;
        }
        const bool hadItems = GetFlag(_hasItems, _depth - 1);
        --_depth;
        if (hadItems) {
            NewLine(_depth);
        }
        _out.push_back(close);
        EndValue();
    }

    void JsonWriter::BeginObject() {
        Begin('{', true);
    }

    void JsonWriter::EndObject() {
        End('}', true);
    }

    void JsonWriter::BeginArray() {
        Begin('[', false);
    }

    void JsonWriter::EndArray() {
        End(']', false);
    }

    void JsonWriter::WriteKeyPrefix() {
        if (GetFlag(_hasItems, _depth - 1)) {
            _out.push_back(',');
        }
        NewLine(_depth);
        SetFlag(_hasItems, _depth - 1, true);
        _keyPending = true;
    }

    void JsonWriter::Key(StringView key) {
        if (_failure) {
            return;
        }
        if (!InObject() || _keyPending) {
            Misuse("JsonWriter: a Key needs an open object that waits for one");
            return;
        }
        WriteKeyPrefix();
        _out.push_back('"');
        AppendEscaped(key);
        _out.push_back('"');
        _out.push_back(':');
        if (_indent >= 0) {
            _out.push_back(' ');
        }
    }

    void JsonWriter::KeyLiteral(StringView ready) {
        constexpr size_t kMinLiteral = 3;  // "":
        if (_failure) {
            return;
        }
        if (!InObject() || _keyPending) {
            Misuse("JsonWriter: a Key needs an open object that waits for one");
            return;
        }
        if (ready.size() < kMinLiteral || ready.front() != '"' || ready.back() != ':') {
            Misuse("JsonWriter: KeyLiteral is a quoted name and its colon");
            return;
        }
        WriteKeyPrefix();
        _out.append(ready.data(), ready.size());
        if (_indent >= 0) {
            _out.push_back(' ');
        }
    }

    void JsonWriter::Value(bool value) {
        if (!BeginValue()) {
            return;
        }
        _out.append(value ? "true" : "false");
        EndValue();
    }

    void JsonWriter::Null() {
        if (!BeginValue()) {
            return;
        }
        _out.append("null");
        EndValue();
    }

    void JsonWriter::Value(StringView value) {
        if (!BeginValue()) {
            return;
        }
        _out.push_back('"');
        AppendEscaped(value);
        _out.push_back('"');
        EndValue();
    }

    void JsonWriter::Value(const PoolString& value) {
        Value(value.ToStringView());
    }

    void JsonWriter::Value(const char* value) {
        Value(value == nullptr ? StringView() : StringView(value));
    }

    void JsonWriter::Raw(StringView ready) {
        if (_failure) {
            return;
        }
        if (ready.empty()) {
            Misuse("JsonWriter: Raw needs the text of a value");
            return;
        }
        if (!BeginValue()) {
            return;
        }
        _out.append(ready.data(), ready.size());
        EndValue();
    }

    void JsonWriter::WriteSigned(int64_t value) {
        if (!BeginValue()) {
            return;
        }
        Array<char, kIntegerTextBytes> text{};
        const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
        _out.append(text.data(), static_cast<size_t>(written.ptr - text.data()));
        EndValue();
    }

    void JsonWriter::WriteUnsigned(uint64_t value) {
        if (!BeginValue()) {
            return;
        }
        Array<char, kIntegerTextBytes> text{};
        const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
        _out.append(text.data(), static_cast<size_t>(written.ptr - text.data()));
        EndValue();
    }

    void JsonWriter::WriteDouble(double value) {
        if (!BeginValue()) {
            return;
        }
        if (!std::isfinite(value)) {
            _out.append("null");
        } else {
            Array<char, kMaxDoubleTextBytes> text{};
            _out.append(text.data(), WriteDoubleText(text.data(), value));
        }
        EndValue();
    }

    void JsonWriter::Value(double value) {
        WriteDouble(value);
    }

    void JsonWriter::Value(float value) {
        if (!std::isfinite(value)) {
            WriteDouble(value);
            return;
        }
        // The shortest text that reads back to the same float, taken as a double, as JsonSerializer does: the double of 0.1f is 0.10000000149011612
        Array<char, kFloatTextBytes> text{};
        const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
        double shortest = static_cast<double>(value);
        if (written.ec == std::errc()) {
            std::from_chars(text.data(), written.ptr, shortest);
        }
        WriteDouble(shortest);
    }

    void JsonWriter::AppendEscaped(StringView text) {
        uint32_t codepoint = 0;
        uint8_t state = kUtf8Accept;
        // The size of the output at the last complete character: the bytes of a sequence that turns out broken are cut back to it
        size_t acceptedSize = _out.size();
        size_t pendingBytes = 0;

        for (size_t i = 0; i < text.size(); ++i) {
            if (state == kUtf8Accept && IsPlainByte(text[i])) {
                size_t end = i + 1;
                while (end < text.size() && IsPlainByte(text[end])) {
                    ++end;
                }
                _out.append(text.data() + i, end - i);
                acceptedSize = _out.size();
                i = end - 1;
                continue;
            }

            const auto byte = static_cast<uint8_t>(text[i]);
            const uint8_t next = DecodeUtf8(state, codepoint, byte);
            if (next == kUtf8Accept) {
                switch (codepoint) {
                    case 0x08:
                        AppendShortEscape(_out, 'b');
                        break;
                    case 0x09:
                        AppendShortEscape(_out, 't');
                        break;
                    case 0x0A:
                        AppendShortEscape(_out, 'n');
                        break;
                    case 0x0C:
                        AppendShortEscape(_out, 'f');
                        break;
                    case 0x0D:
                        AppendShortEscape(_out, 'r');
                        break;
                    case 0x22:
                        AppendShortEscape(_out, '"');
                        break;
                    case 0x5C:
                        AppendShortEscape(_out, '\\');
                        break;
                    default:
                        if (codepoint < kMinPrintable) {
                            _out.append("\\u00");
                            _out.push_back(kHexDigits[(codepoint >> 4) & 0xF]);
                            _out.push_back(kHexDigits[codepoint & 0xF]);
                        } else {
                            _out.push_back(text[i]);
                        }
                        break;
                }
                acceptedSize = _out.size();
                pendingBytes = 0;
            } else if (next == kUtf8Reject) {
                // The byte may be fine for itself and wrong only for the sequence before it: read it again
                if (pendingBytes > 0) {
                    --i;
                }
                _out.resize(acceptedSize);
                _out.append(kReplacement.data(), kReplacement.size());
                acceptedSize = _out.size();
                pendingBytes = 0;
                state = kUtf8Accept;
            } else {
                _out.push_back(text[i]);
                ++pendingBytes;
            }
        }

        if (state != kUtf8Accept) {
            _out.resize(acceptedSize);
            _out.append(kReplacement.data(), kReplacement.size());
        }
    }

}  // namespace BECore
