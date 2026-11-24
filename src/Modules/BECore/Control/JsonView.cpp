#include "JsonView.h"

#include <cstdlib>

namespace BECore {

    namespace {

        constexpr uint32_t kHexBits = 4;
        constexpr uint32_t kHexDigitCount = 4;
        constexpr uint32_t kHighSurrogateFirst = 0xD800;
        constexpr uint32_t kLowSurrogateFirst = 0xDC00;
        constexpr uint32_t kSurrogateOffset = 0x10000;
        constexpr uint32_t kSurrogateBits = 10;
        constexpr uint32_t kMaxOneByte = 0x7F;
        constexpr uint32_t kMaxTwoBytes = 0x7FF;
        constexpr uint32_t kMaxThreeBytes = 0xFFFF;
        constexpr size_t kMaxUtf8Bytes = 4;
        /// A number's text is copied to a NUL-terminated buffer only to tell an overflow from an underflow; a longer text is refused
        constexpr size_t kMaxFallbackNumberBytes = 511;

        constexpr StringView kTrue = "true";

        uint32_t HexDigit(char c) {
            if (c >= '0' && c <= '9') {
                return static_cast<uint32_t>(c - '0');
            }
            if (c >= 'a' && c <= 'f') {
                return static_cast<uint32_t>(c - 'a') + 10;
            }
            return static_cast<uint32_t>(c - 'A') + 10;
        }

        /// The four hex digits at @p at; the tape checked them
        uint32_t ReadHex4(StringView raw, size_t at) {
            uint32_t unit = 0;
            for (uint32_t i = 0; i < kHexDigitCount; ++i) {
                unit = (unit << kHexBits) | HexDigit(raw[at + i]);
            }
            return unit;
        }

        /// Writes @p codePoint as UTF-8 into @p out and returns the byte count
        size_t EncodeUtf8(uint32_t codePoint, char (&out)[kMaxUtf8Bytes]) {
            if (codePoint <= kMaxOneByte) {
                out[0] = static_cast<char>(codePoint);
                return 1;
            }
            if (codePoint <= kMaxTwoBytes) {
                out[0] = static_cast<char>(0xC0 | (codePoint >> 6));
                out[1] = static_cast<char>(0x80 | (codePoint & 0x3F));
                return 2;
            }
            if (codePoint <= kMaxThreeBytes) {
                out[0] = static_cast<char>(0xE0 | (codePoint >> 12));
                out[1] = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                out[2] = static_cast<char>(0x80 | (codePoint & 0x3F));
                return 3;
            }
            out[0] = static_cast<char>(0xF0 | (codePoint >> 18));
            out[1] = static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
            out[2] = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            out[3] = static_cast<char>(0x80 | (codePoint & 0x3F));
            return 4;
        }

        char SimpleEscape(char c) {
            switch (c) {
                case 'b':
                    return '\b';
                case 'f':
                    return '\f';
                case 'n':
                    return '\n';
                case 'r':
                    return '\r';
                case 't':
                    return '\t';
                default:
                    return c;  // `"`, `\` and `/` stand for themselves
            }
        }

