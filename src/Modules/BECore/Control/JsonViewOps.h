#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <expected>

namespace BECore {

    /**
     * @brief Whether two values of tapes are the same JSON value, as the games compare their documents.
     *
     * Objects are equal when they have the same member count and every member of @p a is found in @p b with an equal value, so the order of the
     * keys does not matter; arrays item by item; strings by their decoded text (an escape equals the character it stands for); numbers by value
     * (`1`, `1.0` and `1e0` are equal, `-0` equals `0`, a value beyond 64 bits is compared as a double). Two views that are not valid are
     * equal; one that is not valid equals no valid view.
     *
     * Of two equal keys in one object the first counts (the tape's rule, the DOM kept the last); such an object is not guaranteed to equal
     * itself, and no client document is compared with it. Allocates only to decode a string that holds an escape. Recurses once per level, at most
     * ReaderLimits::kMaxJsonNesting.
     */
    [[nodiscard]] bool JsonEquals(JsonView a, JsonView b);

    /**
     * @brief Writes @p value through @p writer as one value, in the order of its document.
     *
     * A string is written from its decoded text (an escape is normalised by the writer; the tape refused invalid UTF-8), an Integer that fits 64 bits
     * as its digits, every other number through `Value(double)` (so `1e999` is `null`), a view that is not valid as `null`. A container that would make
     * the writer deeper than JsonWriter::kMaxDepth is `null`. Allocates only to decode a string that holds an escape.
     */
    void WriteJson(JsonWriter& writer, JsonView value);

    /**
     * @brief WriteJson() with the members of every object in byte order of the decoded key: the bytes of every file the engine writes.
     *
     * For the files an editor writes and the params a person types, where the bytes are a contract. Of two equal keys the first is written. Copies
     * the keys of each object into a scratch of its own, so it is for files and typed commands, never a hot path.
     */
    void WriteJsonSorted(JsonWriter& writer, JsonView value);

    /**
     * @brief The value that the JSON pointer @p pointer (RFC 6901) names in @p document, or nullopt when the path leads nowhere.
     *
     * A missing key, an index past the end and a step through a scalar
     * are absent; text that is not a pointer is an `invalid_params` error, whatever @p document is. The empty pointer names @p document itself.
     */
    [[nodiscard]] std::expected<Optional<JsonView>, ControlError> FindJsonPointer(JsonView document, StringView pointer);

    /// The syntax of @p pointer alone, as far as a pointer can be judged with no document: empty, or starting with `/` and with every `~` escape
    /// valid. A pointer that passes can still fail on an array index of the document it is used on
    [[nodiscard]] std::expected<void, ControlError> CheckJsonPointer(StringView pointer);

}  // namespace BECore
