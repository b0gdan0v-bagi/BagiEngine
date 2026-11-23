#pragma once

#include <BECore/Control/JsonFwd.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Memory/LinearArena.h>
#include <BECore/Reflection/IDeserializer.h>

namespace BECore {

    /**
     * @brief Reads a JSON document into what `Ser::Load` and the generated `Deserialize` ask for
     *
     * Walks the JsonTape of the text, never a DOM. Values are always strict: a bool is a JSON bool, a number a JSON number of the target's kind
     * and range, a string a JSON string, and a refusal is an error under the field's path that leaves the target as it was. The shapes it reads
     * are those of JsonSerializer (Reflection/CLAUDE.md, "JSON shapes of the reflection pair"). A key is found among the members of the open
     * object whatever the order of the text, with a cursor after the last match, so a document that keeps the order of the reads costs one key
     * compare each.
     *
     * One reader serves many messages: Load() resets the document, the errors and the scopes, keeps the settings (diagnostics, purpose filter)
     * and the buffers, so the second load of a message of the same shape allocates nothing. **The text, or the tape passed to Load(), must stay
     * alive and unchanged until the next Load() or the end of the reader**: strings are read from it on demand. The text is a client's: Load()
     * bounds it (ReaderLimits::kMaxJsonDocumentBytes, kMaxJsonNesting) and the reader counts open scopes against ReaderLimits::kMaxXmlNesting.
     */
    class JsonDeserializer final : public IDeserializer {
    public:
        /// An empty reader: every read is the error `Document root is not an object` until Load()
        JsonDeserializer();

        /**
         * @brief The interim bridge from the DOM: writes @p document as text into a string the reader keeps, and loads that
         *
         * Costs a dump and a parse; the callers that still hold a `Json` use it until they read from text, and the last step of the move to the
         * tape removes it. @p document is not needed after the call.
         */
        explicit JsonDeserializer(const Json& document);
        ~JsonDeserializer() override;

        JsonDeserializer(const JsonDeserializer&) = delete;
        JsonDeserializer& operator=(const JsonDeserializer&) = delete;
        JsonDeserializer(JsonDeserializer&&) = delete;
        JsonDeserializer& operator=(JsonDeserializer&&) = delete;

        /**
         * @brief Reads @p text as the document, its root being the first scope
         *
         * Resets the scopes, the path, the errors, the findings and the names the last document asked; keeps the settings. The text is parsed
         * into the reader's own tape and borrowed (see the class). A text that does not parse records one error with the empty path,
         * `Invalid JSON: <message> at offset <n>`, and returns false; every read then fails as it does for a root that is not an object.
         */
        bool Load(StringView text);

        /// The same over a tape the caller parsed and keeps alive and unchanged; it may be read by many readers
        bool Load(const JsonTape& tape);

        /**
         * @brief The same over one value of a tape the caller keeps alive and unchanged: the arguments of a call, a sub-object of a request
         *
         * @p root is the document's root, any kind of value; one that is not an object fails every read, as for a document that is not one. A view
         * that is not valid returns false and reads as an empty document. Resets exactly as Load(const JsonTape&) does.
         */
        bool Load(JsonView root);

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

        bool BeginObject(StringView name) override;
        void EndObject() override;

        bool BeginArray(StringView name, StringView elementName, size_t& count) override;
        void EndArray() override;
        bool BeginArrayElement() override;
        void EndArrayElement() override;

        /// Always on: JSON has types, so there is no lenient reading to keep
        void SetStrictValues(bool /*strict*/) override {}
        bool IsStrictValues() const override {
            return true;
        }

