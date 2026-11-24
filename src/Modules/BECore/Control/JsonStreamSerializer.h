#pragma once

#include <BECore/Control/JsonWriter.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore {

    CORE_ENUM(JsonStreamScopeKind, uint8_t, Object, Array, Element)

    /// What the serializer does with the value of a member write: drop it, hold it back (the lone `value` of an element) or write it
    CORE_ENUM(JsonStreamPlace, uint8_t, Skip, Pending, Write)

    /**
     * @brief Writes what `Ser::Save` and the generated `Serialize` give straight into a `JsonWriter`
     *
     * The shapes are the ones of the reflection pair (Reflection/CLAUDE.md, "JSON shapes of the reflection pair"): Write and WriteAttribute are the same
     * member, BeginObject a member object, BeginArray an array member whose elements are objects, and an element that holds only a scalar `value`
     * is that scalar. The document is one object written into the place the writer is at; Finish() closes it.
     *
     * Two things follow from a stream that cannot go back:
     * - the members of an object come out in the order they were written, not sorted by name;
     * - a name written twice in one object is written once: the first write stays (a repeated BeginObject / BeginArray answers false), except that
     *   a repeated scalar `value` of an element that is still held back replaces the held one.
     *
     * A call that does not fit the open scope (an End with nothing open, a member written into an array, a Begin past the writer's depth) is ignored
     * or answers false, never an abort: the data comes from whatever a reflected method returns. A write that the writer refuses is the writer's
     * own recorded failure.
     */
    class JsonStreamSerializer final : public ISerializer {
    public:
        explicit JsonStreamSerializer(JsonWriter& writer);
        ~JsonStreamSerializer() override;

        JsonStreamSerializer(const JsonStreamSerializer&) = delete;
        JsonStreamSerializer& operator=(const JsonStreamSerializer&) = delete;
        JsonStreamSerializer(JsonStreamSerializer&&) = delete;
        JsonStreamSerializer& operator=(JsonStreamSerializer&&) = delete;

        /// Starts a new document in @p writer; the buffers of the last one are kept, so a message of a shape seen before allocates nothing
        void Restart(JsonWriter& writer);

        /// Closes what is still open and the document's object; later calls are ignored until Restart
        void Finish();

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
        /// One name written in an open object, as a span of `_nameText`
        struct NameSpan {
            uint32_t offset = 0;
            uint32_t length = 0;
        };

        struct Scope {
            JsonStreamScopeKind kind = JsonStreamScopeKind::Object;
            size_t namesBegin = 0;  // the names of this scope are `_names[namesBegin...]`
            size_t textBegin = 0;
            bool isOpen = true;       // an element's object is opened on the first member that is not a lone `value`
            bool hasPending = false;  // `_pending` holds the element's first `value`
        };

        static constexpr size_t kInlineScopes = 16;
        static constexpr size_t kInlineNames = 32;

        /// Decides where the member @p name goes and records it: Write has its key written, Pending holds the value back, Skip drops it
        [[nodiscard]] JsonStreamPlace Prepare(StringView name, bool isScalar);
        [[nodiscard]] bool FindName(const Scope& scope, StringView name) const;
        void RecordName(StringView name);
        void OpenElement(Scope& scope);
        /// Whether a container (and the element around it, not opened yet) still fits under the writer's depth
        [[nodiscard]] bool CanOpenContainer() const;
        void PopScope();

        template <typename T>
        void PutScalar(StringView name, const T& value);

        JsonWriter* _writer = nullptr;
        SmallVector<Scope, kInlineScopes> _scopes;
        SmallVector<NameSpan, kInlineNames> _names;
        String _nameText;
        String _pending;
    };

}  // namespace BECore
