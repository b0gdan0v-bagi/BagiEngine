// The text of a control response and of a pushed event (ControlCall.h, ControlService::Publish): the bytes the wire carries, the reply buffer of the service and
// the writer form of ControlCall::Reply. The handlers' own answers are pinned where the handler is tested.

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/JsonViewOps.h>
#include <Common/AllocCheck.h>
#include <Common/AssertBridge.h>
#include <Common/ControlTestSupport.h>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        /// A result of every kind of member, written by hand in the byte order of its keys
        void WriteObjectResult(JsonWriter& writer) {
            writer.BeginObject();
            writer.Key("big");
            writer.Raw("18446744073709551615");
            writer.Member("count", 3);
            writer.Key("empty");
            writer.BeginObject();
            writer.EndObject();
            writer.Member("flag", true);
            writer.Key("list");
            writer.BeginArray();
            writer.Value(1);
            writer.Value(2);
            writer.BeginArray();
            writer.EndArray();
            writer.Value("x");
            writer.EndArray();
            writer.Member("name", "a \"quoted\"\n\xC3\xA9 \xFF text");
            writer.Key("none");
            writer.Null();
            writer.Member("ratio", 0.1);
            writer.EndObject();
        }

        /// What the DOM dumped for the object above, printed once from it; its invalid byte 0xFF is the U+FFFD of the writer
        constexpr StringView kObjectResultText = "{\"big\":18446744073709551615,\"count\":3,\"empty\":{},\"flag\":true,\"list\":[1,2,[],\"x\"],"
                                                 "\"name\":\"a \\\"quoted\\\"\\n\xC3\xA9 \xEF\xBF\xBD text\",\"none\":null,\"ratio\":0.1}";

        String OkText(StringView id, StringView resultText) {
            return Format(R"({{"id":{},"ok":true,"result":{}}})", id, resultText);
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

    TEST_F(ControlResponseTest, OkResponseBytesAreThePinnedTextForEveryKindOfIdAndResult) {
        struct ResultCase {
            FunctionRef<void(JsonWriter&)> write;
            StringView text;  // what the DOM path dumped for the same value
        };
        const auto writeArray = [](JsonWriter& writer) {
            writer.BeginArray();
            writer.Value(1);
            writer.Value("two");
            writer.BeginObject();
            writer.EndObject();
            writer.EndArray();
        };
        const auto writeInt = [](JsonWriter& writer) { writer.Value(42); };
        const auto writeString = [](JsonWriter& writer) { writer.Value("text"); };
        const auto writeNull = [](JsonWriter& writer) { writer.Null(); };
        const auto writeFalse = [](JsonWriter& writer) { writer.Value(false); };
        const Vector<ResultCase> results = {
            {WriteObjectResult, kObjectResultText}, {writeArray, R"([1,"two",{}])"}, {writeInt, "42"}, {writeString, R"("text")"}, {writeNull, "null"}, {writeFalse, "false"},
        };
        const Vector<StringView> ids = {"7", R"("req-\"1\"")", kNullId};

        for (const StringView id : ids) {
            for (const ResultCase& result : results) {
                String text;
                AppendOkResponse(text, id, result.write);
                EXPECT_EQ(text, OkText(id, result.text));
            }
        }
    }

    TEST_F(ControlResponseTest, OkResponseHasItsKeysInAlphabeticalOrder) {
        String empty;
        AppendOkResponse(empty, "1", [](JsonWriter& writer) {
            writer.BeginObject();
            writer.EndObject();
        });
        EXPECT_EQ(empty, R"({"id":1,"ok":true,"result":{}})");

        String one;
        AppendOkResponse(one, R"("x")", [](JsonWriter& writer) {
            writer.BeginObject();
            writer.Member("a", 1);
            writer.EndObject();
        });
        EXPECT_EQ(one, R"({"id":"x","ok":true,"result":{"a":1}})");
    }

    TEST_F(ControlResponseTest, AResultNestedPastTheWritersLimitIsCutNotAMisuse) {
        String deepText;
        for (size_t level = 0; level < JsonWriter::kMaxDepth; ++level) {
            deepText += '[';
        }
        for (size_t level = 0; level < JsonWriter::kMaxDepth; ++level) {
            deepText += ']';
        }
        const JsonDoc deep(StringView(deepText.c_str(), deepText.size()));
        ASSERT_TRUE(deep.IsParsed());

        // The envelope's own object counts as a level; an EXPECT of the writer would fail the case
        String text;
        AppendOkResponse(text, "1", [&deep](JsonWriter& writer) { WriteJson(writer, deep.Root()); });

        const ControlReply reply{StringView(text.c_str(), text.size())};
        ASSERT_TRUE(reply.View().IsValid());
        EXPECT_TRUE(reply.IsOk());
    }

    TEST_F(ControlResponseTest, ErrorResponseBytesAreThePinnedText) {
        const ControlError error = MakeControlError(ControlErrors::kInvalidParams, "bad \"x\"\n\xFF");

        EXPECT_EQ(MakeErrorResponse("5", error), "{\"error\":{\"code\":\"invalid_params\",\"message\":\"bad \\\"x\\\"\\n\xEF\xBF\xBD\"},\"id\":5,\"ok\":false}");
        EXPECT_EQ(MakeErrorResponse(kNullId, MakeControlError("c", "m")), R"({"error":{"code":"c","message":"m"},"id":null,"ok":false})");
    }

    TEST_F(ControlResponseTest, AppendingKeepsWhatIsAlreadyInTheString) {
        String out("prefix ");
        AppendOkResponse(out, "1", [](JsonWriter& writer) { writer.Value(true); });
        AppendErrorResponse(out, "2", MakeControlError("c", "m"));

        EXPECT_EQ(out, String(R"(prefix {"id":1,"ok":true,"result":true}{"error":{"code":"c","message":"m"},"id":2,"ok":false})"));
    }

    TEST_F(ControlResponseTest, ASecondResponseIntoAStringThatKeptItsCapacityAllocatesNothing) {
        String out;
        const auto write = [&out] {
            out.clear();
            AppendOkResponse(out, "7", [](JsonWriter& writer) {
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

    TEST_F(ControlResponseTest, ReplyWithAWriterWritesTheObjectAsThePinnedText) {
        service.RegisterHandler("viaWriter", [](ControlCall& call) { call.Reply(WriteObjectResult); });

        service.Post(MakeRequestText(4, "viaWriter"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 1u);
        EXPECT_EQ(texts[0], OkText("4", kObjectResultText));
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

        service.Post(MakeRequestText(1, "scalar"), Raw());
        service.Post(MakeRequestText(2, "array"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 2u);
        EXPECT_EQ(texts[0], String(R"({"id":1,"ok":true,"result":3})"));
        EXPECT_EQ(texts[1], String(R"({"id":2,"ok":true,"result":["a",null]})"));
    }

    TEST_F(ControlResponseTest, ANestedCallThatRepliesWithAWriterGivesItsAnswerAsTheTextItWrote) {
        service.RegisterHandler("inner", [](ControlCall& call) {
            call.Reply([](JsonWriter& writer) {
                writer.BeginObject();
                writer.Member("n", 5);
                writer.Member("s", "t");
                writer.EndObject();
            });
        });
        String nestedText;
        service.RegisterHandler("outer", [this, &nestedText](ControlCall& call) {
            auto answer = service.RunNested(call, "inner", EmptyParamsView(), [](ControlCall&, ControlAnswer) {});
            ASSERT_TRUE(answer.has_value());
            ASSERT_TRUE(answer->has_value());
            nestedText = std::move(**answer);
            call.ReplyEmpty();
        });

        service.Post(MakeRequestText(1, "outer"), Raw());
        service.Drain();

        EXPECT_EQ(nestedText, String(R"({"n":5,"s":"t"})"));
        ASSERT_EQ(texts.size(), 1u);
        EXPECT_EQ(texts[0], String(R"({"id":1,"ok":true,"result":{}})"));
    }

    TEST_F(ControlResponseTest, TheErrorOfAFailedCallIsWrittenAsThePinnedText) {
        service.RegisterHandler("fail", [](ControlCall& call) { call.Fail(ControlErrors::kInvalidParams, "no \"x\""); });

        service.Post(MakeRequestText(8, "fail"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 1u);
        EXPECT_EQ(texts[0], R"({"error":{"code":"invalid_params","message":"no \"x\""},"id":8,"ok":false})");
    }

    TEST_F(ControlResponseTest, PushedEventHasItsKeysInAlphabeticalOrder) {
        service.SetEventSink([this](const String& text) { texts.emplace_back(text.c_str(), text.size()); });

        service.Publish("state \"x\"", [](JsonWriter& writer) {
            writer.BeginObject();
            writer.Member("version", 3);
            writer.EndObject();
        });
        service.Publish("e", [](JsonWriter& writer) { writer.Null(); });

        ASSERT_EQ(texts.size(), 2u);
        EXPECT_EQ(texts[0], String(R"({"data":{"version":3},"event":"state \"x\""})"));
        EXPECT_EQ(texts[1], String(R"({"data":null,"event":"e"})"));
    }

    TEST_F(ControlResponseTest, TryReplyWithASuccessWritesTheSameTextAsReply) {
        service.RegisterHandler("viaTry", [](ControlCall& call) {
            call.TryReply([](JsonWriter& writer) -> std::expected<void, ControlError> {
                WriteObjectResult(writer);
                return {};
            });
        });

        service.Post(MakeRequestText(4, "viaTry"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 1u);
        EXPECT_EQ(texts[0], OkText("4", kObjectResultText));
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

        service.Post(MakeRequestText(3, "fails"), Raw());
        service.Post(MakeRequestText(4, "after"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 2u);
        EXPECT_EQ(texts[0], MakeErrorResponse("3", error));
        EXPECT_EQ(texts[1], String(R"({"id":4,"ok":true,"result":"intact"})"));
    }

    TEST_F(ControlResponseTest, AWriterThatDoesNotWriteOneValueIsAnInternalErrorForBothForms) {
        AssertBridge::Capture capture([](const AssertEvent&) {});  // the writer's own EXPECT of a misuse (compiled out in a profile build)
        const String expected = MakeErrorResponse("1", MakeControlError(ControlErrors::kInternalError, "the handler wrote a result that is not one JSON value"));

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
            service.Post(MakeRequestText(1, "plain"), Raw());
            service.Post(MakeRequestText(1, "tried"), Raw());
            service.Drain();

            ASSERT_EQ(texts.size(), 2u);
            EXPECT_EQ(texts[0], expected);
            EXPECT_EQ(texts[1], expected);
        }
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

        service.Post(MakeRequestText(1, "fails"), Raw());
        service.Post(MakeRequestText(2, "ok"), Raw());
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
            auto good = service.RunNested(call, "good", EmptyParamsView(), [](ControlCall&, ControlAnswer) {});
            auto bad = service.RunNested(call, "bad", EmptyParamsView(), [](ControlCall&, ControlAnswer) {});
            ASSERT_TRUE(good.has_value() && good->has_value());
            ASSERT_TRUE(bad.has_value() && !bad->has_value());
            call.Reply([&](JsonWriter& writer) {
                writer.BeginObject();
                writer.Member("bad", bad->error().code);
                writer.Key("good");
                writer.Raw(**good);
                writer.EndObject();
            });
        });

        service.Post(MakeRequestText(1, "outer"), Raw());
        service.Drain();

        ASSERT_EQ(texts.size(), 1u);
        EXPECT_EQ(texts[0], String(R"({"id":1,"ok":true,"result":{"bad":"nested_failure","good":{"n":5}}})"));
    }

    TEST_F(ControlResponseTest, PublishWithAWriterSendsThePinnedText) {
        service.SetEventSink([this](const String& text) { texts.emplace_back(text.c_str(), text.size()); });

        service.Publish("state_changed", WriteObjectResult);
        service.Publish("e \"x\"", [](JsonWriter& writer) { writer.Null(); });

        ASSERT_EQ(texts.size(), 2u);
        EXPECT_EQ(texts[0], Format(R"({{"data":{},"event":"state_changed"}})", kObjectResultText));
        EXPECT_EQ(texts[1], String(R"({"data":null,"event":"e \"x\""})"));
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
        Vector<String> seen;
        const auto id = service.AddPublishObserver([&seen](StringView, JsonView data) { seen.emplace_back(data.Raw()); });

        service.Publish("e", WriteObjectResult);
        service.RemovePublishObserver(id);

        ASSERT_EQ(seen.size(), 1u);
        EXPECT_EQ(seen[0], String(kObjectResultText));  // the invalid UTF-8 of the sample is replaced in the observer's copy too
        ASSERT_EQ(texts.size(), 1u);
        EXPECT_EQ(texts[0], Format(R"({{"data":{},"event":"e"}})", kObjectResultText));
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

        service.Post(MakeRequestText(1, "wait"), Raw());
        service.Drain();
        ASSERT_TRUE(texts.empty());

        service.Post(MakeRequestText(2, "quick"), [&](const String& text) {
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

        std::thread poster([&] { service.Post(MakeRequestText(6, "late"), [&text](const String& reply) { text = reply; }); });
        poster.join();

        EXPECT_EQ(text, String(R"({"error":{"code":"shutting_down","message":"the application is shutting down"},"id":6,"ok":false})"));
    }

}  // namespace BECore::Tests
