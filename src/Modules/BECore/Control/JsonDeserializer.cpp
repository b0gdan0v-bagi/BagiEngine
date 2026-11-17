#include "JsonDeserializer.h"

#include <BECore/Control/Json.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr StringView kElementValueKey = "value";

        constexpr char kReplacementChar = '?';
        constexpr uint8_t kFirstPrintable = 0x20;
        constexpr uint8_t kDelete = 0x7F;

        /// Text of the document goes into messages that are logged and interned: a control character would break a line of the log
        String Printable(StringView text) {
            String result;
            result.reserve(text.size());
            for (const char c : text) {
                const uint8_t byte = static_cast<uint8_t>(c);
                result += (byte < kFirstPrintable || byte == kDelete) ? kReplacementChar : c;
            }
            return result;
        }

        bool Contains(const Vector<String>& names, StringView name) {
            for (const String& known : names) {
                if (StringView(known) == name) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    JsonDeserializer::JsonDeserializer(const Json& document) {
        Scope root;
        root.node = &document;
        root.kind = ScopeKind::Object;
        // The root is checked at FinishLoad, whenever the mode was set: it was open for the whole load
        root.tracking = true;
        _scopes.push_back(eastl::move(root));
    }

    JsonDeserializer::~JsonDeserializer() = default;

    // =============================================================================
    // Lookup
    // =============================================================================

    const Json* JsonDeserializer::FindMember(StringView name) const {
        const Scope& scope = _scopes.back();
        if (scope.node->is_object()) {
            return FindJsonMember(*scope.node, name);
        }
        if (scope.kind == ScopeKind::Element && name == kElementValueKey) {
            return scope.node;
        }
        return nullptr;
    }

    void JsonDeserializer::NoteAsked(StringView name) const {
        Scope& scope = _scopes.back();
        if (GetDiagnostics() == DiagnosticsMode::Off || !scope.tracking || scope.kind == ScopeKind::Array) {
            return;
        }
        if (!Contains(scope.asked, name)) {
            scope.asked.push_back(String(name));
        }
    }

    const Json* JsonDeserializer::FindForRead(StringView name) {
        NoteAsked(name);
        if (_scopes.size() == 1 && !_scopes.front().node->is_object()) {
            ReportError(name, "Document root is not an object");
            return nullptr;
        }
        return FindMember(name);
    }

    bool JsonDeserializer::HasKey(StringView name) const {
        NoteAsked(name);
        return FindMember(name) != nullptr;
    }

    bool JsonDeserializer::IsScalarMember(StringView name) const {
        NoteAsked(name);
        const Json* member = FindMember(name);
        return member != nullptr && !member->is_object() && !member->is_array();
    }

    void JsonDeserializer::NoteKnownKey(StringView name) {
        NoteAsked(name);
    }

    void JsonDeserializer::SkipScope() {
        _scopes.back().checked = true;
    }

    void JsonDeserializer::RejectValue(StringView name, StringView kind, const Json& value, bool outOfRange) {
        String shown;
        if (value.is_string()) {
            //-NoStd: nlohmann's string
            const std::string& text = value.get_ref<const std::string&>();
            const StringView content = CutText(StringView(text.c_str(), text.size()), ReaderLimits::kMaxQuotedValueBytes);
            shown = Printable(content);
            if (content.size() < text.size()) {
                shown += "...";
            }
        } else if (value.is_object()) {
            shown = "<object>";
        } else if (value.is_array()) {
            shown = "<array>";
        } else {
            // A number, a bool or null: a short text, and nothing nested to walk
            shown = DumpJson(value);
        }
        ReportError(name, Format("Invalid {} value '{}'{}", kind, shown, outOfRange ? " (out of range)" : ""));
    }

    // =============================================================================
    // Values
    // =============================================================================

    bool JsonDeserializer::ReadBool(StringView name, bool& outValue) {
        const Json* value = FindForRead(name);
        if (value == nullptr) {
            return false;
        }
        if (!value->is_boolean()) {
            RejectValue(name, "boolean", *value, false);
            return false;
        }
        outValue = value->get<bool>();
        return true;
    }

    template <typename T>
    bool JsonDeserializer::ReadInteger(StringView name, T& outValue) {
        const Json* value = FindForRead(name);
        if (value == nullptr) {
            return false;
        }
        // Only a JSON integer: 3.0 and 1.5 are numbers of another kind
        if (!value->is_number_integer()) {
            RejectValue(name, "integer", *value, false);
            return false;
        }
        if (value->is_number_unsigned()) {
            const uint64_t number = value->get<uint64_t>();
            if (number > static_cast<uint64_t>(std::numeric_limits<T>::max())) {
                RejectValue(name, "integer", *value, true);
                return false;
            }
            outValue = static_cast<T>(number);
            return true;
        }
        const int64_t number = value->get<int64_t>();
        if constexpr (std::is_unsigned_v<T>) {
            if (number < 0) {
                RejectValue(name, "integer", *value, false);
                return false;
            }
            if (static_cast<uint64_t>(number) > std::numeric_limits<T>::max()) {
                RejectValue(name, "integer", *value, true);
                return false;
            }
        } else if (number < std::numeric_limits<T>::min() || number > std::numeric_limits<T>::max()) {
            RejectValue(name, "integer", *value, true);
            return false;
        }
        outValue = static_cast<T>(number);
        return true;
    }

    template <typename T>
    bool JsonDeserializer::ReadFloating(StringView name, T& outValue) {
        const Json* value = FindForRead(name);
        if (value == nullptr) {
            return false;
        }
        if (!value->is_number()) {
            RejectValue(name, "number", *value, false);
            return false;
        }
        const double number = value->get<double>();
        // A number that is infinite, or beyond the target's type, has no value to keep; the check comes before the cast, which is undefined for a value out of range
        if (!std::isfinite(number) || std::fabs(number) > static_cast<double>(std::numeric_limits<T>::max())) {
            RejectValue(name, "number", *value, true);
            return false;
        }
        outValue = static_cast<T>(number);
        return true;
    }

    bool JsonDeserializer::ReadText(StringView name, String& outValue) {
        const Json* value = FindForRead(name);
        if (value == nullptr) {
            return false;
        }
        if (!value->is_string()) {
            RejectValue(name, "string", *value, false);
            return false;
        }
        //-NoStd: nlohmann's string
        const std::string& text = value->get_ref<const std::string&>();
        outValue = String(text.c_str(), text.size());
        return true;
    }

    bool JsonDeserializer::ReadAttribute(StringView name, bool& outValue) {
        return ReadBool(name, outValue);
    }

    bool JsonDeserializer::ReadAttribute(StringView name, int8_t& outValue) {
        return ReadInteger(name, outValue);
    }

    bool JsonDeserializer::ReadAttribute(StringView name, uint8_t& outValue) {
        return ReadInteger(name, outValue);
    }

    bool JsonDeserializer::ReadAttribute(StringView name, int16_t& outValue) {
        return ReadInteger(name, outValue);
    }

    bool JsonDeserializer::ReadAttribute(StringView name, uint16_t& outValue) {
        return ReadInteger(name, outValue);
    }

    bool JsonDeserializer::ReadAttribute(StringView name, int32_t& outValue) {
        return ReadInteger(name, outValue);
    }

    bool JsonDeserializer::ReadAttribute(StringView name, uint32_t& outValue) {
        return ReadInteger(name, outValue);
    }

    bool JsonDeserializer::ReadAttribute(StringView name, int64_t& outValue) {
        return ReadInteger(name, outValue);
    }

    bool JsonDeserializer::ReadAttribute(StringView name, uint64_t& outValue) {
        return ReadInteger(name, outValue);
    }

    bool JsonDeserializer::ReadAttribute(StringView name, float& outValue) {
        return ReadFloating(name, outValue);
    }

    bool JsonDeserializer::ReadAttribute(StringView name, double& outValue) {
        return ReadFloating(name, outValue);
    }

    bool JsonDeserializer::ReadAttribute(StringView name, String& outValue) {
        return ReadText(name, outValue);
    }

    bool JsonDeserializer::ReadAttribute(StringView name, PoolString& outValue) {
        String text;
        if (!ReadText(name, text)) {
            return false;
        }
        if (!AcceptPoolStringLength(name, text.size())) {
            return false;
        }
        // The one intern of a read: the value is what the field holds
        outValue = PoolString::Intern(StringView(text));
        return true;
    }

    bool JsonDeserializer::Read(StringView name, bool& outValue) {
        return ReadAttribute(name, outValue);
    }

    bool JsonDeserializer::Read(StringView name, int8_t& outValue) {
        return ReadAttribute(name, outValue);
    }

    bool JsonDeserializer::Read(StringView name, uint8_t& outValue) {
        return ReadAttribute(name, outValue);
    }

    bool JsonDeserializer::Read(StringView name, int16_t& outValue) {
        return ReadAttribute(name, outValue);
    }

    bool JsonDeserializer::Read(StringView name, uint16_t& outValue) {
        return ReadAttribute(name, outValue);
    }

    bool JsonDeserializer::Read(StringView name, int32_t& outValue) {
        return ReadAttribute(name, outValue);
    }

    bool JsonDeserializer::Read(StringView name, uint32_t& outValue) {
        return ReadAttribute(name, outValue);
    }

    bool JsonDeserializer::Read(StringView name, int64_t& outValue) {
        return ReadAttribute(name, outValue);
    }

    bool JsonDeserializer::Read(StringView name, uint64_t& outValue) {
        return ReadAttribute(name, outValue);
    }

    bool JsonDeserializer::Read(StringView name, float& outValue) {
        return ReadAttribute(name, outValue);
    }

    bool JsonDeserializer::Read(StringView name, double& outValue) {
        return ReadAttribute(name, outValue);
    }

    bool JsonDeserializer::Read(StringView name, String& outValue) {
        return ReadAttribute(name, outValue);
    }

    bool JsonDeserializer::Read(StringView name, PoolString& outValue) {
        return ReadAttribute(name, outValue);
    }

    // =============================================================================
    // Scopes
    // =============================================================================

    bool JsonDeserializer::IsNestedTooDeep(StringView name) {
        if (_scopes.size() < ReaderLimits::kMaxXmlNesting) {
            return false;
        }
        ReportError(name, "Nesting is deeper than the limit");
        return true;
    }

    bool JsonDeserializer::BeginObject(StringView name) {
        const Json* member = FindForRead(name);
        if (member == nullptr) {
            return false;
        }
        if (!member->is_object()) {
            RejectValue(name, "object", *member, false);
            return false;
        }
        if (IsNestedTooDeep(name)) {
            return false;
        }
        Scope scope;
        scope.node = member;
        scope.kind = ScopeKind::Object;
        scope.tracking = GetDiagnostics() != DiagnosticsMode::Off;
        _scopes.push_back(eastl::move(scope));
        PushPathSegment(name);
        return true;
    }

    void JsonDeserializer::EndObject() {
        if (_scopes.size() > 1 && _scopes.back().kind == ScopeKind::Object) {
            CloseScope();
            _scopes.pop_back();
            PopPathSegment();
        }
    }

    bool JsonDeserializer::BeginArray(StringView name, StringView /*elementName*/, size_t& count) {
        const Json* member = FindForRead(name);
        if (member == nullptr) {
            return false;
        }
        if (!member->is_array()) {
            RejectValue(name, "array", *member, false);
            return false;
        }
        if (IsNestedTooDeep(name)) {
            return false;
        }
        if (member->size() > ReaderLimits::kMaxArrayElements) {
            ReportError(name, "Array has more elements than the limit");
            return false;
        }
        Scope scope;
        scope.node = member;
        scope.kind = ScopeKind::Array;
        _scopes.push_back(eastl::move(scope));
        PushPathSegment(name);
        count = member->size();
        return true;
    }

    void JsonDeserializer::EndArray() {
        if (_scopes.size() > 1 && _scopes.back().kind == ScopeKind::Array) {
            _scopes.pop_back();
            PopPathSegment();
        }
    }

    bool JsonDeserializer::BeginArrayElement() {
        Scope& array = _scopes.back();
        if (array.kind != ScopeKind::Array || array.nextElement >= array.node->size() || IsNestedTooDeep("")) {
            return false;
        }
        const size_t index = array.nextElement++;
        Scope element;
        element.node = &(*array.node)[index];
        element.kind = ScopeKind::Element;
        element.tracking = GetDiagnostics() != DiagnosticsMode::Off;
        _scopes.push_back(eastl::move(element));
        PushPathArrayIndex(index);
        return true;
    }

    void JsonDeserializer::EndArrayElement() {
        if (_scopes.size() > 1 && _scopes.back().kind == ScopeKind::Element) {
            CloseScope();
            _scopes.pop_back();
            PopPathSegment();
        }
    }

    // =============================================================================
    // Unknown keys
    // =============================================================================

    void JsonDeserializer::CloseScope() {
        CheckScope(_scopes.back());
    }

    bool JsonDeserializer::FindingsFull() const {
        const Vector<DeserializeError>& list = GetDiagnostics() == DiagnosticsMode::Warn ? GetWarnings() : GetErrors();
        return list.size() >= ReaderLimits::kMaxRecordedErrors;
    }

    void JsonDeserializer::CheckScope(Scope& scope) {
        if (scope.checked) {
            return;
        }
        scope.checked = true;
        if (GetDiagnostics() == DiagnosticsMode::Off || !scope.tracking || scope.kind == ScopeKind::Array || !scope.node->is_object()) {
            return;
        }

        for (auto member = scope.node->begin(); member != scope.node->end(); ++member) {
            if (FindingsFull()) {
                return;
            }
            //-NoStd: nlohmann's key
            const std::string& key = member.key();
            const StringView name(key.c_str(), key.size());
            if (Contains(scope.asked, name)) {
                continue;
            }
            const StringView shown = CutText(name, ReaderLimits::kMaxQuotedValueBytes);
            const char* const cut = shown.size() < name.size() ? "..." : "";
            const String hint = MakeDidYouMeanHint(name, {&scope.asked});
            ReportFinding(Printable(shown), Format("Unknown key '{}{}'{}", Printable(shown), cut, hint));
        }
    }

    void JsonDeserializer::FinishLoad() {
        // Open scopes are the caller's to close: only a balanced load has a root to finish
        if (GetDiagnostics() == DiagnosticsMode::Off || _scopes.size() != 1) {
            return;
        }
        CheckScope(_scopes.front());
    }

    // =============================================================================
    // Error tracking
    // =============================================================================

    void JsonDeserializer::AddError(StringView path, StringView message, int32_t line) {
        StoreError(_errors, path, message, line);
    }

    bool JsonDeserializer::HasErrors() const {
        return !_errors.empty();
    }

    const Vector<DeserializeError>& JsonDeserializer::GetErrors() const {
        return _errors;
    }

    void JsonDeserializer::ClearErrors() {
        _errors.clear();
    }

}  // namespace BECore
