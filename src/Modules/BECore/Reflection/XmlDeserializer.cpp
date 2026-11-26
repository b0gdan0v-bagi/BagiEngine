#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/StrictText.h>
#include <BECore/Reflection/XmlDeserializer.h>

namespace BECore {

    XmlDeserializer::XmlDeserializer() {}

    void XmlDeserializer::ResetDocument() {
        // The nodes of the stacks belong to the document: they go before it does, so a refused load leaves nothing that points into it
        _nodeStack.clear();
        _arrayStack.clear();
        DropScopes();
        ClearPathSegments();
        _document.reset();
    }

    bool XmlDeserializer::LoadFromFile(const Path& path) {
        ResetDocument();

        const Optional<FileInfo> info = GetFileInfo(path);
        if (!info || info->kind != FileKind::File) {
            // A folder, a pipe or a device is no document, and has no size to check
            AddError("", info ? "XML path is not a regular file" : "Failed to parse XML file", 0);
            return false;
        }
        if (info->size > ReaderLimits::kMaxXmlDocumentBytes) {
            AddError("", "XML file is larger than the limit", 0);
            return false;
        }

        // load_file(const char*) is the ANSI code page on Windows, which a UTF-8 path is not
        const std::expected<Vector<uint8_t>, String> bytes = ReadFileBytes(path, ReaderLimits::kMaxXmlDocumentBytes);
        if (!bytes || !_document.load_buffer(bytes->data(), bytes->size(), pugi::parse_default, pugi::encoding_auto)) {
            AddError("", "Failed to parse XML file", 0);
            return false;
        }

        _nodeStack.push_back(_document.document_element());
        return true;
    }

