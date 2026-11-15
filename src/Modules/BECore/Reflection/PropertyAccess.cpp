#include "PropertyAccess.h"

#include <BECore/Reflection/IPropertyVisitor.h>
#include <cmath>

namespace BECore {

    namespace {

        using namespace PropertyPathLimits;

        /// Fields open at once: the fields on the path, one per open scope, and one for the leaf
        constexpr size_t kMaxOpenFields = 2 * kMaxPathSegments;

        /// Digits of the largest index (`kMaxIndex`)
        constexpr size_t kMaxIndexDigits = 10;

        bool ParseDigits(StringView digits, uint64_t& out) {
            if (digits.empty() || digits.size() > kMaxIndexDigits) {
                return false;
            }
            uint64_t value = 0;
            for (const char c : digits) {
                if (c < '0' || c > '9') {
                    return false;
                }
                value = value * 10 + static_cast<uint64_t>(c - '0');
            }
            out = value;
            return true;
        }

        /// The label of a sequence element is `[i]`; the label of the value of a map with integer keys is the decimal key
        bool IsIndexLabel(StringView label, uint32_t index, bool inElements) {
            uint64_t parsed = 0;
            if (label.size() >= 3 && label.front() == '[' && label.back() == ']') {
                return ParseDigits(label.substr(1, label.size() - 2), parsed) && parsed == index;
            }
            return inElements && ParseDigits(label, parsed) && parsed == index;
        }

        StringView KindName(const PropertyValue& value) {
            if (std::holds_alternative<bool>(value)) {
                return "a bool";
            }
            if (std::holds_alternative<int64_t>(value) || std::holds_alternative<uint64_t>(value)) {
                return "an integer";
            }
            if (std::holds_alternative<double>(value)) {
                return "a number";
            }
            return "a string";
        }

        /**
         * Finds the one leaf the path names and reads or writes it. It opens only the scopes that match the next segment (a scope it
         * does not open is skipped by the dispatch), so the work is the length of the path, not the size of the object.
         *
         * `_frames` has one entry per open scope and the root's: the segment the next label is compared with is the one at
         * `_frameCount - 1`, so nothing is counted twice when a scope closes.
         */
        class PathVisitor final : public IPropertyVisitor {
        public:
            PathVisitor(const PropertyPath& path, const PropertyValue* value, const SetOptions& options) : _path(path), _value(value), _options(options) {}

            std::expected<void, PropertyError> Run(TypedRef root) {
                const size_t count = _path.segments.size();
                if (count == 0) {
                    return Refuse(PropertyErrorCode::invalid_path, "path is empty");
                }
                if (count > kMaxPathSegments) {
                    return Refuse(PropertyErrorCode::too_long, Format("path has more than {} segments", kMaxPathSegments));
                }
                if (!root) {
                    return Refuse(PropertyErrorCode::not_found, "there is no object");
                }
                if (root.type->acceptVisitor == nullptr) {
                    return Refuse(PropertyErrorCode::no_visitor, Format("type '{}' has no property visitor", root.type->Name()));
                }
                if (Writing() && root.isConst) {
                    return Refuse(PropertyErrorCode::read_only, "the object is const");
                }

                root.type->acceptVisitor(root.object, *this);

                if (_error.has_value()) {
                    return std::unexpected(std::move(*_error));
                }
                if (!_reached) {
                    return Refuse(PropertyErrorCode::not_found, NotFoundReason());
                }
                return {};
            }

            PropertyValue TakeResult() {
                return std::move(_result);
            }

            /// The text a write put into a text field, when it did
            [[nodiscard]] bool WroteText() const {
                return _textWritten;
            }

            String TakeOldText() {
                return std::move(_oldText);
            }

            bool Visit(StringView name, bool& value) override {
                if (!Leaf(name)) {
                    return false;
                }
                if (!Writing()) {
                    SetResult<bool>(value);
                    return false;
                }
                if (const bool* given = std::get_if<bool>(_value)) {
                    value = *given;
                    return true;
                }
                return Mismatch("a bool");
            }

            bool Visit(StringView name, int8_t& value) override {
                return VisitInteger(name, value);
            }
            bool Visit(StringView name, uint8_t& value) override {
                return VisitInteger(name, value);
            }
            bool Visit(StringView name, int16_t& value) override {
                return VisitInteger(name, value);
            }
            bool Visit(StringView name, uint16_t& value) override {
                return VisitInteger(name, value);
            }
            bool Visit(StringView name, int32_t& value) override {
                return VisitInteger(name, value);
            }
            bool Visit(StringView name, uint32_t& value) override {
                return VisitInteger(name, value);
            }
            bool Visit(StringView name, int64_t& value) override {
                return VisitInteger(name, value);
            }
            bool Visit(StringView name, uint64_t& value) override {
                return VisitInteger(name, value);
            }
            bool Visit(StringView name, float& value) override {
                return VisitFloating(name, value);
            }
            bool Visit(StringView name, double& value) override {
                return VisitFloating(name, value);
            }

