#pragma once

#include <BECore/Control/Json.h>
#include <BECore/Types/Types.h>

namespace BECore::Tests {

    /// The text of one request of the control API, built through Json so the test data is always valid; a null @p params is left out
    inline String MakeRequest(int64_t id, StringView method, const Json& params = Json()) {
        Json request = Json::object();
        request["id"] = id;
        request["method"] = ToStdString(method);
        if (!params.is_null()) {
            request["params"] = params;
        }
        return DumpJson(request);
    }

    // The readers below go through the checked accessors of Json.h: nlohmann's throwing ones would abort the whole test process on a bad answer

    inline bool IsOk(const Json& response) {
        return GetJsonBool(response, "ok").value_or(false);
    }

    /// The `id` of a response; -1 when it has none
    inline int64_t IdOf(const Json& response) {
        return GetJsonInt(response, "id").value_or(-1);
    }

    /// The `code` of a failed response; empty for a response with no error
    inline String ErrorCode(const Json& response) {
        const Json* error = FindJsonMember(response, "error");
        return error != nullptr ? GetJsonString(*error, "code").value_or(String()) : String();
    }

    /// The `message` of a failed response; empty for a response with no error
    inline String ErrorMessage(const Json& response) {
        const Json* error = FindJsonMember(response, "error");
        return error != nullptr ? GetJsonString(*error, "message").value_or(String()) : String();
    }

    /// The string member @p key of @p object; empty when there is no object, no such member or it is not a string
    inline String StringOf(const Json* object, StringView key) {
        return object != nullptr ? GetJsonString(*object, key).value_or(String()) : String();
    }

    /// The number member @p key of @p object; -1 when there is no object, no such member or it is not a number
    inline double NumberOf(const Json* object, StringView key) {
        return object != nullptr ? GetJsonNumber(*object, key).value_or(-1.0) : -1.0;
    }

    inline Optional<bool> BoolOf(const Json* object, StringView key) {
        return object != nullptr ? GetJsonBool(*object, key) : Optional<bool>();
    }

}  // namespace BECore::Tests
