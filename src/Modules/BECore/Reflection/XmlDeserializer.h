#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Memory/LinearArena.h>
#include <BECore/Reflection/IDeserializer.h>
#include <filesystem>
#include <pugixml.hpp>

namespace BECore {

    class XmlConfig;
    class XmlNode;

    /**
     * @brief XML reader over pugixml.
     *
     * @example
     * XmlDeserializer deserializer;
     * deserializer.LoadFromFile("save.xml");
     * deserializer.BeginObject("player");
     * int32_t health;
     * deserializer.ReadAttribute("health", health);
     * deserializer.EndObject();
     *
     * if (deserializer.HasErrors()) {
     *     for (const auto& err : deserializer.GetErrors()) {
     *         LOG_ERROR("Error: {}", err.errorMessage);
     *     }
     * }
     */
    class XmlDeserializer : public IDeserializer {
    public:
        XmlDeserializer();
        ~XmlDeserializer() override = default;

        XmlDeserializer(const XmlDeserializer&) = delete;
        XmlDeserializer& operator=(const XmlDeserializer&) = delete;
        XmlDeserializer(XmlDeserializer&&) = default;
        XmlDeserializer& operator=(XmlDeserializer&&) = default;

        /**
         * @brief Load XML from file
         *
         * Refused with an error, before the file is read, when the path is not a regular file or the file is over
         * ReaderLimits::kMaxXmlDocumentBytes. Any load drops the earlier document: after a refusal nothing is readable.
         */
        bool LoadFromFile(const std::filesystem::path& path);

        /**
         * @brief Load XML from a virtual path (CoreManager::GetFileSystem()), as LoadFromFile.
         */
        bool LoadFromVirtualPath(StringView virtualPath);

        /**
         * @brief Load XML from string
         *
         * The text is UTF-8 whatever its first bytes are, and at most ReaderLimits::kMaxXmlDocumentBytes. Any load drops the earlier
         * document: after a refusal nothing is readable.
         */
        bool LoadFromString(StringView xmlContent);

        /**
         * @brief Read from a node without copying it: the document that owns the node must outlive the reads.
         */
        bool LoadFromXmlNode(const XmlNode& node);

        bool Read(StringView name, bool& outValue) override;
        bool Read(StringView name, int8_t& outValue) override;
        bool Read(StringView name, uint8_t& outValue) override;
        bool Read(StringView name, int16_t& outValue) override;
        bool Read(StringView name, uint16_t& outValue) override;
        bool Read(StringView name, int32_t& outValue) override;
        bool Read(StringView name, uint32_t& outValue) override;
        bool Read(StringView name, int64_t& outValue) override;
        bool Read(StringView name, uint64_t& outValue) override;
        bool Read(StringView name, float& outValue) override;
        bool Read(StringView name, double& outValue) override;
        bool Read(StringView name, String& outValue) override;
        bool Read(StringView name, PoolString& outValue) override;

        bool ReadAttribute(StringView name, bool& outValue) override;
        bool ReadAttribute(StringView name, int8_t& outValue) override;
        bool ReadAttribute(StringView name, uint8_t& outValue) override;
        bool ReadAttribute(StringView name, int16_t& outValue) override;
        bool ReadAttribute(StringView name, uint16_t& outValue) override;
        bool ReadAttribute(StringView name, int32_t& outValue) override;
        bool ReadAttribute(StringView name, uint32_t& outValue) override;
        bool ReadAttribute(StringView name, int64_t& outValue) override;
        bool ReadAttribute(StringView name, uint64_t& outValue) override;
        bool ReadAttribute(StringView name, float& outValue) override;
        bool ReadAttribute(StringView name, double& outValue) override;
        bool ReadAttribute(StringView name, String& outValue) override;
        bool ReadAttribute(StringView name, PoolString& outValue) override;

        /// The text of the attribute in the DOM, no copy: valid until the next load of this reader
        bool ReadAttributeView(StringView name, StringView& outView) override;

        bool BeginObject(StringView name) override;
        void EndObject() override;

        bool BeginArray(StringView name, StringView elementName, size_t& count) override;
        void EndArray() override;
        bool BeginArrayElement() override;
        void EndArrayElement() override;

        void SetStrictValues(bool strict) override {
            _strictValues = strict;
        }
        bool IsStrictValues() const override {
            return _strictValues;
        }