    bool XmlDeserializer::LoadFromVirtualPath(StringView virtualPath) {
        auto realPath = CoreManager::GetFileSystem().ResolvePath(virtualPath);
        if (realPath.Empty()) {
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
        DropScopes();
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

    namespace {

        /// The first element at or after @p node among its siblings that is named @p name, of any name when it is empty (a comment or a text
        /// is no element)
        pugi::xml_node SkipToElement(pugi::xml_node node, StringView name) {
            while (node && (node.type() != pugi::node_element || (!name.empty() && StringView(node.name()) != name))) {
                node = node.next_sibling();
            }
            return node;
        }

    }  // namespace

    pugi::xml_node XmlDeserializer::GetChild(StringView name) const {
        if (_nodeStack.empty()) {
            return pugi::xml_node();
        }

        NoteAsked(name, false, true);
        return FindXmlChild(_nodeStack.back(), name);
    }

    pugi::xml_attribute XmlDeserializer::GetAttribute(StringView name) const {
        if (_nodeStack.empty()) {
            return pugi::xml_attribute();
        }

        NoteAsked(name, true, false);
        return FindXmlAttribute(_nodeStack.back(), name);
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

        /// Each scope keeps a few names, and the arena is rewound with every scope: a small chunk is enough
        constexpr size_t kScopeArenaChunkBytes = 4 * 1024;

        /// The names a scope usually collects, reserved at once because an arena vector leaves its old blocks behind when it grows
        constexpr size_t kExpectedAskedNames = 16;

        bool Contains(const ArenaVector<StringView>& names, StringView name) {
            for (const StringView known : names) {
                if (known == name) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    LinearArena& XmlDeserializer::GetScopeArena() const {
        if (!_scopeArena) {
            _scopeArena = MakeUnique<LinearArena>("XmlDeserializer", kScopeArenaChunkBytes);
        }
        return *_scopeArena;
    }

    void XmlDeserializer::DropScopes() {
        _scopes.clear();
        if (_scopeArena) {
            _scopeArena->Reset();
        }
    }

    void XmlDeserializer::AddAskedName(ArenaVector<StringView>& names, StringView name) const {
        if (Contains(names, name)) {
            return;
        }
        if (names.capacity() == 0) {
            names.reserve(kExpectedAskedNames);
        }
        char* const text = GetScopeArena().Allocate<char>(name.size());
        std::memcpy(text, name.data(), name.size());
        names.push_back(StringView(text, name.size()));
    }

    void XmlDeserializer::SyncScopes() const {
        while (_scopes.size() < _nodeStack.size()) {
            LinearArena& arena = GetScopeArena();
            const bool isInnermost = _scopes.size() + 1 == _nodeStack.size();
            Scope& scope = _scopes.emplace_back(_nodeStack[_scopes.size()], arena, arena.GetMark());
            scope.checked = !isInnermost;
        }
    }

    void XmlDeserializer::NoteAsked(StringView name, bool asAttribute, bool asElement) const {
        if (GetDiagnostics() == DiagnosticsMode::Off || _nodeStack.empty()) {
            return;
        }
        SyncScopes();
        Scope& scope = _scopes.back();
        if (asAttribute) {
            AddAskedName(scope.askedAttributes, name);
        }
        if (asElement) {
            AddAskedName(scope.askedElements, name);
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

    void XmlDeserializer::OpenScope(bool takesEveryElement) {
        if (GetDiagnostics() == DiagnosticsMode::Off) {
            return;
        }
        SyncScopes();
        _scopes.back().takesEveryElement = takesEveryElement;
    }

    void XmlDeserializer::CloseScope() {
        // A scope exists for every open node only while the mode is on: the two stacks stay in step because a node pushed under Off has no scope
        if (_scopes.size() != _nodeStack.size()) {
            return;
        }
        CheckScope(_scopes.back());
        // The lists are gone before their memory is given back
        const LinearArena::Mark mark = _scopes.back().mark;
        _scopes.pop_back();
        GetScopeArena().Rewind(mark);
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
        if (scope.takesEveryElement) {
            return;
        }

        LinearArena& arena = GetScopeArena();
        const LinearArena::Mark reportedMark = arena.GetMark();
        {
            // Views into the document, which outlives the check; the list is the arena's and goes back with the mark below
            ArenaVector<StringView> reported{ArenaAllocator(arena)};
            for (const pugi::xml_node child : scope.node.children()) {
                if (FindingsFull()) {
                    break;
                }
                if (child.type() != pugi::node_element) {
                    continue;
                }
                const StringView name = child.name();
                if (!Contains(scope.askedElements, name) && !Contains(reported, name)) {
                    if (reported.capacity() == 0) {
                        reported.reserve(kExpectedAskedNames);
                    }
                    reported.push_back(name);
                    ReportUnknown(scope, "element", name);
                }
            }
        }
        arena.Rewind(reportedMark);
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

    template <typename T>
    bool XmlDeserializer::ApplyStrictText(StringView name, StringView text, T& outValue) {
        const StrictText::Status status = StrictText::Parse(text, outValue);
        if (status == StrictText::Status::Ok) {
            return true;
        }
        const StringView shown = CutText(text, ReaderLimits::kMaxQuotedValueBytes);
        ReportError(name, StrictText::InvalidValueMessage<T>(text, shown, status));
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

    bool XmlDeserializer::ReadAttributeView(StringView name, StringView& outView) {
        const pugi::xml_attribute attr = GetAttribute(name);
        if (!attr) {
            return false;
        }
        outView = StringView(attr.as_string());
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

        // Empty: the array takes an element of any name
        const StringView filter = elementName == kGenericElementName ? StringView() : elementName;

        // Counted first, so the count is an upper bound for the caller's reserve and an array over the limit is refused before any element is read
        const pugi::xml_node first = SkipToElement(node.first_child(), filter);
        size_t elementCount = 0;
        for (pugi::xml_node child = first; child; child = SkipToElement(child.next_sibling(), filter)) {
            if (++elementCount > ReaderLimits::kMaxArrayElements) {
                ReportError(name, "Array has more elements than the limit");
                return false;
            }
        }

        ArrayContext ctx;
        ctx.parentNode = node;
        ctx.cursor = first;
        ctx.elementName = filter.empty() ? StringView() : StringView(first.name());
        ctx.count = elementCount;
        count = elementCount;
        _arrayStack.push_back(ctx);
        _nodeStack.push_back(node);
        PushPathSegment(name);
        OpenScope(filter.empty());
        if (!filter.empty()) {
            // So that the check of the array's scope reports the elements of every other name
            NoteAsked(filter, false, true);
        }
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
        if (ctx.index >= ctx.count || !ctx.cursor || IsNestedTooDeep("")) {
            return false;
        }
        PushPathArrayIndex(ctx.index++);
        _nodeStack.push_back(ctx.cursor);
        ctx.cursor = SkipToElement(ctx.cursor.next_sibling(), ctx.elementName);
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
