#pragma once

#include <BECore/Control/JsonFwd.h>
#include <BECore/Reflection/IDeserializer.h>

namespace BECore {

    /**
     * @brief Reads a JSON document into what `Ser::Load` and the generated `Deserialize` ask for
     *
     * The interim DOM-backed reader of the control layer: it walks a `Json` the caller keeps alive and never copies it. Values are always
     * strict: a bool is a JSON bool, a number a JSON number of the target's kind and range, a string a JSON string, and a refusal is an
     * error under the field's path that leaves the target as it was. The shapes it reads are those of JsonSerializer
     * (Reflection/CLAUDE.md, "JSON shapes of the reflection pair"). Input from outside: check IsJsonNestedDeeperThan before keeping the
     * document, as the control layer does; this class only counts open scopes against ReaderLimits::kMaxXmlNesting.
     */
    class JsonDeserializer final : public IDeserializer {
    public:
        /// @p document must outlive the reader; its root is the first scope (a root that is not an object makes every read an error)
        explicit JsonDeserializer(const Json& document);
        ~JsonDeserializer() override;

        JsonDeserializer(const JsonDeserializer&) = delete;
        JsonDeserializer& operator=(const JsonDeserializer&) = delete;
        JsonDeserializer(JsonDeserializer&&) = delete;
        JsonDeserializer& operator=(JsonDeserializer&&) = delete;

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
            const Json* node = nullptr;
            ScopeKind kind = ScopeKind::Object;
            size_t nextElement = 0;
            /// Whether the keys asked here are recorded: a scope opened while the diagnostics were off has no complete list
            bool tracking = false;
            bool checked = false;
            Vector<String> asked;
        };

        /// The member the current scope has under @p name, or null; no error, no note of the key
        const Json* FindMember(StringView name) const;

        /// FindMember with the key noted as asked, and an error when the document's root is not an object
        const Json* FindForRead(StringView name);

        void NoteAsked(StringView name) const;
        bool IsNestedTooDeep(StringView name);

        /// Reports `Invalid <kind> value '<text>'` under @p name; the text is the value cut and cleaned of control characters
        void RejectValue(StringView name, StringView kind, const Json& value, bool outOfRange);

        template <typename T>
        bool ReadInteger(StringView name, T& outValue);

        template <typename T>
        bool ReadFloating(StringView name, T& outValue);

        bool ReadBool(StringView name, bool& outValue);
        bool ReadText(StringView name, String& outValue);

        void CloseScope();
        void CheckScope(Scope& scope);
        bool FindingsFull() const;

        mutable Vector<Scope> _scopes;
        Vector<DeserializeError> _errors;
    };

}  // namespace BECore
