#pragma once

#include <BECore/Control/JsonFwd.h>
#include <BECore/Reflection/ISerializer.h>

namespace BECore {

    /**
     * @brief Writes what `Ser::Save` and the generated `Serialize` give into a JSON document
     *
     * The interim DOM-backed writer of the control layer: the document is built as a `Json` and taken with Take(). The shapes
     * (Reflection/CLAUDE.md, "JSON shapes of the reflection pair"): Write and WriteAttribute are the same member, BeginObject a member
     * object, BeginArray an array member whose elements are objects, and an element that holds only a scalar `value` is that scalar.
     * A call that does not fit the open scope (an End with nothing open, a member written into an array) is ignored, never an abort.
     */
    class JsonSerializer final : public ISerializer {
    public:
        JsonSerializer();
        ~JsonSerializer() override;

        JsonSerializer(const JsonSerializer&) = delete;
        JsonSerializer& operator=(const JsonSerializer&) = delete;
        JsonSerializer(JsonSerializer&&) = delete;
        JsonSerializer& operator=(JsonSerializer&&) = delete;

        /// The root object written so far; the serializer starts a new empty one, with nothing open
        [[nodiscard]] Json Take();

        void Write(StringView name, bool value) override;
        void Write(StringView name, int8_t value) override;
        void Write(StringView name, uint8_t value) override;
        void Write(StringView name, int16_t value) override;
        void Write(StringView name, uint16_t value) override;
        void Write(StringView name, int32_t value) override;
        void Write(StringView name, uint32_t value) override;
        void Write(StringView name, int64_t value) override;
        void Write(StringView name, uint64_t value) override;
        void Write(StringView name, float value) override;
        void Write(StringView name, double value) override;
        void Write(StringView name, StringView value) override;
        void Write(StringView name, const PoolString& value) override;

        void WriteAttribute(StringView name, bool value) override;
        void WriteAttribute(StringView name, int8_t value) override;
        void WriteAttribute(StringView name, uint8_t value) override;
        void WriteAttribute(StringView name, int16_t value) override;
        void WriteAttribute(StringView name, uint16_t value) override;
        void WriteAttribute(StringView name, int32_t value) override;
        void WriteAttribute(StringView name, uint32_t value) override;
        void WriteAttribute(StringView name, int64_t value) override;
        void WriteAttribute(StringView name, uint64_t value) override;
        void WriteAttribute(StringView name, float value) override;
        void WriteAttribute(StringView name, double value) override;
        void WriteAttribute(StringView name, StringView value) override;
        void WriteAttribute(StringView name, const PoolString& value) override;

        bool BeginObject(StringView name) override;
        void EndObject() override;

        bool BeginArray(StringView name, StringView elementName, size_t& count) override;
        void EndArray() override;
        bool BeginArrayElement() override;
        void EndArrayElement() override;

    private:
        enum class ScopeKind : uint8_t { Object, Array, Element };

        /// What is open: a pointer into the document, stable because only the innermost scope ever grows
        struct Scope {
            Json* node = nullptr;
            ScopeKind kind = ScopeKind::Object;
        };

        void Reset();

        /// The member @p name of the open object or element, set to @p value; nothing when an array is open
        void Put(StringView name, Json&& value);

        UniquePtr<Json> _root;
        Vector<Scope> _scopes;
    };

}  // namespace BECore
