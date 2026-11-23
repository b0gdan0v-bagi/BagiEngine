#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonFwd.h>
#include <BECore/Types/Types.h>

namespace BECore {

    class JsonWriter;

    /**
     * @brief One JSON value inside a reflected answer: the place where a document the object does not own the shape of sits between its keys.
     *
     * A `BE_CLASS` answer holds a `JsonPayload` (or an `Optional` of one) where a foreign value goes: the client's `id`, a game's state, the
     * result of a handler. The generated `WriteJson` writes it at the sorted place of its key, byte for byte what `DumpJson` of the same
     * document gives, without building a `Json` for it. It is native to the generated writer (`WriteJson` below), like the `Math` leaves.
     *
     * Four forms, made by the named factories; choose by who holds the document:
     * - `Borrowed(const Json&)`: a document the handler already holds. A view: the struct is a local of the handler and never outlives it.
     * - `Streamed(writer)`: a document a callback writes straight into the writer (a game's `WriteState`), no `Json` tree. A view of the callable.
     * - `Text(ready)`: ready JSON text of exactly one value, copied as it is. A view of the text.
     * - `Owned(Json)`: the struct outlives its source. The only form that allocates, and only when it is made.
     *
     * The views are why a temporary is refused: `Borrowed` of a temporary `Json` and `Streamed` of a temporary lambda do not compile (the callable
     * must be a named local), because both would dangle once the full expression ends.
     *
     * An empty payload (default-made, or moved from) is a bug of the engine: it writes `null` and is an `EXPECT`. A member that may be missing is
     * an `Optional<JsonPayload>`. A `Streamed` callback that returns an error invalidates the writer (JsonWriter::Fail): the text of a half-written
     * value is never sent.
     *
     * **No form outside the JSON wire.** `Serialize` and `Deserialize` exist only because the generated code of a struct calls them, and they do
     * nothing: a `Json` has no stable form in XML or binary and nobody reads one back. A struct that holds a payload is an answer for `WriteJson`,
     * never a saved type and never the result of a `BE_FUNCTION`. The reflection sees an opaque type (`DescribeType`); the schema says `any`.
     */
    class JsonPayload {
    public:
        JsonPayload() = default;

        /// @p value moves in; the document lives as long as the payload
        [[nodiscard]] static JsonPayload Owned(Json value);

        /// A view of @p value, which must outlive every write of the payload
        [[nodiscard]] static JsonPayload Borrowed(const Json& value);
        static JsonPayload Borrowed(const Json&&) = delete;

        /// A view of @p ready, the text of exactly one JSON value, which must outlive every write of the payload
        [[nodiscard]] static JsonPayload Text(StringView ready);

        /// A view of @p write, which must outlive every write of the payload and writes exactly one value into the writer it gets
        template <typename TCallable>
        [[nodiscard]] static JsonPayload Streamed(TCallable& write) {
            return JsonPayload(Form{WriteResultFn(write)});
        }
        template <typename TCallable>
        static JsonPayload Streamed(const TCallable&&) = delete;

        /// Nothing is held (default-made or moved from)
        [[nodiscard]] bool IsEmpty() const {
            return std::holds_alternative<std::monostate>(_form);
        }

        /// Writes the value as the next value of @p writer. An empty payload writes `null`.
        void WriteJson(JsonWriter& writer) const;

        template <typename TSerializer>
        void Serialize(TSerializer& /*serializer*/) const {}

        template <typename TDeserializer>
        void Deserialize(TDeserializer& /*deserializer*/) {}

        /// The property visitors (editor, paths) see no member: the document is not a reflected value
        template <typename TVisitor>
        bool VisitProperties(TVisitor& /*visitor*/) {
            return false;
        }

    private:
        /// Deletes through the complete `Json`, so this header needs only the forward declaration
        struct JsonDeleter {
            void operator()(Json* value) const;
        };

        using Form = Variant<std::monostate, UniquePtr<Json, JsonDeleter>, const Json*, StringView, WriteResultFn>;

        explicit JsonPayload(Form form) : _form(std::move(form)) {}

        Form _form;
    };

}  // namespace BECore