        /// Hands the decoded form of @p raw (the content of a string the tape checked) to @p sink piece by piece: a run with no escape as it is,
        /// an escape as its bytes. Stops when the sink returns false and says whether it ran to the end.
        template <typename Sink>
        bool DecodeString(StringView raw, Sink&& sink) {
            size_t pos = 0;
            while (pos < raw.size()) {
                const size_t slash = raw.find('\\', pos);
                const size_t runEnd = slash == StringView::npos ? raw.size() : slash;
                if (runEnd > pos && !sink(raw.substr(pos, runEnd - pos))) {
                    return false;
                }
                if (slash == StringView::npos) {
                    break;
                }
                pos = slash + 1;
                const char kind = raw[pos++];
                char bytes[kMaxUtf8Bytes];
                size_t count = 1;
                if (kind != 'u') {
                    bytes[0] = SimpleEscape(kind);
                } else {
                    uint32_t codePoint = ReadHex4(raw, pos);
                    pos += kHexDigitCount;
                    if (codePoint >= kHighSurrogateFirst && codePoint < kLowSurrogateFirst) {
                        // The tape guarantees `\u` and a low surrogate follow
                        const uint32_t low = ReadHex4(raw, pos + 2);
                        pos += 2 + kHexDigitCount;
                        codePoint = kSurrogateOffset + ((codePoint - kHighSurrogateFirst) << kSurrogateBits) + (low - kLowSurrogateFirst);
                    }
                    count = EncodeUtf8(codePoint, bytes);
                }
                if (!sink(StringView(bytes, count))) {
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    StringView JsonView::Raw() const noexcept {
        if (!IsValid()) {
            return {};
        }
        const JsonToken& token = Token();
        const StringView text = _tape->Text();
        if (token.kind == JsonKind::String) {
            // The quotes sit just outside the content
            return text.substr(token.offset - 1, static_cast<size_t>(token.length) + 2);
        }
        return text.substr(token.offset, token.length);
    }

    bool JsonView::TextEquals(StringView text) const noexcept {
        if (!IsString()) {
            return false;
        }
        const JsonToken& token = Token();
        const StringView raw = _tape->Text().substr(token.offset, token.length);
        if ((token.flags & JsonToken::kHasEscape) == 0) {
            return raw == text;
        }
        size_t matched = 0;
        const bool allMatched = DecodeString(raw, [&](StringView piece) {
            if (text.substr(matched, piece.size()) != piece) {
                return false;
            }
            matched += piece.size();
            return true;
        });
        return allMatched && matched == text.size();
    }

    Optional<JsonView> JsonView::Find(StringView key) const noexcept {
        uint32_t cursor = 0;
        return Find(key, cursor);
    }

    Optional<JsonView> JsonView::Find(StringView key, uint32_t& cursor) const noexcept {
        if (!IsObject()) {
            return std::nullopt;
        }
        const uint32_t first = _index + 1;
        const uint32_t end = Token().next;
        const uint32_t start = (cursor >= first && cursor < end) ? cursor : first;
        const Vector<JsonToken>& tokens = _tape->Tokens();
        // A member is its key token and then its value's subtree: the next key is where the value ends
        for (uint32_t pass = 0; pass < 2; ++pass) {
            const uint32_t from = pass == 0 ? start : first;
            const uint32_t to = pass == 0 ? end : start;
            for (uint32_t keyIndex = from; keyIndex < to; keyIndex = tokens[keyIndex + 1].next) {
                const JsonView candidate(*_tape, keyIndex);
                if (candidate.TextEquals(key)) {
                    cursor = tokens[keyIndex + 1].next;
                    return JsonView(*_tape, keyIndex + 1);
                }
            }
        }
        return std::nullopt;
    }

    Detail::JsonChildRange<JsonView, 0> JsonView::Items() const noexcept {
        if (!IsArray()) {
            return {_tape, 0, 0};
        }
        return {_tape, _index + 1, Token().next};
    }

    Detail::JsonChildRange<JsonMember, 1> JsonView::Members() const noexcept {
        if (!IsObject()) {
            return {_tape, 0, 0};
        }
        return {_tape, _index + 1, Token().next};
    }

    Optional<bool> JsonView::AsBool() const noexcept {
        if (!IsBool()) {
            return std::nullopt;
        }
        return Raw() == kTrue;
    }

    Optional<int64_t> JsonView::AsInt64() const noexcept {
        if (!IsInteger()) {
            return std::nullopt;
        }
        return StringUtils::ParseNumber<int64_t>(Raw());
    }

    Optional<uint64_t> JsonView::AsUint64() const noexcept {
        if (!IsInteger()) {
            return std::nullopt;
        }
        const StringView text = Raw();
        if (text.front() == '-') {
            // `-0` is the only negative text that is 0; an unsigned from_chars refuses a sign
            return text == "-0" ? Optional<uint64_t>(0) : std::nullopt;
        }
        return StringUtils::ParseNumber<uint64_t>(text);
    }

    Optional<double> JsonView::AsDouble() const noexcept {
        if (!IsNumber()) {
            return std::nullopt;
        }
        const StringView text = Raw();
        if (const Optional<double> value = StringUtils::ParseNumber<double>(text)) {
            return value;
        }
        // from_chars refuses a value out of the range of double. `strtod` gives infinity or zero for it, and the caller decides
        // what an infinite number means; a text is copied only on this path, which no real message takes
        if (text.size() > kMaxFallbackNumberBytes) {
            return std::nullopt;
        }
        char buffer[kMaxFallbackNumberBytes + 1];
        std::memcpy(buffer, text.data(), text.size());
        buffer[text.size()] = '\0';
        return std::strtod(buffer, nullptr);
    }

    Optional<StringView> JsonView::Text() const noexcept {
        if (!IsString() || (Token().flags & JsonToken::kHasEscape) != 0) {
            return std::nullopt;
        }
        return _tape->Text().substr(Token().offset, Token().length);
    }

    bool JsonView::ReadText(String& out) const {
        if (!IsString()) {
            return false;
        }
        const JsonToken& token = Token();
        const StringView raw = _tape->Text().substr(token.offset, token.length);
        if ((token.flags & JsonToken::kHasEscape) == 0) {
            out.assign(raw.data(), raw.size());
            return true;
        }
        out.clear();
        DecodeString(raw, [&out](StringView piece) {
            out.append(piece.data(), piece.size());
            return true;
        });
        return true;
    }

}  // namespace BECore
