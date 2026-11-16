#include "PropertyPath.h"

namespace BECore {

    namespace {

        using namespace PropertyPathLimits;

        constexpr unsigned char kFirstPrintable = 0x20;
        constexpr unsigned char kDelete = 0x7F;

        bool IsControl(char c) {
            const auto byte = static_cast<unsigned char>(c);
            return byte < kFirstPrintable || byte == kDelete;
        }

        bool IsDigit(char c) {
            return c >= '0' && c <= '9';
        }

        bool IsNameStart(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        bool IsNameChar(char c) {
            return IsNameStart(c) || IsDigit(c);
        }

        class Parser {
        public:
            explicit Parser(StringView text) : _text(text) {}

            std::expected<PropertyPath, PathError> Run() {
                if (_text.empty()) {
                    return Fail(PropertyErrorCode::invalid_path, 0, "path is empty");
                }
                if (_text.size() > kMaxPathBytes) {
                    return Fail(PropertyErrorCode::too_long, kMaxPathBytes, Format("path is longer than {} bytes", kMaxPathBytes));
                }
                for (size_t i = 0; i < _text.size(); ++i) {
                    if (IsControl(_text[i])) {
                        return Fail(PropertyErrorCode::invalid_path, i, Format("control character 0x{:02x}", static_cast<unsigned>(static_cast<unsigned char>(_text[i]))));
                    }
                }

                if (!ParseField()) {
                    return std::unexpected(std::move(_error));
                }
                while (_position < _text.size()) {
                    const char c = _text[_position];
                    if (c == '.') {
                        ++_position;
                        if (!ParseField()) {
                            return std::unexpected(std::move(_error));
                        }
                    } else if (c == '[') {
                        ++_position;
                        if (!ParseBracket()) {
                            return std::unexpected(std::move(_error));
                        }
                    } else {
                        return Fail(PropertyErrorCode::invalid_path, _position, "expected '.', '[' or the end of the path");
                    }
                }
                return std::move(_path);
            }

        private:
            std::unexpected<PathError> Fail(PropertyErrorCode code, size_t position, String reason) {
                _error = PathError{code, position, Format("position {}: {}", position, reason)};
                return std::unexpected(_error);
            }

            bool FailHere(PropertyErrorCode code, size_t position, String reason) {
                Fail(code, position, std::move(reason));
                return false;
            }

            bool Reserve(size_t position) {
                if (_path.segments.size() >= kMaxPathSegments) {
                    return FailHere(PropertyErrorCode::too_long, position, Format("more than {} segments", kMaxPathSegments));
                }
                return true;
            }

            bool ParseField() {
                const size_t start = _position;
                if (_position >= _text.size() || !IsNameStart(_text[_position])) {
                    return FailHere(PropertyErrorCode::invalid_path, _position, "expected a name");
                }
                if (!Reserve(start)) {
                    return false;
                }
                while (_position < _text.size() && IsNameChar(_text[_position])) {
                    ++_position;
                }
                PathSegment segment;
                segment.kind = PathSegmentKind::Field;
                segment.text = String(_text.substr(start, _position - start));
                _path.segments.push_back(std::move(segment));
                return true;
            }

            /// After the '['
            bool ParseBracket() {
                const size_t start = _position - 1;
                if (!Reserve(start)) {
                    return false;
                }
                if (_position < _text.size() && IsDigit(_text[_position])) {
                    return ParseIndex();
                }
                if (_position < _text.size() && _text[_position] == '"') {
                    return ParseKey();
                }
                return FailHere(PropertyErrorCode::invalid_path, _position, "expected an index or a quoted key");
            }

            bool ParseIndex() {
                const size_t digitsStart = _position;
                uint64_t value = 0;
                while (_position < _text.size() && IsDigit(_text[_position])) {
                    value = value * 10 + static_cast<uint64_t>(_text[_position] - '0');
                    if (value > kMaxIndex) {
                        return FailHere(PropertyErrorCode::invalid_path, digitsStart, Format("index is larger than {}", kMaxIndex));
                    }
                    ++_position;
                }
                if (!ExpectClosingBracket()) {
                    return false;
                }
                PathSegment segment;
                segment.kind = PathSegmentKind::Index;
                segment.index = static_cast<uint32_t>(value);
                _path.segments.push_back(std::move(segment));
                return true;
            }

            /// At the opening quote
            bool ParseKey() {
                ++_position;
                String key;
                for (;;) {
                    if (_position >= _text.size()) {
                        return FailHere(PropertyErrorCode::invalid_path, _position, "expected '\"' to close the key");
                    }
                    const char c = _text[_position];
                    if (c == '"') {
                        ++_position;
                        break;
                    }
                    if (c == '\\') {
                        const char escaped = _position + 1 < _text.size() ? _text[_position + 1] : '\0';
                        if (escaped != '"' && escaped != '\\') {
                            return FailHere(PropertyErrorCode::invalid_path, _position, "invalid escape, only \\\" and \\\\ exist");
                        }
                        key.push_back(escaped);
                        _position += 2;
                    } else {
                        key.push_back(c);
                        ++_position;
                    }
                    if (key.size() > kMaxKeyBytes) {
                        return FailHere(PropertyErrorCode::too_long, _position, Format("key is longer than {} bytes", kMaxKeyBytes));
                    }
                }
                if (!ExpectClosingBracket()) {
                    return false;
                }
                PathSegment segment;
                segment.kind = PathSegmentKind::Key;
                segment.text = std::move(key);
                _path.segments.push_back(std::move(segment));
                return true;
            }

            bool ExpectClosingBracket() {
                if (_position >= _text.size() || _text[_position] != ']') {
                    return FailHere(PropertyErrorCode::invalid_path, _position, "expected ']'");
                }
                ++_position;
                return true;
            }

            StringView _text;
            size_t _position = 0;
            PropertyPath _path;
            PathError _error;
        };

        void AppendKey(String& out, StringView key) {
            out += "[\"";
            for (const char c : key) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                }
                out += c;
            }
            out += "\"]";
        }

    }  // namespace

    std::expected<PropertyPath, PathError> ParsePropertyPath(StringView text) {
        Parser parser(text);
        return parser.Run();
    }

    String FormatPropertyPath(Span<const PathSegment> segments, size_t count) {
        String out;
        const size_t shown = count < segments.size() ? count : segments.size();
        for (size_t i = 0; i < shown; ++i) {
            const PathSegment& segment = segments[i];
            switch (segment.kind) {
                case PathSegmentKind::Field:
                    if (i > 0) {
                        out += '.';
                    }
                    out += segment.text;
                    break;
                case PathSegmentKind::Index:
                    AppendFormat(out, "[{}]", segment.index);
                    break;
                case PathSegmentKind::Key:
                    AppendKey(out, segment.text);
                    break;
            }
        }
        return out;
    }

}  // namespace BECore