        /// With diagnostics on, an object scope that ends holding a key nothing asked for reports "Unknown key 'x'", with "did you mean 'y'?"
        /// when a key asked in that scope is near. Asked is what counts, not read: Read, ReadAttribute, BeginObject, BeginArray, HasKey and
        /// NoteKnownKey name a key, so a value that fails to read is no unknown key. A scope is checked once, when it ends or, for the root,
        /// at FinishLoad; an array scope has no members of its own. Set the mode before the load.
        bool HasKey(StringView name) const override;
        /// A member that is neither an object nor an array: BeginObject refuses it, a loader of both forms reads it as a plain value
        bool IsScalarMember(StringView name) const override;
        void NoteKnownKey(StringView name) override;
        void FinishLoad() override;
        void SkipScope() override;

        bool HasErrors() const override;
        const Vector<DeserializeError>& GetErrors() const override;
        void ClearErrors() override;

    protected:
        void AddError(StringView path, StringView message, int32_t line) override;

    private:
        /// An object or an array element is addressed by member names; an array holds elements; an element that is not an object
        /// is a scalar and answers the name `value`
        enum class ScopeKind : uint8_t { Object, Array, Element };

        struct Scope {
            JsonView node;
            ScopeKind kind = ScopeKind::Object;
            /// An object: the token index where the member search starts (JsonView::Find). An array: the token index of the next element
            uint32_t cursor = 0;
            /// An array: the position of the next element, for the path
            uint32_t nextOrdinal = 0;
            /// Whether the keys asked here are recorded: a scope opened while the diagnostics were off has no complete list
            bool tracking = false;
            bool checked = false;
            /// The keys asked here, views into the deserializer's arena. Empty until the first key: a list made with the default
            /// allocator would bind to the thread's scratch arena, so it is made with the arena's allocator when it is needed
            Optional<ArenaVector<StringView>> asked;
            /// The arena as it was when the scope opened, to give the scope's names back at its end; empty when the arena did not exist
            /// yet, which also means no outer scope holds a name
            Optional<LinearArena::Mark> mark;
        };

        /// The arena of the asked names, made by the first name noted: a deserializer that never runs with diagnostics holds none. Held by
        /// pointer and owned by the reader (not a scratch scope) because the reader lives across many calls of the loader
        LinearArena& GetNamesArena() const;

        /// The member the current scope has under @p name; no error, no note of the key. Only a read moves the member cursor (@p advance):
        /// a HasKey that is followed by the read of the same key must not make that read wrap around
        Optional<JsonView> FindMember(StringView name, bool advance) const;

        /// FindMember with the key noted as asked, and an error when the document's root is not an object
        Optional<JsonView> FindForRead(StringView name);

        void NoteAsked(StringView name) const;

        /// Notes the mark of the names arena in @p scope, which is about to be pushed
        void MarkScope(Scope& scope) const;
        bool IsNestedTooDeep(StringView name);

        /// Reports `Invalid <kind> value '<text>'` under @p name; the text is the value cut and cleaned of control characters
        void RejectValue(StringView name, StringView kind, JsonView value, bool outOfRange);

        template <typename T>
        bool ReadInteger(StringView name, T& outValue);

        template <typename T>
        bool ReadFloating(StringView name, T& outValue);

        bool ReadBool(StringView name, bool& outValue);
        bool ReadText(StringView name, String& outValue);
        bool ReadPoolString(StringView name, PoolString& outValue);

        /// Starts a document: closes every scope and forgets the errors, the findings and the asked names, then opens the root
        void Reset(const JsonTape& tape, JsonView rootView);

        /// Checks the innermost scope, pops it and gives its names back
        void CloseScope();
        void CheckScope(Scope& scope);
        bool FindingsFull() const;

        mutable Vector<Scope> _scopes;
        mutable UniquePtr<LinearArena> _namesArena;
        Vector<DeserializeError> _errors;
        JsonTape _ownTape;
        const JsonTape* _tape = &_ownTape;
        /// The text of the Json constructor, which the tape borrows
        String _bridgeText;
        /// A string that has escapes is decoded here before it is quoted, interned or compared; the capacity is kept between reads
        String _decoded;
    };

}  // namespace BECore
