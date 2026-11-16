#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonFwd.h>
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

    /// Compact JSON text of @p value. Invalid UTF-8 in a string is replaced, not fatal.
    [[nodiscard]] String DumpJson(const Json& value);

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
