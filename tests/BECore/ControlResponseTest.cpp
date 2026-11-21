// The text of a control response and of a pushed event (ControlCall.h, ControlService::Publish): the bytes the wire carries, the reply buffer of the service and
// the writer form of ControlCall::Reply. The handlers' own answers are pinned where the handler is tested.

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <Common/AllocCheck.h>
#include <Common/AssertBridge.h>
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

    TEST_F(ControlResponseTest, TryReplyWithASuccessIsTheSameTextAsReplyWithTheSameJson) {
        service.RegisterHandler("viaJson", [](ControlCall& call) { call.Reply(ObjectResult()); });
        service.RegisterHandler("viaTry", [](ControlCall& call) {
            call.TryReply([](JsonWriter& writer) -> std::expected<void, ControlError> {
                WriteJson(writer, ObjectResult());
                return {};
            });
        });

        service.Post(MakeRequest(4, "viaJson"), Raw());
        service.Post(MakeRequest(4, "viaTry"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 2u);
        EXPECT_EQ(texts[0], texts[1]);
    }

    TEST_F(ControlResponseTest, TryReplyThatFailsAfterHalfAnObjectSendsExactlyTheErrorResponse) {
        const ControlError error = MakeControlError("game_broke", "no \"state\" \xFF");
        service.RegisterHandler("fails", [&error](ControlCall& call) {
            call.TryReply([&error](JsonWriter& writer) -> std::expected<void, ControlError> {
                writer.BeginObject();
                writer.Member("half", "written");
                writer.Key("more");
                return std::unexpected(error);
            });
        });
        service.RegisterHandler("after", [](ControlCall& call) { call.Reply([](JsonWriter& writer) { writer.Value("intact"); }); });

        service.Post(MakeRequest(3, "fails"), Raw());
        service.Post(MakeRequest(4, "after"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 2u);
        EXPECT_EQ(texts[0], MakeErrorResponse(Json(3), error));
        EXPECT_EQ(texts[1], String(R"({"id":4,"ok":true,"result":"intact"})"));
    }

    TEST_F(ControlResponseTest, AWriterThatDoesNotWriteOneValueIsAnInternalErrorForBothForms) {
        size_t misuses = 0;
        AssertBridge::Capture capture([&misuses](const AssertEvent&) { ++misuses; });  // the writer's own EXPECT of a misuse
        const String expected = MakeErrorResponse(Json(1), MakeControlError(ControlErrors::kInternalError, "the handler wrote a result that is not one JSON value"));

        const auto nothing = [](JsonWriter&) {};
        const auto two = [](JsonWriter& writer) {
            writer.Value(1);
            writer.Value(2);
        };
        const auto open = [](JsonWriter& writer) {
            writer.BeginObject();
            writer.Member("a", 1);
        };
        const Vector<FunctionRef<void(JsonWriter&)>> cases = {nothing, two, open};
        for (const FunctionRef<void(JsonWriter&)>& writeResult : cases) {
            texts.clear();
            service.RegisterHandler("plain", [&writeResult](ControlCall& call) { call.Reply(writeResult); });
            service.RegisterHandler("tried", [&writeResult](ControlCall& call) {
                call.TryReply([&writeResult](JsonWriter& writer) -> std::expected<void, ControlError> {
                    writeResult(writer);
                    return {};
                });
            });
            service.Post(MakeRequest(1, "plain"), Raw());
            service.Post(MakeRequest(1, "tried"), Raw());
            service.Drain();

            ASSERT_EQ(texts.size(), 2u);
            EXPECT_EQ(texts[0], expected);
            EXPECT_EQ(texts[1], expected);
        }
        EXPECT_GT(misuses, 0u);
    }

    TEST_F(ControlResponseTest, TheReplyAfterARollbackIsIntactAndKeepsTheScratch) {
        service.RegisterHandler("fails", [](ControlCall& call) {
            call.TryReply([](JsonWriter& writer) -> std::expected<void, ControlError> {
                writer.BeginArray();
                writer.Value("a long text that grows the reply buffer well past a small reply");
                return std::unexpected(MakeControlError("c", "m"));
            });
        });
        service.RegisterHandler("ok", [](ControlCall& call) { call.Reply([](JsonWriter& writer) { writer.Value(1); }); });

        service.Post(MakeRequest(1, "fails"), Raw());
        service.Post(MakeRequest(2, "ok"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 2u);
        EXPECT_EQ(texts[1], String(R"({"id":2,"ok":true,"result":1})"));
    }

    TEST_F(ControlResponseTest, TryReplyInANestedCallIsReadBackAsAValueOrItsError) {
        service.RegisterHandler("good", [](ControlCall& call) {
            call.TryReply([](JsonWriter& writer) -> std::expected<void, ControlError> {
                writer.BeginObject();
                writer.Member("n", 5);
                writer.EndObject();
                return {};
            });
        });
        service.RegisterHandler("bad", [](ControlCall& call) {
            call.TryReply([](JsonWriter& writer) -> std::expected<void, ControlError> {
                writer.BeginObject();
                return std::unexpected(MakeControlError("nested_failure", "m"));
            });
        });
        service.RegisterHandler("outer", [this](ControlCall& call) {
            auto good = service.RunNested(call, "good", Json::object(), [](ControlCall&, ControlAnswer) {});
            auto bad = service.RunNested(call, "bad", Json::object(), [](ControlCall&, ControlAnswer) {});
            ASSERT_TRUE(good.has_value() && good->has_value());
            ASSERT_TRUE(bad.has_value() && !bad->has_value());
            Json result = Json::object();
            result["good"] = std::move(**good);
            result["bad"] = MakeJsonString(bad->error().code);
            call.Reply(std::move(result));
        });

        service.Post(MakeRequest(1, "outer"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 1u);
        EXPECT_EQ(texts[0], String(R"({"id":1,"ok":true,"result":{"bad":"nested_failure","good":{"n":5}}})"));
    }

    TEST_F(ControlResponseTest, PublishWithAWriterIsTheSameBytesAsPublishWithTheJson) {
        service.SetEventSink([this](const String& text) { texts.emplace_back(text.c_str(), text.size()); });

        service.Publish("state_changed", ObjectResult());
        service.Publish("state_changed", [](JsonWriter& writer) { WriteJson(writer, ObjectResult()); });
        service.Publish("e \"x\"", Json());
        service.Publish("e \"x\"", [](JsonWriter& writer) { writer.Null(); });

        ASSERT_EQ(texts.size(), 4u);
        EXPECT_EQ(texts[0], texts[1]);
        EXPECT_EQ(texts[2], texts[3]);
    }

    TEST_F(ControlResponseTest, PublishWithAWriterDoesNothingWithoutASinkAndAnObserver) {
        int calls = 0;
        service.Publish("e", [&calls](JsonWriter& writer) {
            ++calls;
            writer.Null();
        });

        EXPECT_EQ(calls, 0);
    }

    TEST_F(ControlResponseTest, PublishWithAWriterGivesAnObserverTheWrittenDataAndTheSinkTheSameText) {
        service.SetEventSink([this](const String& text) { texts.emplace_back(text.c_str(), text.size()); });
        Vector<Json> seen;
        const auto id = service.AddPublishObserver([&seen](StringView, const Json& data) { seen.push_back(data); });

        service.Publish("e", [](JsonWriter& writer) { WriteJson(writer, ObjectResult()); });
        service.RemovePublishObserver(id);

        ASSERT_EQ(seen.size(), 1u);
        const auto expectedData = ParseJson(DumpJson(ObjectResult()));  // the invalid UTF-8 of the sample is replaced by the dump, in the observer's copy too
        ASSERT_TRUE(expectedData.has_value());
        EXPECT_TRUE(seen[0] == *expectedData);  // not EXPECT_EQ: a failure would print the Json
        ASSERT_EQ(texts.size(), 1u);
        String reference;
        service.SetEventSink([&reference](const String& text) { reference = text; });
        service.Publish("e", ObjectResult());
        EXPECT_EQ(texts[0], reference);
    }

    TEST_F(ControlResponseTest, PublishWithAWriterThatWritesNoValueSendsNothing) {
        AssertBridge::Capture capture([](const AssertEvent&) {});  // the EXPECT of Publish
        service.SetEventSink([this](const String& text) { texts.emplace_back(text.c_str(), text.size()); });

        service.Publish("e", [](JsonWriter&) {});

        EXPECT_TRUE(texts.empty());
    }

    TEST_F(ControlResponseTest, ASecondPushWithAWriterIntoTheKeptBufferAllocatesNothing) {
        size_t sent = 0;
        service.SetEventSink([&sent](const String& text) { sent += text.size(); });
        const auto push = [this] {
            service.Publish("state_changed", [](JsonWriter& writer) {
                writer.BeginObject();
                writer.Member("game", "Stress");
                writer.Member("version", 12);
                writer.EndObject();
            });
        };
        push();

        ExpectNoAllocations(push);

        EXPECT_GT(sent, 0u);
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
