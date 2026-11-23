#include "JsonTape.h"

#include <BECore/Reflection/ReaderLimits.h>

namespace BECore {

    namespace {

        constexpr uint8_t kMaxAscii = 0x7F;
        constexpr uint8_t kFirstPrintable = 0x20;
        constexpr uint32_t kHexDigitCount = 4;
        constexpr uint32_t kHexBits = 4;
        constexpr uint32_t kHighSurrogateFirst = 0xD800;
        constexpr uint32_t kLowSurrogateFirst = 0xDC00;
        constexpr uint32_t kLowSurrogateLast = 0xDFFF;
        constexpr StringView kTrue = "true";
        constexpr StringView kFalse = "false";
        constexpr StringView kNull = "null";

        CORE_ENUM(Expect, uint8_t, Value, FirstItem, FirstMember, Key, Colon, After)

        bool IsDigit(char c) {
            return c >= '0' && c <= '9';
        }

        /// The value of a hex digit, or a negative number
        int HexValue(char c) {
            if (IsDigit(c)) {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }
            return -1;
        }

        bool InRange(uint8_t byte, uint8_t low, uint8_t high) {
            return byte >= low && byte <= high;
        }

        /// One pass over a text. The error of a step is a literal and `_pos` is the byte it was found at; an empty message is success.
        class Tokenizer {
        public:
            Tokenizer(StringView text, Vector<JsonToken>& tokens, Vector<uint32_t>& open) : _text(text), _tokens(tokens), _open(open) {}

            [[nodiscard]] std::expected<void, JsonParseError> Run() {
                Expect expect = Expect::Value;
                for (;;) {
                    SkipWhitespace();
                    if (_pos >= _text.size()) {
                        if (expect == Expect::After && _open.empty()) {
                            return {};
                        }
                        return Fail("Unexpected end of text");
                    }
                    const StringView error = Step(expect);
                    if (!error.empty()) {
                        return Fail(error);
                    }
                }
            }

        private:
            std::unexpected<JsonParseError> Fail(StringView message) const {
                return std::unexpected(JsonParseError{_pos, message});
            }

            void SkipWhitespace() {
                while (_pos < _text.size()) {
                    const char c = _text[_pos];
                    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                        return;
                    }
                    ++_pos;
                }
            }

            /// The text is not at its end
            StringView Step(Expect& expect) {
                switch (expect) {
                    case Expect::Value:
                    case Expect::FirstItem:
                        return StartValue(expect);
                    case Expect::FirstMember:
                        if (_text[_pos] == '}') {
                            Close();
                            expect = Expect::After;
                            return {};
                        }
                        [[fallthrough]];
                    case Expect::Key:
                        return ReadKey(expect);
                    case Expect::Colon:
                        if (_text[_pos] != ':') {
                            return "Expected ':' after a member name";
                        }
                        ++_pos;
                        expect = Expect::Value;
                        return {};
                    case Expect::After:
                        return AfterValue(expect);
                }
                return "Unreachable parser state";
            }

            StringView StartValue(Expect& expect) {
                const char c = _text[_pos];
                if (expect == Expect::FirstItem && c == ']') {
                    Close();
                    expect = Expect::After;
                    return {};
                }
                if (!_open.empty() && _tokens[_open.back()].kind == JsonKind::Array) {
                    ++_tokens[_open.back()].count;
                }
                expect = Expect::After;
                switch (c) {
                    case '{':
                        expect = Expect::FirstMember;
                        return Open(JsonKind::Object);
                    case '[':
                        expect = Expect::FirstItem;
                        return Open(JsonKind::Array);
                    case '"':
                        return ReadString();
                    case 't':
                        return ReadLiteral(kTrue, JsonKind::Bool);
                    case 'f':
                        return ReadLiteral(kFalse, JsonKind::Bool);
                    case 'n':
                        return ReadLiteral(kNull, JsonKind::Null);
                    default:
                        if (c == '-' || IsDigit(c)) {
                            return ReadNumber();
                        }
                        return "Unexpected character where a value is expected";
                }
            }

            StringView ReadKey(Expect& expect) {
                if (_text[_pos] != '"') {
                    return "Expected a string as a member name";
                }
                ++_tokens[_open.back()].count;
                expect = Expect::Colon;
                return ReadString();
            }

