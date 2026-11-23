#pragma once

#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

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

    /// Posts the request @p id of @p method to @p service, runs the main-thread side and returns the parsed response; a null Json when
    /// nothing was answered or the answer did not parse
    inline Json CallControl(ControlService& service, int64_t id, StringView method, const Json& params = Json()) {
        Json response;
        service.Post(MakeRequest(id, method, params), [&response](String text) {
            auto parsed = ParseJson(text);
            response = parsed ? std::move(*parsed) : Json();
        });
        service.Drain();
        return response;
    }

    /// The text the generated writer makes of a reflected @p answer
    template <typename T>
    String WrittenText(const T& answer) {
        String text;
        JsonWriter writer(text);
        answer.WriteJson(writer);
        return text;
    }

    /// The parsed text of a reflected @p answer; a null Json when it did not parse
    template <typename T>
    Json ToJson(const T& answer) {
        auto parsed = ParseJson(WrittenText(answer));
        return parsed ? std::move(*parsed) : Json();
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

    /// Each reply text of @p actual equals the one of @p expected byte for byte: a writer's answer is pinned by the text the `Json` form made of it
    inline void ExpectTexts(const Vector<String>& actual, const Vector<StringView>& expected) {
        ASSERT_EQ(actual.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            EXPECT_TRUE(StringView(actual[i]) == expected[i]) << "reply " << i << ": " << actual[i].c_str();
        }
    }

}  // namespace BECore::Tests
