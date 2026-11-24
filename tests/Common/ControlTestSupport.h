#pragma once

#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Time/Time.h>
#include <BECore/Types/Types.h>
#include <Common/JsonDoc.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The text of one request of the control API; a null (empty) @p paramsText is left out, any other is written as it is: it must be a valid JSON value
    inline String MakeRequestText(int64_t id, StringView method, StringView paramsText = {}) {
        String text;
        JsonWriter writer(text);
        writer.BeginObject();
        writer.Member("id", id);
        writer.Member("method", method);
        if (!paramsText.empty()) {
            writer.Key("params");
            writer.Raw(paramsText);
        }
        writer.EndObject();
        return text;
    }

    /// The member @p key of @p object; a view that is not valid (and reads as nothing) when @p object is not an object or has no such member
    [[nodiscard]] inline JsonView MemberOf(JsonView object, StringView key) {
        const Optional<JsonView> member = object.Find(key);
        return member ? *member : JsonView();
    }

    /// The string member @p key of @p object, decoded; empty when there is no such member or it is not a string
    inline String StringOf(JsonView object, StringView key) {
        String text;
        MemberOf(object, key).ReadText(text);
        return text;
    }

    /// The number member @p key of @p object; -1 when there is no such member or it is not a number
    inline double NumberOf(JsonView object, StringView key) {
        return MemberOf(object, key).AsDouble().value_or(-1.0);
    }

    /// The integer member @p key of @p object; nullopt when there is no such member or it is not an integer
    inline Optional<int64_t> IntOf(JsonView object, StringView key) {
        return MemberOf(object, key).AsInt64();
    }

    inline Optional<bool> BoolOf(JsonView object, StringView key) {
        return MemberOf(object, key).AsBool();
    }

    /// The text of the member @p key as it stands in the document (a string with its quotes, a container whole); empty when there is none
    inline String RawOf(JsonView object, StringView key) {
        return String(MemberOf(object, key).Raw());
    }

    /// One answer of the control API with its own tape: it owns the reply text and keeps it where it is (the tape borrows it), so the object moves but its
    /// text does not. The default object is "nothing was answered": nothing is valid and every reader gives the empty value.
    class ControlReply {
    public:
        ControlReply() = default;
        explicit ControlReply(StringView text) : _doc(MakeUnique<JsonDoc>(text)) {}

        /// The whole answer; not valid when nothing came or the text is not JSON
        [[nodiscard]] JsonView View() const {
            return _doc ? _doc->Root() : JsonView();
        }

        [[nodiscard]] StringView Text() const {
            return _doc ? _doc->Text() : StringView();
        }

        [[nodiscard]] bool IsOk() const {
            return BoolOf(View(), "ok").value_or(false);
        }

        /// The `id` as written (`1`, `"abc"`, `null`); empty for an answer with none
        [[nodiscard]] String IdText() const {
            return RawOf(View(), "id");
        }

        /// The `code` of a failed answer; empty for one with no error
        [[nodiscard]] String ErrorCode() const {
            return StringOf(MemberOf(View(), "error"), "code");
        }

        [[nodiscard]] String ErrorMessage() const {
            return StringOf(MemberOf(View(), "error"), "message");
        }

        [[nodiscard]] JsonView Result() const {
            return MemberOf(View(), "result");
        }

    private:
        UniquePtr<JsonDoc> _doc;
    };

    /// Posts the request @p id of @p method with @p paramsText to @p service, runs the main-thread side and returns the answer; an empty reply when nothing was answered
    inline ControlReply CallControlText(ControlService& service, int64_t id, StringView method, StringView paramsText = {}) {
        ControlReply reply;
        service.Post(MakeRequestText(id, method, paramsText), [&reply](const String& text) { reply = ControlReply(StringView(text.c_str(), text.size())); });
        service.Drain();
        return reply;
    }

    /// The clock of `CoreManager::GetTime()` as the text of `ControlClock`: the oracle of every answer that carries it
    inline String ClockText() {
        const Time& time = CoreManager::GetTime();
        String text;
        JsonWriter writer(text);
        writer.BeginObject();
        writer.Member("deltaTime", time.GetDeltaTime());
        writer.Member("fixedDeltaTime", static_cast<double>(time.GetFixedDeltaTime()));  // the wire widens the float, so its text is the double of it
        writer.Member("frame", time.GetFrameCount());
        writer.Member("totalTime", time.GetTotalTime());
        writer.EndObject();
        return text;
    }

    /// The text the generated writer makes of a reflected @p answer
    template <typename T>
    String WrittenText(const T& answer) {
        String text;
        JsonWriter writer(text);
        answer.WriteJson(writer);
        return text;
    }

    /// Each reply text of @p actual equals the one of @p expected byte for byte: a writer's answer is pinned by the literal text it must write
    inline void ExpectTexts(const Vector<String>& actual, const Vector<StringView>& expected) {
        ASSERT_EQ(actual.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            EXPECT_TRUE(StringView(actual[i]) == expected[i]) << "reply " << i << ": " << actual[i].c_str();
        }
    }

}  // namespace BECore::Tests
