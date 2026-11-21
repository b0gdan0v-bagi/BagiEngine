#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <cmath>

namespace BECore {

    XmlDeserializer::XmlDeserializer() {}

    void XmlDeserializer::ResetDocument() {
        // The nodes of the stacks belong to the document: they go before it does, so a refused load leaves nothing that points into it
        _nodeStack.clear();
        _arrayStack.clear();
        _scopes.clear();
        ClearPathSegments();
        _document.reset();
    }

    bool XmlDeserializer::LoadFromFile(const std::filesystem::path& path) {
        ResetDocument();

        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            // A folder, a pipe or a device is no document, and has no size to check
            AddError("", std::filesystem::exists(path, error) ? "XML path is not a regular file" : "Failed to parse XML file", 0);
            return false;
        }
        const uintmax_t size = std::filesystem::file_size(path, error);
        if (error || size > ReaderLimits::kMaxXmlDocumentBytes) {
            AddError("", error ? "Failed to read the size of the XML file" : "XML file is larger than the limit", 0);
            return false;
        }

        pugi::xml_parse_result result = _document.load_file(path.c_str());
        if (!result) {
            AddError("", "Failed to parse XML file", 0);
            return false;
        }

        _nodeStack.push_back(_document.document_element());
        return true;
    }

    bool XmlDeserializer::LoadFromVirtualPath(StringView virtualPath) {
        auto realPath = CoreManager::GetFileSystem().ResolvePath(virtualPath);
        if (realPath.empty()) {
            AddError("", "Failed to resolve virtual path", 0);
            return false;
        }
        return LoadFromFile(realPath);
    }

    bool XmlDeserializer::LoadFromString(StringView xmlContent) {
        ResetDocument();

        if (xmlContent.size() > ReaderLimits::kMaxXmlDocumentBytes) {
            AddError("", "XML string is larger than the limit", 0);
            return false;
        }

        // UTF-8 as the engine's strings are: the automatic detection would read a text that starts like a UTF-16 or a UTF-32 mark as words,
        // and a view into the middle of a buffer is not aligned for that
        pugi::xml_parse_result result = _document.load_buffer(xmlContent.data(), xmlContent.size(), pugi::parse_default, pugi::encoding_utf8);
        if (!result) {
            AddError("", "Failed to parse XML string", 0);
            return false;
        }

        _nodeStack.push_back(_document.document_element());
        return true;
    }

    bool XmlDeserializer::LoadFromXmlNode(const XmlNode& node) {
        _nodeStack.clear();
        _arrayStack.clear();
        _scopes.clear();
        ClearPathSegments();

        pugi::xml_node pugiNode = node.GetPugiNode();
        if (!pugiNode) {
            AddError("", "Invalid XmlNode", 0);
            return false;
        }

        _nodeStack.push_back(pugiNode);
        return true;
    }

    bool XmlDeserializer::IsNestedTooDeep(StringView name) {
        if (_nodeStack.size() < ReaderLimits::kMaxXmlNesting) {
            return false;
        }
        ReportError(name, "Nesting is deeper than the limit");
        return true;
    }

    pugi::xml_node XmlDeserializer::GetChild(StringView name) const {
        if (_nodeStack.empty()) {
            return pugi::xml_node();
        }

        NoteAsked(name, false, true);
        String nameStr(name);
        return _nodeStack.back().child(nameStr.c_str());
    }

    pugi::xml_attribute XmlDeserializer::GetAttribute(StringView name) const {
        if (_nodeStack.empty()) {
            return pugi::xml_attribute();
        }

        NoteAsked(name, true, false);
        String nameStr(name);
        return _nodeStack.back().attribute(nameStr.c_str());
    }

    bool XmlDeserializer::HasKey(StringView name) const {
        // Both are asked, so the one that is found must not hide the other
        const bool hasAttribute = !GetAttribute(name).empty();
        const bool hasElement = !GetChild(name).empty();
        return hasAttribute || hasElement;
    }

    void XmlDeserializer::AttributeNames(Vector<String>& names) const {
        if (_nodeStack.empty()) {
            return;
        }
        for (const pugi::xml_attribute attribute : _nodeStack.back().attributes()) {
            names.push_back(String(attribute.name()));
        }
    }

    // =============================================================================
    // Unknown keys
    // =============================================================================

    namespace {

        bool Contains(const Vector<String>& names, StringView name) {
            for (const String& known : names) {
                if (StringView(known) == name) {
                    return true;
                }
            }
            return false;
        }

        void AddName(Vector<String>& names, StringView name) {
            if (!Contains(names, name)) {
                names.push_back(String(name));
            }
        }

    }  // namespace

    void XmlDeserializer::SyncScopes() const {
        while (_scopes.size() < _nodeStack.size()) {
            Scope scope;
            scope.node = _nodeStack[_scopes.size()];
            scope.checked = _scopes.size() + 1 < _nodeStack.size();
            _scopes.push_back(eastl::move(scope));
        }
    }

    void XmlDeserializer::NoteAsked(StringView name, bool asAttribute, bool asElement) const {
        if (GetDiagnostics() == DiagnosticsMode::Off || _nodeStack.empty()) {
            return;
        }
        SyncScopes();
        Scope& scope = _scopes.back();
        if (asAttribute) {
            AddName(scope.askedAttributes, name);
        }
        if (asElement) {
            AddName(scope.askedElements, name);
        }
    }

    void XmlDeserializer::NoteKnownKey(StringView name) {
        NoteAsked(name, true, true);
    }

    void XmlDeserializer::SkipScope() {
        if (GetDiagnostics() == DiagnosticsMode::Off || _nodeStack.empty()) {
            return;
        }
        SyncScopes();
        _scopes.back().checked = true;
    }

    void XmlDeserializer::OpenScope(bool isArray) {
        if (GetDiagnostics() == DiagnosticsMode::Off) {
            return;
        }
        SyncScopes();
        _scopes.back().isArray = isArray;
    }

    void XmlDeserializer::CloseScope() {
        // A scope exists for every open node only while the mode is on: the two stacks stay in step because a node pushed under Off has no scope
        if (_scopes.size() != _nodeStack.size()) {
            return;
        }
        CheckScope(_scopes.back());
        _scopes.pop_back();
    }

    bool XmlDeserializer::FindingsFull() const {
        const Vector<DeserializeError>& list = GetDiagnostics() == DiagnosticsMode::Warn ? GetWarnings() : GetErrors();
        return list.size() >= ReaderLimits::kMaxRecordedErrors;
    }

    void XmlDeserializer::ReportUnknown(const Scope& scope, StringView kind, StringView name) {
        const StringView shown = CutText(name, ReaderLimits::kMaxQuotedValueBytes);
        const char* const cut = shown.size() < name.size() ? "..." : "";
        const String hint = MakeDidYouMeanHint(name, {&scope.askedAttributes, &scope.askedElements});
        ReportFinding(name, Format("Unknown {} '{}{}'{}", kind, shown, cut, hint));
    }

    void XmlDeserializer::CheckScope(Scope& scope) {
        if (scope.checked) {
            return;
        }
        scope.checked = true;
        if (GetDiagnostics() == DiagnosticsMode::Off) {
            return;
        }

        for (const pugi::xml_attribute attribute : scope.node.attributes()) {
            if (FindingsFull()) {
                return;
            }
            const StringView name = attribute.name();
            if (!Contains(scope.askedAttributes, name)) {
                ReportUnknown(scope, "attribute", name);
            }
        }
        if (scope.isArray) {
            return;
        }

        Vector<String> reported;
        for (const pugi::xml_node child : scope.node.children()) {
            if (FindingsFull()) {
                return;
            }
            if (child.type() != pugi::node_element) {
                continue;
            }
            const StringView name = child.name();
            if (!Contains(scope.askedElements, name) && !Contains(reported, name)) {
                reported.push_back(String(name));
                ReportUnknown(scope, "element", name);
            }
        }
    }

    void XmlDeserializer::FinishLoad() {
        // Open scopes are the caller's to close: only a balanced load has a root to finish
        if (GetDiagnostics() == DiagnosticsMode::Off || _nodeStack.size() != 1) {
            return;
        }
        SyncScopes();
        CheckScope(_scopes.front());
    }

    void XmlDeserializer::AddError(StringView path, StringView message, int32_t line) {
        StoreError(_errors, path, message, line);
    }

    namespace {

        constexpr char kPlus = '+';
        constexpr char kMinus = '-';

        bool IsAsciiSpace(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }

        StringView TrimAsciiSpaces(StringView text) {
            while (!text.empty() && IsAsciiSpace(text.front())) {
                text.remove_prefix(1);
            }
            while (!text.empty() && IsAsciiSpace(text.back())) {
                text.remove_suffix(1);
            }
            return text;
        }

        // A leading '+' is accepted (pugixml takes one); std::from_chars does not take one
        StringView DropPlusSign(StringView text) {
            if (!text.empty() && text.front() == kPlus) {
                text.remove_prefix(1);
                // "+-1" and "++1" stay malformed
                if (text.empty() || text.front() == kPlus || text.front() == kMinus) {
                    return {};
                }
            }
            return text;
        }

        enum class ParseStatus { Ok, Malformed, OutOfRange };

        ParseStatus ParseBoolText(StringView text, bool& outValue) {
            text = TrimAsciiSpaces(text);
            if (text == "true" || text == "1") {
                outValue = true;
                return ParseStatus::Ok;
            }
            if (text == "false" || text == "0") {
                outValue = false;
                return ParseStatus::Ok;
            }
            return ParseStatus::Malformed;
        }

        template <typename T>
        ParseStatus ParseNumberText(StringView text, T& outValue) {
            text = DropPlusSign(TrimAsciiSpaces(text));
            if (text.empty()) {
                return ParseStatus::Malformed;
            }

            T parsed{};
            const char* const end = text.data() + text.size();
            const auto result = std::from_chars(text.data(), end, parsed);
            if (result.ec == std::errc::invalid_argument || result.ptr != end) {
                return ParseStatus::Malformed;
            }
            if (result.ec == std::errc::result_out_of_range) {
                return ParseStatus::OutOfRange;
            }
            if constexpr (std::is_floating_point_v<T>) {
                // "inf" and "nan" are text, not numbers of a config
                if (!std::isfinite(parsed)) {
                    return ParseStatus::Malformed;
                }
            }
            outValue = parsed;
            return ParseStatus::Ok;
        }

        template <typename T>
        ParseStatus ParseText(StringView text, T& outValue) {
            if constexpr (std::is_same_v<T, bool>) {
                return ParseBoolText(text, outValue);
            } else {
                return ParseNumberText(text, outValue);
            }
        }

        template <typename T>
        constexpr const char* KindName() {
            if constexpr (std::is_same_v<T, bool>) {
                return "boolean";
            } else if constexpr (std::is_integral_v<T>) {
                return "integer";
            } else {
                return "number";
            }
        }

    }  // namespace

    template <typename T>
    bool XmlDeserializer::ApplyStrictText(StringView name, StringView text, T& outValue) {
        const ParseStatus status = ParseText(text, outValue);
        if (status == ParseStatus::Ok) {
            return true;
        }
        const char* const reason = status == ParseStatus::OutOfRange ? " (out of range)" : "";
        const StringView quoted = CutText(text, ReaderLimits::kMaxQuotedValueBytes);
        const char* const cut = quoted.size() < text.size() ? "..." : "";
        ReportError(name, Format("Invalid {} value '{}{}'{}", KindName<T>(), quoted, cut, reason));
        return false;
    }

    template <typename T>
    bool XmlDeserializer::ReadStrictAttribute(StringView name, T& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            // As in the lenient read: a missing attribute is no error
            return false;
        }
        return ApplyStrictText(name, attr.as_string(), outValue);
    }

    template <typename T>
    bool XmlDeserializer::ReadStrictElement(StringView name, T& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }
        return ApplyStrictText(name, node.text().as_string(), outValue);
    }

    // =============================================================================
    // Read methods (child elements)
    // =============================================================================

    bool XmlDeserializer::Read(StringView name, bool& outValue) {
        if (_strictValues) {
            return ReadStrictElement(name, outValue);
        }

        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        StringView text = node.text().as_string();
        outValue = (text == "true" || text == "1");
        return true;
    }

    bool XmlDeserializer::Read(StringView name, int8_t& outValue) {
        if (_strictValues) {
            return ReadStrictElement(name, outValue);
        }

        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        outValue = static_cast<int8_t>(node.text().as_int());
        return true;
    }

    bool XmlDeserializer::Read(StringView name, uint8_t& outValue) {
        if (_strictValues) {
            return ReadStrictElement(name, outValue);
        }

        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        outValue = static_cast<uint8_t>(node.text().as_uint());
        return true;
    }

    bool XmlDeserializer::Read(StringView name, int16_t& outValue) {
        if (_strictValues) {
            return ReadStrictElement(name, outValue);
        }

        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        outValue = static_cast<int16_t>(node.text().as_int());
        return true;
    }

    bool XmlDeserializer::Read(StringView name, uint16_t& outValue) {
        if (_strictValues) {
            return ReadStrictElement(name, outValue);
        }

        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        outValue = static_cast<uint16_t>(node.text().as_uint());
        return true;
    }

    bool XmlDeserializer::Read(StringView name, int32_t& outValue) {
        if (_strictValues) {
            return ReadStrictElement(name, outValue);
        }

        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        outValue = node.text().as_int();
        return true;
    }

    bool XmlDeserializer::Read(StringView name, uint32_t& outValue) {
        if (_strictValues) {
            return ReadStrictElement(name, outValue);
        }

        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        outValue = node.text().as_uint();
        return true;
    }

    bool XmlDeserializer::Read(StringView name, int64_t& outValue) {
        if (_strictValues) {
            return ReadStrictElement(name, outValue);
        }

        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        outValue = static_cast<int64_t>(node.text().as_llong());
        return true;
    }

    bool XmlDeserializer::Read(StringView name, uint64_t& outValue) {
        if (_strictValues) {
            return ReadStrictElement(name, outValue);
        }

        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        outValue = static_cast<uint64_t>(node.text().as_ullong());
        return true;
    }

    bool XmlDeserializer::Read(StringView name, float& outValue) {
        if (_strictValues) {
            return ReadStrictElement(name, outValue);
        }

        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        outValue = node.text().as_float();
        return true;
    }

    bool XmlDeserializer::Read(StringView name, double& outValue) {
        if (_strictValues) {
            return ReadStrictElement(name, outValue);
        }

        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        outValue = node.text().as_double();
        return true;
    }

    bool XmlDeserializer::Read(StringView name, String& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        const char* text = node.text().as_string();
        outValue = String(text);
        return true;
    }

    bool XmlDeserializer::Read(StringView name, PoolString& outValue) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            ReportError(name, "Element not found");
            return false;
        }

        const StringView text{node.text().as_string()};
        if (!AcceptPoolStringLength(name, text.size())) {
            return false;
        }
        outValue = PoolString::Intern(text);
        return true;
    }

    // =============================================================================
    // ReadAttribute methods (XML attributes)
    // =============================================================================

    bool XmlDeserializer::ReadAttribute(StringView name, bool& outValue) {
        if (_strictValues) {
            return ReadStrictAttribute(name, outValue);
        }

        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            // Don't add error for missing attributes (they might be optional)
            return false;
        }

        StringView text = attr.as_string();
        outValue = (text == "true" || text == "1");
        return true;
    }

    bool XmlDeserializer::ReadAttribute(StringView name, int8_t& outValue) {
        if (_strictValues) {
            return ReadStrictAttribute(name, outValue);
        }

        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }

        outValue = static_cast<int8_t>(attr.as_int());
        return true;
    }

    bool XmlDeserializer::ReadAttribute(StringView name, uint8_t& outValue) {
        if (_strictValues) {
            return ReadStrictAttribute(name, outValue);
        }

        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }

        outValue = static_cast<uint8_t>(attr.as_uint());
        return true;
    }

    bool XmlDeserializer::ReadAttribute(StringView name, int16_t& outValue) {
        if (_strictValues) {
            return ReadStrictAttribute(name, outValue);
        }

        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }

        outValue = static_cast<int16_t>(attr.as_int());
        return true;
    }

    bool XmlDeserializer::ReadAttribute(StringView name, uint16_t& outValue) {
        if (_strictValues) {
            return ReadStrictAttribute(name, outValue);
        }

        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }

        outValue = static_cast<uint16_t>(attr.as_uint());
        return true;
    }

    bool XmlDeserializer::ReadAttribute(StringView name, int32_t& outValue) {
        if (_strictValues) {
            return ReadStrictAttribute(name, outValue);
        }

        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }

        outValue = attr.as_int();
        return true;
    }

    bool XmlDeserializer::ReadAttribute(StringView name, uint32_t& outValue) {
        if (_strictValues) {
            return ReadStrictAttribute(name, outValue);
        }

        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }

        outValue = attr.as_uint();
        return true;
    }

    bool XmlDeserializer::ReadAttribute(StringView name, int64_t& outValue) {
        if (_strictValues) {
            return ReadStrictAttribute(name, outValue);
        }

        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }

        outValue = static_cast<int64_t>(attr.as_llong());
        return true;
    }

    bool XmlDeserializer::ReadAttribute(StringView name, uint64_t& outValue) {
        if (_strictValues) {
            return ReadStrictAttribute(name, outValue);
        }

        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }

        outValue = static_cast<uint64_t>(attr.as_ullong());
        return true;
    }

    bool XmlDeserializer::ReadAttribute(StringView name, float& outValue) {
        if (_strictValues) {
            return ReadStrictAttribute(name, outValue);
        }

        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }

        outValue = attr.as_float();
        return true;
    }

    bool XmlDeserializer::ReadAttribute(StringView name, double& outValue) {
        if (_strictValues) {
            return ReadStrictAttribute(name, outValue);
        }

        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }

        outValue = attr.as_double();
        return true;
    }

    bool XmlDeserializer::ReadAttribute(StringView name, String& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }

        const char* text = attr.as_string();
        outValue = String(text);
        return true;
    }

    bool XmlDeserializer::ReadAttribute(StringView name, PoolString& outValue) {
        pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }

        const StringView text{attr.as_string()};
        if (!AcceptPoolStringLength(name, text.size())) {
            return false;
        }
        outValue = PoolString::Intern(text);
        return true;
    }

    // =============================================================================
    // Object nesting
    // =============================================================================

    bool XmlDeserializer::BeginObject(StringView name) {
        pugi::xml_node node = GetChild(name);
        if (!node) {
            return false;
        }
        if (IsNestedTooDeep(name)) {
            return false;
        }
        _nodeStack.push_back(node);
        PushPathSegment(name);
        OpenScope(false);
        return true;
    }

    void XmlDeserializer::EndObject() {
        if (_nodeStack.size() > 1) {
            CloseScope();
            _nodeStack.pop_back();
            PopPathSegment();
        }
    }

    // =============================================================================
    // Array support
    // =============================================================================

    bool XmlDeserializer::BeginArray(StringView name, StringView elementName, size_t& count) {
        pugi::xml_node node = GetChild(name);
        if (!node || IsNestedTooDeep(name)) {
            return false;
        }

        ArrayContext ctx;
        ctx.parentNode = node;
        ctx.currentIndex = 0;
        // Counted first: a pass over the children costs less than the doublings of a vector that grows to the same size
        size_t elementCount = 0;
        for (pugi::xml_node child : node.children()) {
            if (child.type() == pugi::node_element && ++elementCount == ReaderLimits::kMaxArrayElements) {
                break;
            }
        }
        ctx.elements.reserve(elementCount);
        for (pugi::xml_node child : node.children()) {
            // A comment between the elements is no element: it would shift every index after it
            if (child.type() != pugi::node_element) {
                continue;
            }
            if (ctx.elements.size() == ReaderLimits::kMaxArrayElements) {
                ReportError(name, "Array has more elements than the limit");
                return false;
            }
            ctx.elements.push_back(child);
        }
        count = ctx.elements.size();
        _arrayStack.push_back(eastl::move(ctx));
        _nodeStack.push_back(node);
        PushPathSegment(name);
        OpenScope(true);
        return true;
    }

    void XmlDeserializer::EndArray() {
        if (!_arrayStack.empty()) {
            _arrayStack.pop_back();
        }
        if (_nodeStack.size() > 1) {
            CloseScope();
            _nodeStack.pop_back();
            PopPathSegment();
        }
    }

    bool XmlDeserializer::BeginArrayElement() {
        if (_arrayStack.empty()) {
            return false;
        }
        ArrayContext& ctx = _arrayStack.back();
        if (ctx.currentIndex >= ctx.elements.size() || IsNestedTooDeep("")) {
            return false;
        }
        PushPathArrayIndex(ctx.currentIndex);
        _nodeStack.push_back(ctx.elements[ctx.currentIndex++]);
        OpenScope(false);
        return true;
    }

    void XmlDeserializer::EndArrayElement() {
        if (_nodeStack.size() > 1) {
            CloseScope();
            _nodeStack.pop_back();
            PopPathSegment();
        }
    }

    // =============================================================================
    // Error tracking
    // =============================================================================

    bool XmlDeserializer::HasErrors() const {
        return !_errors.empty();
    }

    const Vector<DeserializeError>& XmlDeserializer::GetErrors() const {
        return _errors;
    }

    void XmlDeserializer::ClearErrors() {
        _errors.clear();
    }

}  // namespace BECore