            StringView AfterValue(Expect& expect) {
                if (_open.empty()) {
                    return "Unexpected characters after the value";
                }
                const char c = _text[_pos];
                const JsonKind kind = _tokens[_open.back()].kind;
                if (c == ',') {
                    ++_pos;
                    expect = kind == JsonKind::Array ? Expect::Value : Expect::Key;
                    return {};
                }
                if (kind == JsonKind::Array) {
                    if (c != ']') {
                        return "Expected ',' or ']' in an array";
                    }
                    Close();
                    return {};
                }
                if (c != '}') {
                    return "Expected ',' or '}' in an object";
                }
                Close();
                return {};
            }

            StringView Open(JsonKind kind) {
                if (_open.size() >= ReaderLimits::kMaxJsonNesting) {
                    return "Nesting is too deep";
                }
                JsonToken token;
                token.kind = kind;
                token.offset = static_cast<uint32_t>(_pos);
                _open.push_back(static_cast<uint32_t>(_tokens.size()));
                _tokens.push_back(token);
                ++_pos;
                return {};
            }

            /// Ends the open container at the bracket under `_pos`; the caller has matched the bracket to the container's kind
            void Close() {
                JsonToken& token = _tokens[_open.back()];
                ++_pos;
                token.length = static_cast<uint32_t>(_pos) - token.offset;
                token.next = static_cast<uint32_t>(_tokens.size());
                _open.pop_back();
            }

            void PushScalar(JsonKind kind, size_t offset, size_t length, uint8_t flags = 0) {
                JsonToken token;
                token.kind = kind;
                token.flags = flags;
                token.offset = static_cast<uint32_t>(offset);
                token.length = static_cast<uint32_t>(length);
                token.next = static_cast<uint32_t>(_tokens.size()) + 1;
                _tokens.push_back(token);
            }

            StringView ReadLiteral(StringView literal, JsonKind kind) {
                if (_text.substr(_pos, literal.size()) != literal) {
                    return "Invalid literal";
                }
                PushScalar(kind, _pos, literal.size());
                _pos += literal.size();
                return {};
            }

            void SkipDigits() {
                while (_pos < _text.size() && IsDigit(_text[_pos])) {
                    ++_pos;
                }
            }

            StringView ReadNumber() {
                const size_t start = _pos;
                JsonKind kind = JsonKind::Integer;
                if (_text[_pos] == '-') {
                    ++_pos;
                }
                if (_pos >= _text.size() || !IsDigit(_text[_pos])) {
                    return "Invalid number: a digit is expected";
                }
                if (_text[_pos] == '0') {
                    ++_pos;
                } else {
                    SkipDigits();
                }
                if (_pos < _text.size() && _text[_pos] == '.') {
                    kind = JsonKind::Number;
                    ++_pos;
                    if (_pos >= _text.size() || !IsDigit(_text[_pos])) {
                        return "Invalid number: a digit is expected after '.'";
                    }
                    SkipDigits();
                }
                if (_pos < _text.size() && (_text[_pos] == 'e' || _text[_pos] == 'E')) {
                    kind = JsonKind::Number;
                    ++_pos;
                    if (_pos < _text.size() && (_text[_pos] == '+' || _text[_pos] == '-')) {
                        ++_pos;
                    }
                    if (_pos >= _text.size() || !IsDigit(_text[_pos])) {
                        return "Invalid number: a digit is expected in the exponent";
                    }
                    SkipDigits();
                }
                PushScalar(kind, start, _pos - start);
                return {};
            }

            /// Reads four hex digits at `_pos` into @p unit and moves past them
            bool ReadHex4(uint32_t& unit) {
                if (_pos + kHexDigitCount > _text.size()) {
                    return false;
                }
                unit = 0;
                for (uint32_t i = 0; i < kHexDigitCount; ++i) {
                    const int digit = HexValue(_text[_pos + i]);
                    if (digit < 0) {
                        _pos += i;
                        return false;
                    }
                    unit = (unit << kHexBits) | static_cast<uint32_t>(digit);
                }
                _pos += kHexDigitCount;
                return true;
            }

            /// `_pos` is just after the backslash
            StringView ReadEscape() {
                if (_pos >= _text.size()) {
                    return "Unexpected end of text in a string";
                }
                switch (_text[_pos]) {
                    case '"':
                    case '\\':
                    case '/':
                    case 'b':
                    case 'f':
                    case 'n':
                    case 'r':
                    case 't':
                        ++_pos;
                        return {};
                    case 'u':
                        ++_pos;
                        return ReadUnicodeEscape();
                    default:
                        return "Invalid escape in a string";
                }
            }

