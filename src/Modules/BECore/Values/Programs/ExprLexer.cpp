#include "ExprLexer.h"

#include <BECore/Values/Programs/ExprAst.h>

namespace BECore {

    namespace {

        constexpr unsigned char kFirstPrintable = 0x20;
        constexpr unsigned char kDelete = 0x7F;
        constexpr unsigned char kAsciiLimit = 0x80;

        constexpr unsigned char kContinuationMask = 0xC0;
        constexpr unsigned char kContinuationTag = 0x80;
        constexpr unsigned char kTwoByteMask = 0xE0;
        constexpr unsigned char kTwoByteTag = 0xC0;
        constexpr unsigned char kThreeByteMask = 0xF0;
        constexpr unsigned char kThreeByteTag = 0xE0;
        constexpr unsigned char kFourByteMask = 0xF8;
        constexpr unsigned char kFourByteTag = 0xF0;
        constexpr uint32_t kContinuationBits = 6;
        constexpr uint32_t kContinuationValueMask = 0x3F;
        constexpr uint32_t kMaxCodePoint = 0x10FFFF;
        constexpr uint32_t kSurrogateFirst = 0xD800;
        constexpr uint32_t kSurrogateLast = 0xDFFF;
        constexpr uint32_t kTwoByteMinimum = 0x80;
        constexpr uint32_t kThreeByteMinimum = 0x800;
        constexpr uint32_t kFourByteMinimum = 0x10000;

        /// The first guess of the token count: a token of a real formula is about this many bytes. A text of one-byte tokens grows the list once or twice
        constexpr size_t kBytesPerToken = 3;
        constexpr size_t kTokenSlack = 2;

        bool IsDigit(char c) {
            return c >= '0' && c <= '9';
        }

