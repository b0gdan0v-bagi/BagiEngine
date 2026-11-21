#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonFwd.h>
#include <BECore/Control/JsonWriter.h>
#include <expected>
#include <nlohmann/json.hpp>

namespace BECore {

    /**
     * @brief Checked access to JSON that comes from outside the process.
     *
     * The engine builds without exceptions, so nlohmann/json aborts the process where it would throw: at(), get<T>()
     * on the wrong type, value() on a non-object and operator[] by a missing key on a const object. Never call those
     * on client data: parse with ParseJson() and read members with the helpers below (Control/CLAUDE.md, "JSON rules").
     */

    /// True when `[` and `{` outside strings nest deeper than @p maxDepth. A scan that does not recurse and does not judge the syntax (the parser does).
    /// ParseJson itself accepts any depth, but copying, comparing and dumping the value recurse once per level and can overflow a reader thread's stack,
    /// so a reader of outside text that keeps or copies the value checks the depth first
    [[nodiscard]] bool IsJsonNestedDeeperThan(StringView text, size_t maxDepth);

    /// Parses @p text. Never throws or aborts: text that is not one complete JSON value is a kParseError.
    [[nodiscard]] std::expected<Json, ControlError> ParseJson(StringView text);

    /// Writes @p value through @p writer: the object members in the map's order (alphabetical, as `dump()` writes them), numbers, strings and
    /// the replacement of invalid UTF-8 as JsonWriter does. It recurses once per level like `dump()`; a container that would make the writer deeper than
    /// JsonWriter::kMaxDepth (the containers its caller already opened count) is written as `null`, never a crash (client JSON is held to ControlService::kMaxRequestDepth by its readers).
    void WriteJson(JsonWriter& writer, const Json& value);

    /// Appends the JSON text of @p value to @p out, which is never cleared: compact for JsonWriter::kCompact, else indented like `dump(indent)`.
    /// The same bytes as `dump(indent, ' ', false, error_handler_t::replace)`; invalid UTF-8 in a string is replaced, not fatal. Allocates only
    /// for the growth of @p out.
    void AppendJson(String& out, const Json& value, int indent = JsonWriter::kCompact);

    /// The JSON text of @p value (see AppendJson)
    [[nodiscard]] String DumpJson(const Json& value, int indent = JsonWriter::kCompact);

    /// A JSON string value holding @p text
    [[nodiscard]] Json MakeJsonString(StringView text);

    /// The member @p key of @p object, or nullptr when @p object is not an object or has no such member
    [[nodiscard]] const Json* FindJsonMember(const Json& object, StringView key);

    /// A string member; nullopt when it is absent or not a string
    [[nodiscard]] Optional<String> GetJsonString(const Json& object, StringView key);

    /// An integer member; nullopt when it is absent, not an integer (1.5 and "3" are not) or does not fit int64_t
    [[nodiscard]] Optional<int64_t> GetJsonInt(const Json& object, StringView key);

    /// A number member (integer or floating point); nullopt when it is absent or not a number
    [[nodiscard]] Optional<double> GetJsonNumber(const Json& object, StringView key);

    /// A boolean member; nullopt when it is absent or not a boolean
    [[nodiscard]] Optional<bool> GetJsonBool(const Json& object, StringView key);

    /**
     * @brief The value that the JSON pointer @p pointer (RFC 6901) names in @p document.
     *
     * Answers the value, nullptr when the path leads nowhere (a missing key, an index past the end, a step through a scalar), or an invalid_params error
     * when the text is not a pointer: not empty and not starting with `/`, a `~` not followed by `0` or `1`, `-` or an index with a leading zero or a
     * non-digit where @p document has an array. Never aborts, whatever the text; the pointer is client data, so build no `json_pointer` from it.
     * The returned pointer is into @p document.
     */
    [[nodiscard]] std::expected<const Json*, ControlError> FindJsonPointer(const Json& document, StringView pointer);

}  // namespace BECore