            /// `_pos` is just after `\u`; a high surrogate must be followed by `\u` and a low one
            StringView ReadUnicodeEscape() {
                uint32_t unit = 0;
                if (!ReadHex4(unit)) {
                    return "Invalid \\u escape: four hex digits are expected";
                }
                if (unit >= kLowSurrogateFirst && unit <= kLowSurrogateLast) {
                    return "Invalid \\u escape: a low surrogate without a high one";
                }
                if (unit < kHighSurrogateFirst || unit >= kLowSurrogateFirst) {
                    return {};
                }
                if (_pos + 1 >= _text.size() || _text[_pos] != '\\' || _text[_pos + 1] != 'u') {
                    return "Invalid \\u escape: a high surrogate without a low one";
                }
                _pos += 2;
                uint32_t low = 0;
                if (!ReadHex4(low)) {
                    return "Invalid \\u escape: four hex digits are expected";
                }
                if (low < kLowSurrogateFirst || low > kLowSurrogateLast) {
                    return "Invalid \\u escape: a high surrogate without a low one";
                }
                return {};
            }

            /// A multi-byte UTF-8 sequence at `_pos` (RFC 3629: no overlong form, no surrogate, nothing past U+10FFFF)
            bool SkipUtf8Sequence() {
                const uint8_t lead = static_cast<uint8_t>(_text[_pos]);
                uint8_t secondLow = 0x80;
                uint8_t secondHigh = 0xBF;
                size_t length = 0;
                if (InRange(lead, 0xC2, 0xDF)) {
                    length = 2;
                } else if (InRange(lead, 0xE0, 0xEF)) {
                    length = 3;
                    if (lead == 0xE0) {
                        secondLow = 0xA0;
                    } else if (lead == 0xED) {
                        secondHigh = 0x9F;
                    }
                } else if (InRange(lead, 0xF0, 0xF4)) {
                    length = 4;
                    if (lead == 0xF0) {
                        secondLow = 0x90;
                    } else if (lead == 0xF4) {
                        secondHigh = 0x8F;
                    }
                } else {
                    return false;
                }
                if (_pos + length > _text.size() || !InRange(static_cast<uint8_t>(_text[_pos + 1]), secondLow, secondHigh)) {
                    return false;
                }
                for (size_t i = 2; i < length; ++i) {
                    if (!InRange(static_cast<uint8_t>(_text[_pos + i]), 0x80, 0xBF)) {
                        return false;
                    }
                }
                _pos += length;
                return true;
            }

            /// `_pos` is at the opening quote
            StringView ReadString() {
                ++_pos;
                const size_t start = _pos;
                uint8_t flags = 0;
                for (;;) {
                    if (_pos >= _text.size()) {
                        return "Unexpected end of text in a string";
                    }
                    const uint8_t byte = static_cast<uint8_t>(_text[_pos]);
                    if (byte == '"') {
                        break;
                    }
                    if (byte < kFirstPrintable) {
                        return "Control character in a string";
                    }
                    if (byte == '\\') {
                        flags |= JsonToken::kHasEscape;
                        ++_pos;
                        const StringView error = ReadEscape();
                        if (!error.empty()) {
                            return error;
                        }
                    } else if (byte <= kMaxAscii) {
                        ++_pos;
                    } else if (!SkipUtf8Sequence()) {
                        return "Invalid UTF-8 in a string";
                    }
                }
                PushScalar(JsonKind::String, start, _pos - start, flags);
                ++_pos;
                return {};
            }

            StringView _text;
            Vector<JsonToken>& _tokens;
            Vector<uint32_t>& _open;
            size_t _pos = 0;
        };

    }  // namespace

    std::expected<void, JsonParseError> JsonTape::Parse(StringView text) {
        _tokens.clear();
        _open.clear();
        _text = StringView{};
        if (text.size() > ReaderLimits::kMaxJsonDocumentBytes) {
            return std::unexpected(JsonParseError{0, "The text is too large"});
        }
        Tokenizer tokenizer(text, _tokens, _open);
        auto result = tokenizer.Run();
        if (!result) {
            _tokens.clear();
            return result;
        }
        _text = text;
        return {};
    }

}  // namespace BECore