        bool IsNameStart(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        bool IsNameChar(char c) {
            return IsNameStart(c) || IsDigit(c);
        }

        bool IsSpace(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }

        /// The length of the valid UTF-8 sequence that starts at @p at (overlong forms, surrogates and values beyond U+10FFFF are invalid); 0 when it is not one
        size_t Utf8SequenceLength(StringView text, size_t at) {
            const auto byte = [&](size_t i) { return static_cast<unsigned char>(text[i]); };
            const unsigned char lead = byte(at);
            size_t length = 0;
            uint32_t value = 0;
            uint32_t minimum = 0;
            if ((lead & kTwoByteMask) == kTwoByteTag) {
                length = 2;
                value = lead & ~kTwoByteMask;
                minimum = kTwoByteMinimum;
            } else if ((lead & kThreeByteMask) == kThreeByteTag) {
                length = 3;
                value = lead & ~kThreeByteMask;
                minimum = kThreeByteMinimum;
            } else if ((lead & kFourByteMask) == kFourByteTag) {
                length = 4;
                value = lead & ~kFourByteMask;
                minimum = kFourByteMinimum;
            } else {
                return 0;
            }
            if (at + length > text.size()) {
                return 0;
            }
            for (size_t i = 1; i < length; ++i) {
                if ((byte(at + i) & kContinuationMask) != kContinuationTag) {
                    return 0;
                }
                value = (value << kContinuationBits) | (byte(at + i) & kContinuationValueMask);
            }
            if (value < minimum || value > kMaxCodePoint || (value >= kSurrogateFirst && value <= kSurrogateLast)) {
                return 0;
            }
            return length;
        }

        /// The byte at @p at as a message says it; never copies an invalid byte into the text
        String DescribeByteAt(StringView text, size_t at) {
            const auto byte = static_cast<unsigned char>(text[at]);
            if (byte >= kAsciiLimit) {
                const size_t length = Utf8SequenceLength(text, at);
                if (length == 0) {
                    return Format("the invalid UTF-8 byte 0x{:02x}", static_cast<unsigned>(byte));
                }
                return Format("the character '{}'", String(text.substr(at, length)));
            }
            if (byte < kFirstPrintable || byte == kDelete) {
                return Format("the control character 0x{:02x}", static_cast<unsigned>(byte));
            }
            return Format("'{}'", text[at]);
        }

        class Lexer {
        public:
            Lexer(StringView text, const LexOptions& options) : _text(text), _options(options) {}

            std::expected<ArenaVector<ExprToken>, ProgramError> Run() {
                if (_text.size() > _options.maxBytes) {
                    return Fail(_options.maxBytes, Format("the {} is longer than {} bytes", _options.statements ? "script" : "formula", _options.maxBytes));
                }
                _tokens.reserve(_text.size() / kBytesPerToken + kTokenSlack);
                while (true) {
                    while (_position < _text.size() && IsSpace(_text[_position])) {
                        ++_position;
                    }
                    if (_position >= _text.size()) {
                        Push(ExprTokenKind::End, _text.size(), 0);
                        return std::move(_tokens);
                    }
                    if (!Next()) {
                        return std::unexpected(std::move(_error));
                    }
                }
            }

        private:
            std::unexpected<ProgramError> Fail(size_t position, String message) {
                _error = ProgramError{std::move(message), static_cast<uint32_t>(position + 1)};
                return std::unexpected(_error);
            }

            bool FailHere(size_t position, String message) {
                Fail(position, std::move(message));
                return false;
            }

            void Push(ExprTokenKind kind, size_t start, size_t length, bool isFloat = false) {
                _tokens.push_back(ExprToken{kind, static_cast<uint32_t>(start + 1), _text.substr(start, length), isFloat});
            }

            [[nodiscard]] bool EndsOperand() const {
                if (_tokens.empty()) {
                    return false;
                }
                const ExprTokenKind kind = _tokens.back().kind;
                return kind == ExprTokenKind::Number || kind == ExprTokenKind::String || kind == ExprTokenKind::Name || kind == ExprTokenKind::RightParen || kind == ExprTokenKind::RightBracket;
            }

            [[nodiscard]] char At(size_t offset) const {
                return _position + offset < _text.size() ? _text[_position + offset] : '\0';
            }

            bool Operator(ExprTokenKind one, ExprTokenKind withEqual) {
                if (At(1) == '=') {
                    Push(withEqual, _position, 2);
                    _position += 2;
                } else {
                    Push(one, _position, 1);
                    ++_position;
                }
                return true;
            }

            bool Single(ExprTokenKind kind) {
                Push(kind, _position, 1);
                ++_position;
                return true;
            }

            /// `+` and `-` in a script: the sign or the operator, `+=` / `-=`, or the doubled `++` / `--`
            bool SignOrAssignment(char c, ExprTokenKind plain, ExprTokenKind withEqual, ExprTokenKind doubled) {
                if (!_options.statements) {
                    return Single(plain);
                }
                if (At(1) == '=') {
                    Push(withEqual, _position, 2);
                    _position += 2;
                    return true;
                }
                if (At(1) == c) {
                    Push(doubled, _position, 2);
                    _position += 2;
                    return true;
                }
                return Single(plain);
            }

            /// A byte a script reads as one token and a formula refuses like any byte outside its language
            bool ScriptOnly(ExprTokenKind kind) {
                if (_options.statements) {
                    return Single(kind);
                }
                return FailHere(_position, Format("unexpected {}", DescribeByteAt(_text, _position)));
            }

            bool Doubled(char c, ExprTokenKind kind) {
                if (At(1) != c) {
                    return FailHere(_position, Format("unexpected '{}': the operator is '{}{}'", c, c, c));
                }
                Push(kind, _position, 2);
                _position += 2;
                return true;
            }

            bool Next() {
                const char c = _text[_position];
                if (IsDigit(c) || (c == '.' && IsDigit(At(1)) && !EndsOperand())) {
                    return Number();
                }
                if (IsNameStart(c)) {
                    const size_t start = _position;
                    while (_position < _text.size() && IsNameChar(_text[_position])) {
                        ++_position;
                    }
                    Push(ExprTokenKind::Name, start, _position - start);
                    return true;
                }
                switch (c) {
                    case '\'':
                        return QuotedString();
                    case '(':
                        return Single(ExprTokenKind::LeftParen);
                    case ')':
                        return Single(ExprTokenKind::RightParen);
                    case ',':
                        return Single(ExprTokenKind::Comma);
                    case '.':
                        return Single(ExprTokenKind::Dot);
                    case '+':
                        return SignOrAssignment('+', ExprTokenKind::Plus, ExprTokenKind::PlusEqual, ExprTokenKind::PlusPlus);
                    case '-':
                        return SignOrAssignment('-', ExprTokenKind::Minus, ExprTokenKind::MinusEqual, ExprTokenKind::MinusMinus);
                    case '{':
                        return ScriptOnly(ExprTokenKind::LeftBrace);
                    case '}':
                        return ScriptOnly(ExprTokenKind::RightBrace);
                    case ';':
                        return ScriptOnly(ExprTokenKind::Semicolon);
                    case '*':
                        return Single(ExprTokenKind::Star);
                    case '/':
                        return Single(ExprTokenKind::Slash);
                    case '%':
                        return Single(ExprTokenKind::Percent);
                    case '!':
                        return Operator(ExprTokenKind::Bang, ExprTokenKind::BangEqual);
                    case '<':
                        return Operator(ExprTokenKind::Less, ExprTokenKind::LessEqual);
                    case '>':
                        return Operator(ExprTokenKind::Greater, ExprTokenKind::GreaterEqual);
                    case '=':
                        if (At(1) != '=') {
                            if (_options.statements) {
                                return Single(ExprTokenKind::Equal);
                            }
                            return FailHere(_position, "unexpected '=': the comparison is '==' (a formula assigns nothing)");
                        }
                        Push(ExprTokenKind::EqualEqual, _position, 2);
                        _position += 2;
                        return true;
                    case '&':
                        return Doubled('&', ExprTokenKind::AndAnd);
                    case '|':
                        return Doubled('|', ExprTokenKind::OrOr);
                    case '[':
                        return Single(ExprTokenKind::LeftBracket);
                    case ']':
                        return Single(ExprTokenKind::RightBracket);
                    default:
                        return FailHere(_position, Format("unexpected {}", DescribeByteAt(_text, _position)));
                }
            }

            /// `'text'`: printable ASCII up to the next quote, no escapes
            bool QuotedString() {
                const size_t start = _position;
                ++_position;
                while (_position < _text.size() && _text[_position] != '\'') {
                    const auto byte = static_cast<unsigned char>(_text[_position]);
                    if (byte < kFirstPrintable || byte >= kDelete) {
                        return FailHere(_position, Format("unexpected {} in a string: a string holds printable ASCII only", DescribeByteAt(_text, _position)));
                    }
                    ++_position;
                }
                if (_position >= _text.size()) {
                    return FailHere(start, "unterminated string, expected ' to close it");
                }
                _tokens.push_back(ExprToken{ExprTokenKind::String, static_cast<uint32_t>(start + 1), _text.substr(start + 1, _position - start - 1), false});
                ++_position;
                return true;
            }

            /// `12`, `1.5`, `.5`, `1e3`, `1.5e-3`: no suffix, no sign (a sign is an operator)
            bool Number() {
                const size_t start = _position;
                bool isFloat = false;
                while (IsDigit(At(0))) {
                    ++_position;
                }
                if (At(0) == '.') {
                    isFloat = true;
                    ++_position;
                    if (!IsDigit(At(0))) {
                        return FailHere(_position, Format("expected a digit after the decimal point, found {}", FoundAt(_position)));
                    }
                    while (IsDigit(At(0))) {
                        ++_position;
                    }
                }
                if (At(0) == 'e' || At(0) == 'E') {
                    isFloat = true;
                    ++_position;
                    if (At(0) == '+' || At(0) == '-') {
                        ++_position;
                    }
                    if (!IsDigit(At(0))) {
                        return FailHere(_position, Format("expected a digit in the exponent, found {}", FoundAt(_position)));
                    }
                    while (IsDigit(At(0))) {
                        ++_position;
                    }
                }
                if (IsNameChar(At(0))) {
                    return FailHere(_position, Format("unexpected '{}' after the number (a number has no suffix)", At(0)));
                }
                Push(ExprTokenKind::Number, start, _position - start, isFloat);
                return true;
            }

            [[nodiscard]] String FoundAt(size_t position) const {
                return position >= _text.size() ? String("the end of the text") : DescribeByteAt(_text, position);
            }

            StringView _text;
            LexOptions _options;
            size_t _position = 0;
            ArenaVector<ExprToken> _tokens;
            ProgramError _error;
        };

    }  // namespace

    std::expected<ArenaVector<ExprToken>, ProgramError> LexExpression(StringView text, const LexOptions& options) {
        Lexer lexer(text, options);
        return lexer.Run();
    }

    String DescribeToken(const ExprToken& token) {
        if (token.kind == ExprTokenKind::End) {
            return "the end of the text";
        }
        if (token.kind == ExprTokenKind::String) {
            return Format("the string '{}'", token.text);
        }
        return Format("'{}'", token.text);
    }

}  // namespace BECore
