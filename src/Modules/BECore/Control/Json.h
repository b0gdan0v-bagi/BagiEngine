#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonFwd.h>
#include <nlohmann/json.hpp>

namespace BECore {

    /**
     * @brief Checked access to JSON that comes from outside the process.
     *
     * The engine builds without exceptions, so nlohmann/json aborts the process where it would throw: at(), get<T>()
     * on the wrong type, value() on a non-object and operator[] by a missing key on a const object. Never call those
     * on client data. Parse with ParseJson() and read members with the helpers below, which check the type first.
     * Building a response (Json::object(), assigning members, dump) is safe.
     */

    /// Parses @p text. Never throws or aborts: text that is not one complete JSON value is a kParseError.
    [[nodiscard]] std::expected<Json, ControlError> ParseJson(eastl::string_view text);

    /// Compact JSON text of @p value. Invalid UTF-8 in a string is replaced, not fatal.
    [[nodiscard]] eastl::string DumpJson(const Json& value);

    /// A JSON string value holding @p text
    [[nodiscard]] Json MakeJsonString(eastl::string_view text);

    /// The member @p key of @p object, or nullptr when @p object is not an object or has no such member
    [[nodiscard]] const Json* FindJsonMember(const Json& object, eastl::string_view key);

    /// A string member; nullopt when it is absent or not a string
    [[nodiscard]] std::optional<eastl::string> GetJsonString(const Json& object, eastl::string_view key);

    /// An integer member; nullopt when it is absent, not an integer (1.5 and "3" are not) or does not fit int64_t
    [[nodiscard]] std::optional<int64_t> GetJsonInt(const Json& object, eastl::string_view key);

    /// A number member (integer or floating point); nullopt when it is absent or not a number
    [[nodiscard]] std::optional<double> GetJsonNumber(const Json& object, eastl::string_view key);

    /// A boolean member; nullopt when it is absent or not a boolean
    [[nodiscard]] std::optional<bool> GetJsonBool(const Json& object, eastl::string_view key);

}  // namespace BECore