            bool Visit(StringView name, PoolString& value) override {
                if (!Leaf(name)) {
                    return false;
                }
                if (!Writing()) {
                    SetResult<String>(String(value.ToStringView().data(), value.ToStringView().size()));
                    return false;
                }
                const String* text = std::get_if<String>(_value);
                if (text == nullptr) {
                    return Mismatch("a string");
                }
                if (!_options.allowInterning) {
                    return Fail(PropertyErrorCode::interning, "writing a PoolString interns the text and the pool never frees, so it is not allowed here");
                }
                value = PoolString::Intern(StringView(text->data(), text->size()));
                return true;
            }

            bool Visit(StringView name, String& value) override {
                if (!Leaf(name)) {
                    return false;
                }
                if (!Writing()) {
                    SetResult<String>(value);
                    return false;
                }
                const String* text = std::get_if<String>(_value);
                if (text == nullptr) {
                    return Mismatch("a string");
                }
                _oldText = value;
                value = *text;
                _textWritten = true;
                return true;
            }

            bool VisitEnum(StringView name, const StringView* enumNames, size_t enumCount, size_t& selectedIndex) override {
                if (!Leaf(name)) {
                    return false;
                }
                if (!Writing()) {
                    SetResult<String>(String(enumNames[selectedIndex].data(), enumNames[selectedIndex].size()));
                    return false;
                }
                size_t chosen = enumCount;
                if (const String* text = std::get_if<String>(_value)) {
                    for (size_t i = 0; i < enumCount; ++i) {
                        if (enumNames[i] == StringView(text->data(), text->size())) {
                            chosen = i;
                            break;
                        }
                    }
                    if (chosen == enumCount) {
                        return Fail(PropertyErrorCode::out_of_range, Format("the text is not one of the {} names of the enum", enumCount));
                    }
                } else if (const int64_t* signedIndex = std::get_if<int64_t>(_value)) {
                    if (*signedIndex < 0 || static_cast<uint64_t>(*signedIndex) >= enumCount) {
                        return Fail(PropertyErrorCode::out_of_range, Format("index {} is not below the {} values of the enum", *signedIndex, enumCount));
                    }
                    chosen = static_cast<size_t>(*signedIndex);
                } else if (const uint64_t* unsignedIndex = std::get_if<uint64_t>(_value)) {
                    if (*unsignedIndex >= enumCount) {
                        return Fail(PropertyErrorCode::out_of_range, Format("index {} is not below the {} values of the enum", *unsignedIndex, enumCount));
                    }
                    chosen = static_cast<size_t>(*unsignedIndex);
                } else {
                    return Mismatch("an enum name or an index");
                }
                selectedIndex = chosen;
                return true;
            }

            bool BeginCompound(StringView name) override {
                return Open(name, false);
            }

            void EndCompound() override {
                Close();
            }

            bool BeginList(StringView name, const ListInfo& /*info*/) override {
                return Open(name, true);
            }

            void EndList(ListRequest& /*request*/) override {
                Close();
            }

            void BeginField(const FieldAttributes& attributes) override {
                if (_openFields < _fields.size()) {
                    _fields[_openFields] = attributes;
                }
                ++_openFields;
            }

            void EndField() override {
                --_openFields;
            }

            bool VisitPresence(StringView name, bool& present) override {
                if (Matches(name) && !present) {
                    NoteMatched();
                    Fail(PropertyErrorCode::not_found, "optional is empty");
                }
                return false;
            }

            bool ShowsHidden() const override {
                return true;
            }

        private:
            struct Frame {
                bool elements = false;
            };

            [[nodiscard]] bool Writing() const {
                return _value != nullptr;
            }

            [[nodiscard]] size_t Position() const {
                return _frameCount - 1;
            }

            std::unexpected<PropertyError> Refuse(PropertyErrorCode code, String reason) const {
                return std::unexpected(PropertyError{code, Format("path '{}': {}", FormatPropertyPath(_path.segments, _path.segments.size()), reason)});
            }

            bool Fail(PropertyErrorCode code, String reason) {
                if (!_error.has_value()) {
                    _error = Refuse(code, std::move(reason)).error();
                }
                _finished = true;
                return false;
            }

