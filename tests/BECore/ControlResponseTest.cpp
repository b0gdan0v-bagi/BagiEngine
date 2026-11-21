// The text of a control response and of a pushed event (ControlCall.h, ControlService::Publish): the bytes the wire carries, the reply buffer of the service and
// the writer form of ControlCall::Reply. The handlers' own answers are pinned where the handler is tested.

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        /// What the DOM path wrote for an ok response before the writer: the reference the new text has to equal
        String DomOkResponse(const Json& id, const Json& result) {
            Json response = Json::object();
            response["id"] = id;
            response["ok"] = true;
            response["result"] = result;
            return DumpJson(response);
        }

        String DomErrorResponse(const Json& id, const ControlError& error) {
            Json details = Json::object();
            details["code"] = MakeJsonString(error.code);
            details["message"] = MakeJsonString(error.message);
            Json response = Json::object();
            response["id"] = id;
            response["ok"] = false;
            response["error"] = std::move(details);
            return DumpJson(response);
        }

        Json ObjectResult() {
            Json result = Json::object();
            result["name"] = MakeJsonString("a \"quoted\"\n\xC3\xA9 \xFF text");
            result["count"] = 3;
            result["big"] = std::numeric_limits<uint64_t>::max();
            result["ratio"] = 0.1;
            result["flag"] = true;
            result["none"] = nullptr;
            result["empty"] = Json::object();
            result["list"] = Json::array({1, 2, Json::array(), MakeJsonString("x")});
            return result;
        }

        class ControlResponseTest : public ::testing::Test {
        protected:
            ControlReplyFn Raw() {
                return [this](const String& text) { texts.emplace_back(text.c_str(), text.size()); };
            }

            ControlService service;
            Vector<String> texts;
        };

    }  // namespace

    TEST_F(ControlResponseTest, OkResponseBytesAreTheDomPathsForEveryKindOfIdAndResult) {
        const Vector<Json> ids = {Json(7), MakeJsonString("req-\"1\""), Json()};
        const Vector<Json> results = {ObjectResult(), Json::array({1, MakeJsonString("two"), Json::object()}), Json(42), MakeJsonString("text"), Json(), Json(false)};

        for (const Json& id : ids) {
            for (const Json& result : results) {
                EXPECT_EQ(MakeOkResponse(id, result), DomOkResponse(id, result));
            }
        }
    }

    TEST_F(ControlResponseTest, OkResponseHasItsKeysInAlphabeticalOrder) {
        EXPECT_EQ(MakeOkResponse(Json(1), Json::object()), R"({"id":1,"ok":true,"result":{}})");

        Json result = Json::object();
        result["a"] = 1;
        EXPECT_EQ(MakeOkResponse(MakeJsonString("x"), result), R"({"id":"x","ok":true,"result":{"a":1}})");
    }

    TEST_F(ControlResponseTest, AResultNestedPastTheWritersLimitIsCutNotAMisuse) {
        Json deep = Json::array();
        for (size_t level = 0; level < JsonWriter::kMaxDepth + 10; ++level) {
            deep = Json::array({std::move(deep)});
        }

        const String text = MakeOkResponse(Json(1), std::move(deep));  // the envelope's own object counts as a level; an EXPECT of the writer would fail the case

        const auto parsed = ParseJson(text);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_TRUE(IsOk(*parsed));
    }

    TEST_F(ControlResponseTest, ErrorResponseBytesAreTheDomPathsAndThePinnedText) {
        const ControlError error = MakeControlError(ControlErrors::kInvalidParams, "bad \"x\"\n\xFF");

        EXPECT_EQ(MakeErrorResponse(Json(5), error), DomErrorResponse(Json(5), error));
        EXPECT_EQ(MakeErrorResponse(Json(), MakeControlError("c", "m")), R"({"error":{"code":"c","message":"m"},"id":null,"ok":false})");
    }

    TEST_F(ControlResponseTest, AppendingKeepsWhatIsAlreadyInTheString) {
        String out("prefix ");
        AppendOkResponse(out, Json(1), [](JsonWriter& writer) { writer.Value(true); });
        AppendErrorResponse(out, Json(2), MakeControlError("c", "m"));

        EXPECT_EQ(out, String(R"(prefix {"id":1,"ok":true,"result":true}{"error":{"code":"c","message":"m"},"id":2,"ok":false})"));
    }

    TEST_F(ControlResponseTest, ASecondResponseIntoAStringThatKeptItsCapacityAllocatesNothing) {
        String out;
        const auto write = [&out] {
            out.clear();
            AppendOkResponse(out, Json(7), [](JsonWriter& writer) {
                writer.BeginObject();
                writer.Member("frame", 12);
                writer.Member("paused", false);
                writer.Member("time", 0.25);
                writer.EndObject();
            });
        };
        write();
        const char* const data = out.data();
        const size_t capacity = out.capacity();

        ExpectNoAllocations(write);

        EXPECT_EQ(out.data(), data);
        EXPECT_EQ(out.capacity(), capacity);
    }

    TEST_F(ControlResponseTest, ReplyWithAWriterIsTheSameTextAsReplyWithTheSameJson) {
        service.RegisterHandler("viaJson", [](ControlCall& call) { call.Reply(ObjectResult()); });
        service.RegisterHandler("viaWriter", [](ControlCall& call) { call.Reply([](JsonWriter& writer) { WriteJson(writer, ObjectResult()); }); });

        service.Post(MakeRequest(4, "viaJson"), Raw());
        service.Post(MakeRequest(4, "viaWriter"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 2u);
        EXPECT_EQ(texts[0], texts[1]);
        EXPECT_EQ(texts[0], DomOkResponse(Json(4), ObjectResult()));
    }

    TEST_F(ControlResponseTest, ReplyWithAWriterWritesAScalarAndAnArray) {
        service.RegisterHandler("scalar", [](ControlCall& call) { call.Reply([](JsonWriter& writer) { writer.Value(3); }); });
        service.RegisterHandler("array", [](ControlCall& call) {
            call.Reply([](JsonWriter& writer) {
                writer.BeginArray();
                writer.Value("a");
                writer.Null();
                writer.EndArray();
            });
        });

        service.Post(MakeRequest(1, "scalar"), Raw());
        service.Post(MakeRequest(2, "array"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 2u);
        EXPECT_EQ(texts[0], String(R"({"id":1,"ok":true,"result":3})"));
        EXPECT_EQ(texts[1], String(R"({"id":2,"ok":true,"result":["a",null]})"));
    }

    TEST_F(ControlResponseTest, ANestedCallThatRepliesWithAWriterGivesItsAnswerAsAValue) {
        service.RegisterHandler("inner", [](ControlCall& call) {
            call.Reply([](JsonWriter& writer) {
                writer.BeginObject();
                writer.Member("n", 5);
                writer.Member("s", "t");
                writer.EndObject();
            });
        });
        service.RegisterHandler("outer", [this](ControlCall& call) {
            auto answer = service.RunNested(call, "inner", Json::object(), [](ControlCall&, ControlAnswer) {});
            ASSERT_TRUE(answer.has_value());
            ASSERT_TRUE(answer->has_value());
            call.Reply(std::move(**answer));
        });

        service.Post(MakeRequest(1, "outer"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 1u);
        EXPECT_EQ(texts[0], String(R"({"id":1,"ok":true,"result":{"n":5,"s":"t"}})"));
    }

    TEST_F(ControlResponseTest, TheErrorOfAFailedCallIsWrittenWithTheDomPathsBytes) {
        service.RegisterHandler("fail", [](ControlCall& call) { call.Fail(ControlErrors::kInvalidParams, "no \"x\""); });

        service.Post(MakeRequest(8, "fail"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 1u);
        EXPECT_EQ(texts[0], DomErrorResponse(Json(8), MakeControlError(ControlErrors::kInvalidParams, "no \"x\"")));
    }

    TEST_F(ControlResponseTest, PushedEventHasItsKeysInAlphabeticalOrder) {
        service.SetEventSink([this](const String& text) { texts.emplace_back(text.c_str(), text.size()); });

        Json data = Json::object();
        data["version"] = 3;
        service.Publish("state \"x\"", data);
        service.Publish("e", Json());

        ASSERT_EQ(texts.size(), 2u);
        EXPECT_EQ(texts[0], String(R"({"data":{"version":3},"event":"state \"x\""})"));
        EXPECT_EQ(texts[1], String(R"({"data":null,"event":"e"})"));
    }

    TEST_F(ControlResponseTest, AReplyFromInsideAReplyFunctionLeavesTheOuterTextIntact) {
        // "wait" is deferred, so EndFrame() answers it from inside the reply function of "quick" (which bypasses the barrier): two replies in flight at once
        service.RegisterHandler("wait", [](ControlCall& call) { call.DeferFrames(1, [](ControlCall& done) { done.Reply([](JsonWriter& writer) { writer.Value("inner"); }); }); });
        String outerBefore;
        String outerAfter;
        service.RegisterHandler("quick", [](ControlCall& call) { call.Reply([](JsonWriter& writer) { writer.Value("outer"); }); }, true);

        service.Post(MakeRequest(1, "wait"), Raw());
        service.Drain();
        ASSERT_TRUE(texts.empty());

        service.Post(MakeRequest(2, "quick"), [&](const String& text) {
            outerBefore = text;
            service.EndFrame();
            outerAfter = text;
        });
        service.Drain();

        EXPECT_EQ(outerBefore, String(R"({"id":2,"ok":true,"result":"outer"})"));
        EXPECT_EQ(outerAfter, outerBefore);
        ASSERT_EQ(texts.size(), 1u);
        EXPECT_EQ(texts[0], String(R"({"id":1,"ok":true,"result":"inner"})"));
    }

    TEST_F(ControlResponseTest, AReplyFromAnotherThreadAfterShutdownArrivesIntact) {
        service.Shutdown();
        String text;

        std::thread poster([&] { service.Post(MakeRequest(6, "late"), [&text](const String& reply) { text = reply; }); });
        poster.join();

        EXPECT_EQ(text, String(R"({"error":{"code":"shutting_down","message":"the application is shutting down"},"id":6,"ok":false})"));
    }

}  // namespace BECore::Tests
