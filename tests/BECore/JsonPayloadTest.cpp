// The reflected leaf JsonPayload (Control/JsonPayload.h): a generated `WriteJson` writes it as one value at the sorted place of its key, byte for byte
// the text the DOM serializer gave for the same document (the literals below were printed from it).

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonPayload.h>
#include <BECore/Control/JsonStreamSerializer.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriteReflect.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/JsonPayloadTypes.h>
#include <Common/AllocCheck.h>
#include <Common/AssertBridge.h>
#include <Common/ControlTestSupport.h>
#include <Generated/JsonPayloadTypes.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using JsonPayloadTestData::PayloadBase;
        using JsonPayloadTestData::PayloadBetween;
        using JsonPayloadTestData::PayloadDerived;
        using JsonPayloadTestData::PayloadOptional;
        using JsonPayloadTestData::PayloadRow;
        using JsonPayloadTestData::PayloadRows;

        constexpr Array<int, 2> kIndents = {JsonWriter::kCompact, 2};

        /// A document with nested containers and keys out of order, and the same document with its keys in byte order, as the DOM dumped it
        constexpr StringView kDocumentText = R"({"b":[1,2.5,"x"],"a":{"z":null,"y":true},"c":"text"})";
        constexpr StringView kSortedDocumentText = R"({"a":{"y":true,"z":null},"b":[1,2.5,"x"],"c":"text"})";

        template <typename T>
        String Written(const T& object, int indent = JsonWriter::kCompact) {
            String out;
            JsonWriter writer(out, indent);
            object.WriteJson(writer);
            EXPECT_TRUE(writer.IsComplete());
            return out;
        }

        /// The writing form's callback: one object, the way a game's `WriteState` does
        std::expected<void, ControlError> WriteSmallObject(JsonWriter& writer) {
            writer.BeginObject();
            writer.Member("n", 1);
            writer.Member("on", true);
            writer.EndObject();
            return {};
        }

        /// What a writing callback of a game that cannot write its document returns
        std::expected<void, ControlError> WriteFailing(JsonWriter& writer) {
            writer.BeginArray();  // half a value, as a failing callback leaves it
            return std::unexpected(MakeControlError("write_failed", "the state cannot be written"));
        }

        const ControlError kFailure = MakeControlError("write_failed", "the state cannot be written");

        const String kSmallObjectText = R"({"n":1,"on":true})";

    }  // namespace

    TEST(JsonPayloadTest, AStreamedDocumentIsWrittenAtTheSortedPlaceOfItsKey) {
        const JsonDoc document(kDocumentText);
        const auto write = [&document](JsonWriter& writer) -> std::expected<void, ControlError> {
            WriteJsonSorted(writer, document.Root());
            return {};
        };
        PayloadBetween answer;
        answer.alpha = "A";
        answer.zulu = "Z";
        answer.middle = JsonPayload::Streamed(write);

        EXPECT_EQ(Written(answer, JsonWriter::kCompact), R"({"alpha":"A","middle":{"a":{"y":true,"z":null},"b":[1,2.5,"x"],"c":"text"},"zulu":"Z"})");
        EXPECT_EQ(Written(answer, 2), "{\n  \"alpha\": \"A\",\n  \"middle\": {\n    \"a\": {\n      \"y\": true,\n      \"z\": null\n    },\n    \"b\": [\n      1,\n      2.5,\n      \"x\"\n    ],\n "
                                      "   \"c\": \"text\"\n  },\n  \"zulu\": \"Z\"\n}");
    }

    TEST(JsonPayloadTest, ReadyTextIsWrittenAsItIsAlsoWhenIndented) {
        const String text = R"({"k":[1,2]})";
        PayloadBetween answer;
        answer.middle = JsonPayload::Text(text);
        EXPECT_EQ(Written(answer), R"({"alpha":"","middle":{"k":[1,2]},"zulu":""})");
        EXPECT_EQ(Written(answer, 2), "{\n  \"alpha\": \"\",\n  \"middle\": {\"k\":[1,2]},\n  \"zulu\": \"\"\n}");
    }

    TEST(JsonPayloadTest, AWritingCallbackStreamsTheValue) {
        const auto write = [](JsonWriter& writer) { return WriteSmallObject(writer); };
        PayloadBetween answer;
        answer.middle = JsonPayload::Streamed(write);
        EXPECT_EQ(Written(answer), "{\"alpha\":\"\",\"middle\":" + kSmallObjectText + ",\"zulu\":\"\"}");
    }

    TEST(JsonPayloadTest, AnAbsentOptionalWritesNoKey) {
        PayloadOptional answer;
        answer.always = JsonPayload::Text(kSortedDocumentText);
        EXPECT_EQ(Written(answer), Format(R"({{"always":{},"version":0}})", kSortedDocumentText));

        const String text = "[1]";
        answer.maybe = JsonPayload::Text(text);
        EXPECT_EQ(Written(answer), Format(R"({{"always":{},"maybe":[1],"version":0}})", kSortedDocumentText));
    }

    TEST(JsonPayloadTest, AnEmptyPayloadWritesNullAndIsAnExpect) {
        size_t expects = 0;
        AssertBridge::Capture capture([&](const AssertEvent&) { ++expects; });
        PayloadBetween answer;
        String out;
        JsonWriter writer(out);
        answer.WriteJson(writer);
        EXPECT_EQ(out, R"({"alpha":"","middle":null,"zulu":""})");
        EXPECT_TRUE(JsonPayload().IsEmpty());
    }

    TEST(JsonPayloadTest, ARowOfAListAndAnOptionalRowHoldTheirPayloads) {
        const String second = "7";
        const auto write = [](JsonWriter& writer) { return WriteSmallObject(writer); };
        PayloadRows answer;
        answer.rows.resize(3);
        answer.rows[0].ok = true;
        answer.rows[0].result = JsonPayload::Text(kSortedDocumentText);
        answer.rows[1].result = JsonPayload::Text(second);
        answer.rows[2].ok = true;
        answer.rows[2].result = JsonPayload::Streamed(write);
        answer.last = PayloadRow{};
        answer.last->result = JsonPayload::Text(second);

        const String expected =
            Format(R"({{"last":{{"ok":false,"result":7}},"rows":[{{"ok":true,"result":{}}},{{"ok":false,"result":7}},{{"ok":true,"result":{}}}]}})", kSortedDocumentText, kSmallObjectText);
        EXPECT_EQ(Written(answer), expected);
    }

    TEST(JsonPayloadTest, AClassWithAParentMergesItsPayloadKeysWithTheParents) {
        const String id = "\"r1\"";
        const String data = "{\"q\":1}";
        PayloadDerived answer;
        answer.name = "n";
        answer.id = JsonPayload::Text(id);
        answer.data = JsonPayload::Text(data);
        answer.zeta = 9;
        EXPECT_EQ(Written(answer), R"({"data":{"q":1},"id":"r1","name":"n","zeta":9})");
    }

    TEST(JsonPayloadTest, ANestedValueIsCutLikeTheDocumentWriterWhenTheWriterIsDeep) {
        // The payload writes through the document writer, so the depth rule of WriteJson(writer, view) holds inside a struct
        String deepText;
        for (size_t level = 0; level < JsonWriter::kMaxDepth; ++level) {
            deepText += '[';
        }
        for (size_t level = 0; level < JsonWriter::kMaxDepth; ++level) {
            deepText += ']';
        }
        const JsonDoc deep(StringView(deepText.c_str(), deepText.size()));
        ASSERT_TRUE(deep.IsParsed());
        const auto write = [&deep](JsonWriter& writer) -> std::expected<void, ControlError> {
            WriteJson(writer, deep.Root());
            return {};
        };
        PayloadBetween answer;
        answer.middle = JsonPayload::Streamed(write);
        String out;
        JsonWriter writer(out);
        answer.WriteJson(writer);
        EXPECT_TRUE(writer.IsComplete());
    }

    TEST(JsonPayloadTest, AFailedWritingCallbackRecordsItsErrorInTheWriter) {
        const auto fail = [](JsonWriter& writer) { return WriteFailing(writer); };
        PayloadBetween answer;
        answer.middle = JsonPayload::Streamed(fail);
        String out;
        JsonWriter writer(out);
        answer.WriteJson(writer);
        EXPECT_FALSE(writer.IsValid());
        ASSERT_NE(writer.GetFailure(), nullptr);
        EXPECT_EQ(writer.GetFailure()->code, kFailure.code);
        EXPECT_EQ(writer.GetFailure()->message, kFailure.message);
    }

    TEST(JsonPayloadTest, ACallbackIsNotAskedOnceAnEarlierPayloadFailed) {
        size_t calls = 0;
        const auto count = [&calls](JsonWriter& writer) {
            ++calls;
            return WriteSmallObject(writer);
        };
        const auto fail = [](JsonWriter& writer) { return WriteFailing(writer); };
        PayloadRows answer;
        answer.rows.resize(2);
        answer.rows[0].result = JsonPayload::Streamed(fail);
        answer.rows[1].result = JsonPayload::Streamed(count);
        String out;
        JsonWriter writer(out);
        answer.WriteJson(writer);
        EXPECT_EQ(calls, 0u);
    }

    namespace {

        /// A handler that answers with @p answer through ReplyObject, posted once; the reply texts that came back
        template <typename T>
        Vector<String> AnswerOnce(ControlService& service, const T& answer) {
            Vector<String> replies;
            service.RegisterHandler("ask", [&answer](ControlCall& call) { call.ReplyObject(answer); });
            service.Post(MakeRequestText(4, "ask"), [&replies](const String& text) { replies.push_back(text); });
            service.Drain();
            return replies;
        }

        const String kFailureReply = MakeErrorResponse("4", kFailure);

    }  // namespace

    TEST(JsonPayloadTest, ReplyObjectAnswersTheErrorOfAFailedPayloadAndNothingOfWhatWasWritten) {
        const auto fail = [](JsonWriter& writer) { return WriteFailing(writer); };
        PayloadBetween answer;
        answer.alpha = "A";
        answer.middle = JsonPayload::Streamed(fail);
        ControlService service;
        const Vector<String> replies = AnswerOnce(service, answer);
        ASSERT_EQ(replies.size(), 1u);
        EXPECT_EQ(replies[0], kFailureReply);
    }

    TEST(JsonPayloadTest, TheErrorOfAPayloadInARowOrANestedStructIsTheAnswer) {
        const auto fail = [](JsonWriter& writer) { return WriteFailing(writer); };
        const String fine = "1";
        PayloadRows inRows;
        inRows.rows.resize(2);
        inRows.rows[0].result = JsonPayload::Text(fine);
        inRows.rows[1].result = JsonPayload::Streamed(fail);
        PayloadRows inOptional;
        inOptional.last = PayloadRow{};
        inOptional.last->result = JsonPayload::Streamed(fail);
        for (const PayloadRows* answer : {&inRows, &inOptional}) {
            ControlService service;
            const Vector<String> replies = AnswerOnce(service, *answer);
            ASSERT_EQ(replies.size(), 1u);
            EXPECT_EQ(replies[0], kFailureReply);
        }
    }

    TEST(JsonPayloadTest, TheFirstFailureOfTheWriterIsTheOneAnswered) {
        const auto first = [](JsonWriter& /*writer*/) -> std::expected<void, ControlError> { return std::unexpected(MakeControlError("first", "one")); };
        const auto second = [](JsonWriter& /*writer*/) -> std::expected<void, ControlError> { return std::unexpected(MakeControlError("second", "two")); };
        PayloadRows answer;
        answer.rows.resize(2);
        answer.rows[0].result = JsonPayload::Streamed(first);
        answer.rows[1].result = JsonPayload::Streamed(second);
        ControlService service;
        const Vector<String> replies = AnswerOnce(service, answer);
        ASSERT_EQ(replies.size(), 1u);
        EXPECT_EQ(replies[0], MakeErrorResponse("4", MakeControlError("first", "one")));
    }

    TEST(JsonPayloadTest, ANestedCallKeepsTheErrorOfAFailedPayload) {
        const auto fail = [](JsonWriter& writer) { return WriteFailing(writer); };
        PayloadBetween answer;
        answer.middle = JsonPayload::Streamed(fail);
        ControlService service;
        Optional<ControlAnswer> nested;
        service.RegisterHandler("inner", [&answer](ControlCall& call) { call.ReplyObject(answer); });
        service.RegisterHandler("outer", [&](ControlCall& call) {
            nested = service.RunNested(call, "inner", EmptyParamsView(), [](ControlCall&, ControlAnswer) {});
            call.ReplyEmpty();
        });
        service.Post(MakeRequestText(1, "outer"), [](const String&) {});
        service.Drain();
        ASSERT_TRUE(nested.has_value());
        ASSERT_FALSE(nested.value().has_value());
        EXPECT_EQ(nested.value().error().code, kFailure.code);
    }

    TEST(JsonPayloadTest, APushOfAFailedPayloadIsNotSentAndNoObserverIsCalled) {
        const auto fail = [](JsonWriter& writer) { return WriteFailing(writer); };
        const String fine = "1";
        PayloadBetween failing;
        failing.middle = JsonPayload::Streamed(fail);
        PayloadBetween working;
        working.middle = JsonPayload::Text(fine);

        for (const bool observed : {false, true}) {
            ControlService service;
            size_t pushes = 0;
            size_t observations = 0;
            String last;
            service.SetEventSink([&](const String& text) {
                ++pushes;
                last = text;
            });
            if (observed) {
                static_cast<void>(service.AddPublishObserver([&](StringView, JsonView) { ++observations; }));
            }

            service.Publish("event", [&failing](JsonWriter& writer) { failing.WriteJson(writer); });
            EXPECT_EQ(pushes, 0u) << "observed " << observed;
            EXPECT_EQ(observations, 0u) << "observed " << observed;

            service.Publish("event", [&working](JsonWriter& writer) { working.WriteJson(writer); });  // the scratch of the service is intact
            EXPECT_EQ(pushes, 1u) << "observed " << observed;
            EXPECT_EQ(observations, observed ? 1u : 0u) << "observed " << observed;
            EXPECT_EQ(last, R"({"data":{"alpha":"","middle":1,"zulu":""},"event":"event"})");
        }
    }

    TEST(JsonPayloadTest, TheLeafIsOpaqueToReflectionAndHasNoFormOutsideTheJsonWire) {
        static_assert(DescribeType<JsonPayload>().kind == TypeKind::Opaque);
        static_assert(DescribeType<Optional<JsonPayload>>().kind == TypeKind::Optional);
        static_assert(DescribeType<Optional<JsonPayload>>().element->kind == TypeKind::Opaque);

        // The generated pair compiles for a struct with a payload; it saves nothing of the document
        PayloadBetween answer;
        answer.middle = JsonPayload::Text(kSortedDocumentText);
        String out;
        JsonWriter writer(out);
        JsonStreamSerializer serializer(writer);
        answer.Serialize(serializer);
    }

    TEST(JsonPayloadTest, TheTextAndTheWritingFormAllocateNothingIntoAKeptString) {
        const String text = R"({"k":[1,2]})";
        const auto write = [](JsonWriter& writer) { return WriteSmallObject(writer); };
        PayloadBetween byText;
        byText.middle = JsonPayload::Text(text);
        PayloadBetween byWriter;
        byWriter.middle = JsonPayload::Streamed(write);

        String out;
        out.reserve(256);
        const auto writeBoth = [&] {
            out.clear();
            JsonWriter first(out);
            byText.WriteJson(first);
            out.clear();
            JsonWriter second(out);
            byWriter.WriteJson(second);
        };
        writeBoth();
        ExpectNoAllocations(writeBoth);
    }

    TEST(JsonPayloadTest, ASecondSuccessfulResponseIntoAKeptStringAllocatesNothing) {
        const String text = R"({"k":[1,2]})";
        PayloadBetween answer;
        answer.middle = JsonPayload::Text(text);
        const StringView id = "4";
        String out;
        const auto respond = [&] {
            out.clear();
            EXPECT_TRUE(TryAppendOkResponse(out, id, [&answer](JsonWriter& writer) -> std::expected<void, ControlError> {
                            answer.WriteJson(writer);
                            return {};
                        }).has_value());
        };
        respond();
        ExpectNoAllocations(respond);
        EXPECT_EQ(out, R"({"id":4,"ok":true,"result":{"alpha":"","middle":{"k":[1,2]},"zulu":""}})");
    }

    TEST(JsonPayloadTest, ASecondPushIntoTheKeptScratchAllocatesNothingInBothForms) {
        const JsonDoc data(R"({"n":[1,2]})");
        PayloadBetween answer;
        const String text = R"({"k":1})";
        answer.middle = JsonPayload::Text(text);
        ControlService service;
        String last;
        service.SetEventSink([&last](const String& pushed) { last = pushed; });
        const auto pushDocument = [&] { service.Publish("state_changed", [&data](JsonWriter& writer) { WriteJson(writer, data.Root()); }); };
        const auto pushWriter = [&] { service.Publish("state_changed", [&answer](JsonWriter& writer) { answer.WriteJson(writer); }); };
        pushDocument();
        pushWriter();

        ExpectNoAllocations(pushDocument);
        EXPECT_EQ(last, R"({"data":{"n":[1,2]},"event":"state_changed"})");
        ExpectNoAllocations(pushWriter);
        EXPECT_EQ(last, R"({"data":{"alpha":"","middle":{"k":1},"zulu":""},"event":"state_changed"})");
    }

}  // namespace BECore::Tests