            bool Mismatch(StringView expected) {
                return Fail(PropertyErrorCode::type_mismatch, Format("the field takes {}, got {}", expected, KindName(*_value)));
            }

            template <typename T>
            void SetResult(T value) {
                _result.template emplace<T>(std::move(value));
            }

            [[nodiscard]] String NotFoundReason() const {
                const size_t unmatched = _deepest < _path.segments.size() ? _deepest : _path.segments.size() - 1;
                const String segment = FormatPropertyPath(Span<const PathSegment>(_path.segments.data() + unmatched, 1), 1);
                if (unmatched == 0) {
                    return Format("the object has no member '{}'", segment);
                }
                return Format("'{}' has no member '{}'", FormatPropertyPath(_path.segments, unmatched), segment);
            }

            /// Whether @p label is the next segment, in the scope that is open
            [[nodiscard]] bool Matches(StringView label) const {
                if (_finished) {
                    return false;
                }
                const PathSegment& segment = _path.segments[Position()];
                const bool inElements = _frames[Position()].elements;
                const StringView text(segment.text.data(), segment.text.size());
                switch (segment.kind) {
                    case PathSegmentKind::Field:
                        return !inElements && label == text;
                    case PathSegmentKind::Index:
                        return IsIndexLabel(label, segment.index, inElements);
                    case PathSegmentKind::Key:
                        return inElements && label == text;
                }
                return false;
            }

            void NoteMatched() {
                if (Position() + 1 > _deepest) {
                    _deepest = Position() + 1;
                }
            }

            /// A scope (a struct, a pair, a pointee, a variant, a list or a map) the next segment names
            bool Open(StringView label, bool elements) {
                if (!Matches(label)) {
                    return false;
                }
                NoteMatched();
                if (Position() + 1 >= _path.segments.size()) {
                    return Fail(PropertyErrorCode::not_a_value, Format("'{}' is a struct, a list or a map, not a value: name one of its members", label));
                }
                if (_frameCount >= _frames.size()) {
                    return Fail(PropertyErrorCode::too_long, "the path is deeper than a visit can follow");
                }
                _frames[_frameCount].elements = elements;
                ++_frameCount;
                return true;
            }

            void Close() {
                if (_frameCount > 1) {
                    --_frameCount;
                }
            }

            /// The last segment names @p label: the value is to be read or written
            bool Leaf(StringView label) {
                if (!Matches(label)) {
                    return false;
                }
                NoteMatched();
                if (Position() + 1 < _path.segments.size()) {
                    return Fail(PropertyErrorCode::not_found, Format("'{}' is a value and has no members", label));
                }
                _finished = true;
                _reached = true;
                return !Writing() || CheckWritable();
            }

            bool CheckWritable() {
                const size_t open = _openFields < _fields.size() ? _openFields : _fields.size();
                for (size_t i = 0; i < open; ++i) {
                    if (_fields[i].hidden) {
                        return Fail(PropertyErrorCode::hidden, "a hidden field cannot be written");
                    }
                }
                for (size_t i = 0; i < open; ++i) {
                    if (_fields[i].readOnly) {
                        return Fail(PropertyErrorCode::read_only, "the field is read-only");
                    }
                }
                return true;
            }

            /// The `range` of the innermost open field, which is the leaf's own or its container's
            bool InRange(double value) {
                if (_openFields == 0) {
                    return true;
                }
                const FieldAttributes& innermost = _fields[(_openFields < _fields.size() ? _openFields : _fields.size()) - 1];
                if (innermost.hasRange && !(value >= innermost.rangeMin && value <= innermost.rangeMax)) {
                    return Fail(PropertyErrorCode::out_of_range, Format("{} is outside the range {}..{}", value, innermost.rangeMin, innermost.rangeMax));
                }
                return true;
            }

            template <typename T>
            bool VisitInteger(StringView name, T& field) {
                if (!Leaf(name)) {
                    return false;
                }
                if (!Writing()) {
                    if constexpr (std::is_signed_v<T>) {
                        SetResult<int64_t>(static_cast<int64_t>(field));
                    } else {
                        SetResult<uint64_t>(static_cast<uint64_t>(field));
                    }
                    return false;
                }
                T candidate{};
                if (const int64_t* signedValue = std::get_if<int64_t>(_value)) {
                    if (!std::in_range<T>(*signedValue)) {
                        return Fail(PropertyErrorCode::out_of_range, Format("{} does not fit the field", *signedValue));
                    }
                    candidate = static_cast<T>(*signedValue);
                } else if (const uint64_t* unsignedValue = std::get_if<uint64_t>(_value)) {
                    if (!std::in_range<T>(*unsignedValue)) {
                        return Fail(PropertyErrorCode::out_of_range, Format("{} does not fit the field", *unsignedValue));
                    }
                    candidate = static_cast<T>(*unsignedValue);
                } else {
                    return Mismatch("an integer");
                }
                if (!InRange(static_cast<double>(candidate))) {
                    return false;
                }
                field = candidate;
                return true;
            }

