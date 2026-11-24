#include "JsonDeserializer.h"

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

        /// Each scope keeps a few names, and the arena is rewound with every scope: a small chunk is enough
        constexpr size_t kNamesArenaChunkBytes = 4 * 1024;

        /// The names a scope usually collects, reserved at once because an arena vector leaves its old blocks behind when it grows
        constexpr size_t kExpectedAskedNames = 16;

        bool Contains(const Optional<ArenaVector<StringView>>& names, StringView name) {
            if (!names) {
                return false;
            }
            for (const StringView known : *names) {
                if (known == name) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    JsonDeserializer::JsonDeserializer() {
        Reset(_ownTape, JsonView());
    }

    JsonDeserializer::~JsonDeserializer() = default;

    void JsonDeserializer::Reset(const JsonTape& tape, JsonView rootView) {
        _tape = &tape;
        // The scopes hold lists made in the arena: they go before the arena's memory does
        _scopes.clear();
        if (_namesArena) {
            _namesArena->Reset();
        }
        ClearPathSegments();
        _errors.clear();
        ClearWarnings();
        Scope root;
        root.node = rootView;
        root.kind = ScopeKind::Object;
        // The root is checked at FinishLoad, whenever the mode was set: it was open for the whole load
        root.tracking = true;
        _scopes.push_back(eastl::move(root));
    }

    bool JsonDeserializer::Load(StringView text) {
        const std::expected<void, JsonParseError> parsed = _ownTape.Parse(text);
        Reset(_ownTape, JsonView(_ownTape, 0));
        if (!parsed) {
            ReportError({}, Format("Invalid JSON: {} at offset {}", parsed.error().message, parsed.error().offset));
            return false;
        }
        return true;
    }

    bool JsonDeserializer::Load(const JsonTape& tape) {
        Reset(tape, JsonView(tape, 0));
        return !tape.Empty();
    }

    bool JsonDeserializer::Load(JsonView root) {
        // A view that is not valid has no tape: the reader falls back to its own, and every read fails as for an empty document
        Reset(root.IsValid() ? *root.Tape() : _ownTape, root);
        return root.IsValid();
    }

    LinearArena& JsonDeserializer::GetNamesArena() const {
        if (!_namesArena) {
            _namesArena = MakeUnique<LinearArena>("JsonDeserializer", kNamesArenaChunkBytes);
        }
        return *_namesArena;
    }

    void JsonDeserializer::MarkScope(Scope& scope) const {
        if (scope.tracking && _namesArena) {
            scope.mark = _namesArena->GetMark();
        }
    }

    // =============================================================================
    // Lookup
    // =============================================================================

    Optional<JsonView> JsonDeserializer::FindMember(StringView name, bool advance) const {
        Scope& scope = _scopes.back();
        if (scope.node.IsObject()) {
            if (advance) {
                return scope.node.Find(name, scope.cursor);
            }
            uint32_t cursor = scope.cursor;
            return scope.node.Find(name, cursor);
        }
        if (scope.kind == ScopeKind::Element && name == kElementValueKey) {
            return scope.node;
        }
        return std::nullopt;
    }

    void JsonDeserializer::NoteAsked(StringView name) const {
        Scope& scope = _scopes.back();
        if (GetDiagnostics() == DiagnosticsMode::Off || !scope.tracking || scope.kind == ScopeKind::Array) {
            return;
        }
        if (Contains(scope.asked, name)) {
            return;
        }
        LinearArena& arena = GetNamesArena();
        if (!scope.asked) {
            scope.asked.emplace(ArenaAllocator(arena));
            scope.asked->reserve(kExpectedAskedNames);
        }
        // The caller's name may be a temporary
        char* const text = arena.Allocate<char>(name.size());
        std::memcpy(text, name.data(), name.size());
        scope.asked->push_back(StringView(text, name.size()));
    }

    Optional<JsonView> JsonDeserializer::FindForRead(StringView name) {
        NoteAsked(name);
        if (_scopes.size() == 1 && !_scopes.front().node.IsObject()) {
            ReportError(name, "Document root is not an object");
            return std::nullopt;
        }
        return FindMember(name, true);
    }

    bool JsonDeserializer::HasKey(StringView name) const {
        NoteAsked(name);
        return FindMember(name, false).has_value();
    }

    bool JsonDeserializer::IsScalarMember(StringView name) const {
        NoteAsked(name);
        const Optional<JsonView> member = FindMember(name, false);
        return member && !member->IsObject() && !member->IsArray();
    }

    void JsonDeserializer::NoteKnownKey(StringView name) {
        NoteAsked(name);
    }

    void JsonDeserializer::SkipScope() {
        _scopes.back().checked = true;
    }

    void JsonDeserializer::RejectValue(StringView name, StringView kind, JsonView value, bool outOfRange) {
        String shown;
        switch (value.Kind()) {
            case JsonKind::Object:
                shown = "<object>";
                break;
            case JsonKind::Array:
                shown = "<array>";
                break;
            case JsonKind::String: {
                value.ReadText(_decoded);
                const StringView content = CutText(StringView(_decoded), ReaderLimits::kMaxQuotedValueBytes);
                shown = Printable(content);
                if (content.size() < _decoded.size()) {
                    shown += "...";
                }
                break;
            }
            default: {
                // A number, a bool or null, as the document wrote it
                const StringView text = value.Raw();
                const StringView content = CutText(text, ReaderLimits::kMaxQuotedValueBytes);
                shown = Printable(content);
                if (content.size() < text.size()) {
                    shown += "...";
                }
                break;
            }
        }
        ReportError(name, Format("Invalid {} value '{}'{}", kind, shown, outOfRange ? " (out of range)" : ""));
    }

    // =============================================================================
    // Values
    // =============================================================================

    bool JsonDeserializer::ReadBool(StringView name, bool& outValue) {
        const Optional<JsonView> value = FindForRead(name);
        if (!value) {
            return false;
        }
        const Optional<bool> flag = value->AsBool();
        if (!flag) {
            RejectValue(name, "boolean", *value, false);
            return false;
        }
        outValue = *flag;
        return true;
    }

    template <typename T>
    bool JsonDeserializer::ReadInteger(StringView name, T& outValue) {
        const Optional<JsonView> value = FindForRead(name);
        if (!value) {
            return false;
        }
        // Only a JSON integer: 3.0 and 1.5 are numbers of another kind
        if (!value->IsInteger()) {
            RejectValue(name, "integer", *value, false);
            return false;
        }
        if (const Optional<int64_t> number = value->AsInt64()) {
            if constexpr (std::is_unsigned_v<T>) {
                if (*number < 0) {
                    RejectValue(name, "integer", *value, false);
                    return false;
                }
                if (static_cast<uint64_t>(*number) > std::numeric_limits<T>::max()) {
                    RejectValue(name, "integer", *value, true);
                    return false;
                }
            } else if (*number < std::numeric_limits<T>::min() || *number > std::numeric_limits<T>::max()) {
                RejectValue(name, "integer", *value, true);
                return false;
            }
            outValue = static_cast<T>(*number);
            return true;
        }
        // Past int64: only a positive number that fits uint64 can still be a value
        const Optional<uint64_t> big = value->AsUint64();
        if (!big || *big > static_cast<uint64_t>(std::numeric_limits<T>::max())) {
            RejectValue(name, "integer", *value, true);
            return false;
        }
        outValue = static_cast<T>(*big);
        return true;
    }

    template <typename T>
    bool JsonDeserializer::ReadFloating(StringView name, T& outValue) {
        const Optional<JsonView> value = FindForRead(name);
        if (!value) {
            return false;
        }
        if (!value->IsNumber()) {
            RejectValue(name, "number", *value, false);
            return false;
        }
        const Optional<double> number = value->AsDouble();
        // A number that is infinite, or beyond the target's type, has no value to keep; the check comes before the cast, which is undefined for a value out of range
        if (!number || !std::isfinite(*number) || std::fabs(*number) > static_cast<double>(std::numeric_limits<T>::max())) {
            RejectValue(name, "number", *value, true);
            return false;
        }
        outValue = static_cast<T>(*number);
        return true;
    }

    bool JsonDeserializer::ReadText(StringView name, String& outValue) {
        const Optional<JsonView> value = FindForRead(name);
        if (!value) {
            return false;
        }
        if (!value->ReadText(outValue)) {
            RejectValue(name, "string", *value, false);
            return false;
        }
        return true;
    }

    bool JsonDeserializer::ReadPoolString(StringView name, PoolString& outValue) {
        const Optional<JsonView> value = FindForRead(name);
        if (!value) {
            return false;
        }
        if (!value->IsString()) {
            RejectValue(name, "string", *value, false);
            return false;
        }
        // A string with no escape is interned from the text itself; one with escapes is decoded into the buffer the reader keeps
        StringView text;
        if (const Optional<StringView> plain = value->Text()) {
            text = *plain;
        } else {
            value->ReadText(_decoded);
            text = StringView(_decoded);
        }
        if (!AcceptPoolStringLength(name, text.size())) {
            return false;
        }
        // The one intern of a read: the value is what the field holds
        outValue = PoolString::Intern(text);
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
        return ReadPoolString(name, outValue);
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
        const Optional<JsonView> member = FindForRead(name);
        if (!member) {
            return false;
        }
        if (!member->IsObject()) {
            RejectValue(name, "object", *member, false);
            return false;
        }
        if (IsNestedTooDeep(name)) {
            return false;
        }
        Scope scope;
        scope.node = *member;
        scope.kind = ScopeKind::Object;
        scope.tracking = GetDiagnostics() != DiagnosticsMode::Off;
        MarkScope(scope);
        _scopes.push_back(eastl::move(scope));
        PushPathSegment(name);
        return true;
    }

    void JsonDeserializer::EndObject() {
        if (_scopes.size() > 1 && _scopes.back().kind == ScopeKind::Object) {
            CloseScope();
            PopPathSegment();
        }
    }

    bool JsonDeserializer::BeginArray(StringView name, StringView /*elementName*/, size_t& count) {
        const Optional<JsonView> member = FindForRead(name);
        if (!member) {
            return false;
        }
        if (!member->IsArray()) {
            RejectValue(name, "array", *member, false);
            return false;
        }
        if (IsNestedTooDeep(name)) {
            return false;
        }
        if (member->Size() > ReaderLimits::kMaxArrayElements) {
            ReportError(name, "Array has more elements than the limit");
            return false;
        }
        Scope scope;
        scope.node = *member;
        scope.kind = ScopeKind::Array;
        scope.cursor = member->Index() + 1;
        _scopes.push_back(eastl::move(scope));
        PushPathSegment(name);
        count = member->Size();
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
        if (array.kind != ScopeKind::Array || array.cursor >= array.node.End() || IsNestedTooDeep("")) {
            return false;
        }
        const JsonView item(*_tape, array.cursor);
        array.cursor = item.End();
        const size_t index = array.nextOrdinal++;
        Scope element;
        element.node = item;
        element.kind = ScopeKind::Element;
        element.tracking = GetDiagnostics() != DiagnosticsMode::Off;
        MarkScope(element);
        _scopes.push_back(eastl::move(element));
        PushPathArrayIndex(index);
        return true;
    }

    void JsonDeserializer::EndArrayElement() {
        if (_scopes.size() > 1 && _scopes.back().kind == ScopeKind::Element) {
            CloseScope();
            PopPathSegment();
        }
    }

    // =============================================================================
    // Unknown keys
    // =============================================================================

    void JsonDeserializer::CloseScope() {
        CheckScope(_scopes.back());
        const bool tracking = _scopes.back().tracking;
        const Optional<LinearArena::Mark> mark = _scopes.back().mark;
        // The list is gone before its memory is given back
        _scopes.pop_back();
        if (!tracking || !_namesArena) {
            return;
        }
        if (mark) {
            _namesArena->Rewind(*mark);
        } else {
            // The arena was made inside this scope, so no outer scope holds a name in it
            _namesArena->Reset();
        }
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
        if (GetDiagnostics() == DiagnosticsMode::Off || !scope.tracking || scope.kind == ScopeKind::Array || !scope.node.IsObject()) {
            return;
        }

        for (const JsonMember member : scope.node.Members()) {
            if (FindingsFull()) {
                return;
            }
            // A key with an escape is decoded for the comparison and the message
            StringView name;
            if (const Optional<StringView> plain = member.key.Text()) {
                name = *plain;
            } else {
                member.key.ReadText(_decoded);
                name = StringView(_decoded);
            }
            if (Contains(scope.asked, name)) {
                continue;
            }
            const StringView shown = CutText(name, ReaderLimits::kMaxQuotedValueBytes);
            const char* const cut = shown.size() < name.size() ? "..." : "";
            const String hint = scope.asked ? MakeDidYouMeanHint(name, {&*scope.asked}) : String();
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