        // Load diagnostics (see IDeserializer::SetDiagnostics). With the mode not Off a scope that ends with an attribute or a child
        // element nobody asked for gives the finding "Unknown attribute|element 'x'" at <scope path>.x, with "did you mean 'y'?" when a
        // name asked in that scope is near. Asked is what counts, not read: Read, ReadAttribute, BeginObject, BeginArray, HasKey and
        // NoteKnownKey name a key, so a value that fails to parse is no unknown key. A scope is checked once, when it ends
        // (EndObject, EndArray, EndArrayElement) or, for the root, at FinishLoad. An array scope checks its own attributes only: its
        // children are taken by position (BeginArrayElement) and are the elements. Children of one name count once. Off tracks nothing.
        bool HasKey(StringView name) const override;
        void AttributeNames(Vector<String>& names) const override;
        void NoteKnownKey(StringView name) override;
        void FinishLoad() override;
        void SkipScope() override;

        bool HasErrors() const override;
        const Vector<DeserializeError>& GetErrors() const override;
        void ClearErrors() override;

    protected:
        void AddError(StringView path, StringView message, int32_t line) override;

    private:
        /**
         * @brief Drop the document and every node of it that a stack holds, before another one is loaded
         */
        void ResetDocument();

        /**
         * @brief Whether one more open object, array or element would pass ReaderLimits::kMaxXmlNesting; reports it under @p name when it would
         */
        bool IsNestedTooDeep(StringView name);

        pugi::xml_node GetChild(StringView name) const;

        pugi::xml_attribute GetAttribute(StringView name) const;

        /**
         * @brief Strict read of an attribute: the whole text must be a value of T
         *
         * A missing attribute is false with no error, as in the lenient read; a malformed
         * text is reported under the name and the target keeps its value.
         */
        template <typename T>
        bool ReadStrictAttribute(StringView name, T& outValue);

        /**
         * @brief Strict read of a child element (see ReadStrictAttribute)
         */
        template <typename T>
        bool ReadStrictElement(StringView name, T& outValue);

        template <typename T>
        bool ApplyStrictText(StringView name, StringView text, T& outValue);

        /// What one open node was asked for; in lockstep with _nodeStack while the mode is not Off. The lists and the text of the names sit in
        /// the deserializer's arena from the mark of the scope on, and CloseScope gives them back.
        struct Scope {
            Scope(pugi::xml_node openNode, LinearArena& arena, const LinearArena::Mark& openMark)
                : node(openNode), mark(openMark), askedAttributes(ArenaAllocator(arena)), askedElements(ArenaAllocator(arena)) {}

            pugi::xml_node node;
            LinearArena::Mark mark;
            ArenaVector<StringView> askedAttributes;
            ArenaVector<StringView> askedElements;
            bool isArray = false;
            bool checked = false;
        };

        /// Drops every scope and everything the arena holds; no scope is alive afterwards, so no mark is used again
        void DropScopes();

        /// The arena of the scopes, made by the first scope: a deserializer that never runs with diagnostics holds none
        LinearArena& GetScopeArena() const;

        /// Adds @p name to @p names unless it is there; the text is copied into the arena, because a caller's name may be a temporary
        void AddAskedName(ArenaVector<StringView>& names, StringView name) const;

        /// Notes that a key was asked of the current node, as an attribute and / or as a child element (const: HasKey asks too)
        void NoteAsked(StringView name, bool asAttribute, bool asElement) const;

        /// Makes a Scope for every open node that has none; all but the innermost count as checked (a mode switched on in the middle of a load did not see them)
        void SyncScopes() const;

        /// Called after a node is pushed on _nodeStack by BeginObject, BeginArray or BeginArrayElement
        void OpenScope(bool isArray);

        /// Called before a node is popped from _nodeStack: checks its scope and drops it
        void CloseScope();

        void CheckScope(Scope& scope);

        /// The finding of one unknown name; @p scope gives the names to suggest from
        void ReportUnknown(const Scope& scope, StringView kind, StringView name);

        bool FindingsFull() const;

        pugi::xml_document _document;
        Vector<pugi::xml_node> _nodeStack;
        mutable Vector<Scope> _scopes;
        // Held by pointer: a LinearArena cannot move, a moved deserializer keeps its arena, and the views of the scopes point into it
        mutable UniquePtr<LinearArena> _scopeArena;

        struct ArrayContext {
            pugi::xml_node parentNode;
            pugi::xml_node cursor;  // the next element to hand out; empty after the last
            size_t index = 0;
            size_t count = 0;
        };
        Vector<ArrayContext> _arrayStack;

        Vector<DeserializeError> _errors;

        bool _strictValues = false;
    };

}  // namespace BECore