            template <typename T>
            bool VisitFloating(StringView name, T& field) {
                if (!Leaf(name)) {
                    return false;
                }
                if (!Writing()) {
                    SetResult<double>(static_cast<double>(field));
                    return false;
                }
                double wide = 0.0;
                if (const double* number = std::get_if<double>(_value)) {
                    wide = *number;
                } else if (const int64_t* signedValue = std::get_if<int64_t>(_value)) {
                    wide = static_cast<double>(*signedValue);
                } else if (const uint64_t* unsignedValue = std::get_if<uint64_t>(_value)) {
                    wide = static_cast<double>(*unsignedValue);
                } else {
                    return Mismatch("a number");
                }
                if (!std::isfinite(wide)) {
                    return Fail(PropertyErrorCode::out_of_range, "the number is not finite");
                }
                if (std::fabs(wide) > static_cast<double>(std::numeric_limits<T>::max())) {
                    return Fail(PropertyErrorCode::out_of_range, Format("{} is beyond what the field holds", wide));
                }
                const T candidate = static_cast<T>(wide);
                if (!InRange(static_cast<double>(candidate))) {
                    return false;
                }
                field = candidate;
                return true;
            }

            const PropertyPath& _path;
            const PropertyValue* _value;
            SetOptions _options;

            Array<Frame, kMaxPathSegments + 1> _frames{};
            size_t _frameCount = 1;
            Array<FieldAttributes, kMaxOpenFields> _fields{};
            size_t _openFields = 0;
            size_t _deepest = 0;

            bool _finished = false;
            bool _reached = false;
            bool _textWritten = false;
            String _oldText;
            PropertyValue _result;
            Optional<PropertyError> _error;
        };

        std::unexpected<PropertyError> FromPathError(StringView text, const PathError& error) {
            return std::unexpected(PropertyError{error.code, Format("path '{}': {}", text, error.message)});
        }

    }  // namespace

    std::expected<PropertyValue, PropertyError> GetProperty(TypedRef root, const PropertyPath& path) {
        PathVisitor visitor(path, nullptr, SetOptions{});
        if (auto result = visitor.Run(root); !result) {
            return std::unexpected(std::move(result.error()));
        }
        return visitor.TakeResult();
    }

    std::expected<PropertyValue, PropertyError> GetProperty(TypedRef root, StringView path) {
        const auto parsed = ParsePropertyPath(path);
        if (!parsed) {
            return FromPathError(path, parsed.error());
        }
        return GetProperty(root, *parsed);
    }

    std::expected<void, PropertyError> SetProperty(TypedRef root, const PropertyPath& path, const PropertyValue& value, const SetOptions& options) {
        PathVisitor visitor(path, &value, options);
        auto result = visitor.Run(root);
        if (!result || !visitor.WroteText()) {
            return result;
        }

        // A FixedString keeps what fits and the dispatch assigns it without a word: read it back, and put the old text back when it was cut
        const String& written = std::get<String>(value);
        const auto readBack = GetProperty(root, path);
        if (readBack.has_value()) {
            if (const String* stored = std::get_if<String>(&*readBack); stored != nullptr && *stored != written) {
                const size_t kept = stored->size();
                const PropertyValue previous(std::in_place_type<String>, visitor.TakeOldText());
                PathVisitor restore(path, &previous, options);
                static_cast<void>(restore.Run(root));
                return std::unexpected(PropertyError{PropertyErrorCode::out_of_range, Format("path '{}': the text of {} bytes does not fit the field, which holds {} bytes at most",
                                                                                             FormatPropertyPath(path.segments, path.segments.size()), written.size(), kept)});
            }
        }
        return {};
    }

    std::expected<void, PropertyError> SetProperty(TypedRef root, StringView path, const PropertyValue& value, const SetOptions& options) {
        const auto parsed = ParsePropertyPath(path);
        if (!parsed) {
            return FromPathError(path, parsed.error());
        }
        return SetProperty(root, *parsed, value, options);
    }

}  // namespace BECore
