#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Time/Time.h>
#include <Common/ControlTestSupport.h>
#include <Events/ApplicationEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        StringView View(StringView text) {
            return text;
        }

        String FramesOf(int64_t frames) {
            return Format(R"({{"frames":{}}})", frames);
        }

        /// One entry of the `calls` of a batch; a null (empty) @p paramsText is left out
        String CallOf(StringView method, StringView paramsText = {}) {
            String text;
            JsonWriter writer(text);
            writer.BeginObject();
            writer.Member("method", method);
            if (!paramsText.empty()) {
                writer.Key("params");
                writer.Raw(paramsText);
            }
            writer.EndObject();
            return text;
        }

        /// The text of an array of the given elements (each already JSON text)
        String ArrayOf(const Vector<String>& elements) {
            String text("[");
            for (size_t index = 0; index < elements.size(); ++index) {
                if (index != 0) {
                    text += ',';
                }
                text += elements[index];
            }
            text += ']';
            return text;
        }

        /// The `params` of a batch over @p callsText (the text of the array of calls)
        String BatchOf(StringView callsText, bool continueOnError = false) {
            String text;
            JsonWriter writer(text);
            writer.BeginObject();
            writer.Key("calls");
            writer.Raw(callsText);
            if (continueOnError) {
                writer.Member("continueOnError", true);
            }
            writer.EndObject();
            return text;
        }

        String BatchOf(const Vector<String>& calls, bool continueOnError = false) {
            return BatchOf(ArrayOf(calls), continueOnError);
        }

        /// Counts the QuitEvents that a `quit` emits
        class QuitCounter : public SubscriptionHolder {
        public:
            using SubscriptionHolder::Subscribe;

            int count = 0;

            void OnQuit(const ApplicationEvents::QuitEvent&) {
                ++count;
            }
        };

        /// A call to run inside the outer handler of a test
        struct NestedStep {
            String method;
            String params = "{}";
        };

        class ControlBatchTest : public ::testing::Test {
        protected:
            void SetUp() override {
                service.SetManualClock(true);
                RegisterControlBuiltins(service);
                // The test's own outer handler: runs `plan` through RunNested, one call after the other, the way a batch does, and answers with how many ran
                service.RegisterHandler("outer", [this](ControlCall& call) {
                    order.push_back("outer");
                    next = 0;
                    answers.clear();
                    RunPlan(call);
                });
                service.RegisterHandler("count", [this](ControlCall& call) {
                    order.push_back("count");
                    call.ReplyEmpty();
                });
                service.RegisterHandler("fail", [this](ControlCall& call) {
                    order.push_back("fail");
                    call.Fail("boom", "it failed");
                });
                service.RegisterHandler("silent", [this](ControlCall&) { order.push_back("silent"); });
            }

            void TearDown() override {
                CoreManager::GetTime().Reset();
            }

            ControlReplyFn Collect() {
                return [this](const String& text) {
                    raw.push_back(text);
                    responses.emplace_back(StringView(text.c_str(), text.size()));
                };
            }

            void Post(const String& text) {
                service.Post(text, Collect());
            }

            void RunPlan(ControlCall& outer) {
                while (next < plan.size()) {
                    const NestedStep& step = plan[next++];
                    auto answer = service.RunNested(outer, View(step.method), ParamsOf(step), [this](ControlCall& again, ControlAnswer later) {
                        order.push_back("continued");
                        answers.push_back(std::move(later));
                        RunPlan(again);
                    });
                    if (!answer) {
                        return;  // deferred: the continuation carries on
                    }
                    answers.push_back(std::move(*answer));
                }
                outer.Reply([ran = answers.size()](JsonWriter& writer) {
                    writer.BeginObject();
                    writer.Member("ran", static_cast<int64_t>(ran));
                    writer.EndObject();
                });
            }

            /// The params of @p step as a view of a tape that lives as long as the test: a nested call that defers keeps its view until the outer call is answered
            JsonView ParamsOf(const NestedStep& step) {
                RequestBuffers& buffers = *tapes.emplace_back(MakeUnique<RequestBuffers>());
                buffers.text = step.params;
                EXPECT_TRUE(buffers.tape.Parse(buffers.text).has_value());
                return JsonView(buffers.tape, 0);
            }

            /// The answer of a nested call as a reply; an empty one (nothing is valid) when it is an error
            static ControlReply Parsed(const ControlAnswer& answer) {
                return answer ? ControlReply(StringView(answer->c_str(), answer->size())) : ControlReply();
            }

            /// The application loop of the manual clock: a frame runs while a step is due. Returns how many ran
            int RunDueFrames() {
                constexpr int kGuard = 1000;  // a loop that does not end would hang the test run
                int frames = 0;
                while (service.IsStepDue() && frames < kGuard) {
                    CoreManager::GetTime().Advance(service.GetStepSeconds());
                    service.Drain();
                    service.EndFrame();
                    ++frames;
                }
                return frames;
            }

            JsonView ResultOf(size_t index) const {
                return index < responses.size() ? responses[index].Result() : JsonView();
            }

            /// The entry @p index of `results` in the answer to the request @p response; not valid when there is none
            JsonView EntryOf(size_t response, size_t index) const {
                return ItemAt(MemberOf(ResultOf(response), "results"), index);
            }

            size_t ResultCount(size_t response) const {
                const JsonView results = MemberOf(ResultOf(response), "results");
                return results.IsArray() ? results.Size() : 0;
            }

            ControlService service;
            Vector<ControlReply> responses;
            Vector<String> raw;  // the reply texts as they were sent
            Vector<String> order;
            Vector<NestedStep> plan;
            Vector<ControlAnswer> answers;
            Vector<UniquePtr<RequestBuffers>> tapes;
            size_t next = 0;
        };

    }  // namespace

    TEST_F(ControlBatchTest, NestedCallThatAnswersAtOnceNeedsNoFrame) {
        plan = {{"ping"}, {"count"}};
        Post(MakeRequestText(1, "outer"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(responses[0].IsOk());
        EXPECT_EQ(IntOf(ResultOf(0), "ran").value_or(-1), 2);
        EXPECT_FALSE(service.IsStepDue());
        ASSERT_EQ(answers.size(), 2u);
        ASSERT_TRUE(answers[0].has_value());
        EXPECT_TRUE(BoolOf(Parsed(answers[0]).View(), "pong").value_or(false));
        EXPECT_EQ(order, (Vector<String>{"outer", "count"}));
    }

    TEST_F(ControlBatchTest, NestedCallStepDefersTheOuterAndHoldsTheRequestsBehindIt) {
        plan = {{"ping"}, {"step", FramesOf(3)}, {"count"}};
        Post(MakeRequestText(1, "outer"));
        Post(MakeRequestText(2, "count"));
        service.Drain();

        EXPECT_TRUE(responses.empty());
        ASSERT_TRUE(service.IsStepDue());
        EXPECT_EQ(order, (Vector<String>{"outer"}));  // `count` of the nested plan and the posted one both wait

        EXPECT_EQ(RunDueFrames(), 3);
        ASSERT_EQ(responses.size(), 1u);  // the outer is answered; the request behind it is still in the inbox
        EXPECT_EQ(responses[0].IdText(), "1");
        EXPECT_EQ(IntOf(ResultOf(0), "ran").value_or(-1), 3);
        ASSERT_EQ(answers.size(), 3u);
        ASSERT_TRUE(answers[1].has_value());
        EXPECT_TRUE(MemberOf(Parsed(answers[1]).View(), "frame").IsValid());  // the answer of `step`
        EXPECT_EQ(order, (Vector<String>{"outer", "continued", "count"}));

        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(responses[1].IdText(), "2");
    }

    TEST_F(ControlBatchTest, NestedCallCanDeferASecondTimeFromTheContinuation) {
        plan = {{"step", FramesOf(2)}, {"step", FramesOf(3)}, {"count"}};
        Post(MakeRequestText(1, "outer"));
        service.Drain();

        EXPECT_EQ(RunDueFrames(), 5);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(responses[0].IsOk());
        EXPECT_EQ(IntOf(ResultOf(0), "ran").value_or(-1), 3);
        EXPECT_EQ(order, (Vector<String>{"outer", "continued", "continued", "count"}));
        EXPECT_FALSE(service.IsStepDue());
    }

    TEST_F(ControlBatchTest, NestedCallStepKeepsTheSecondsOfItsFrames) {
        constexpr double kSeconds = 0.25;
        plan = {{"step", Format(R"({{"dt":{},"frames":2}})", kSeconds)}};
        Post(MakeRequestText(1, "outer"));
        service.Drain();

        ASSERT_TRUE(service.IsStepDue());
        EXPECT_DOUBLE_EQ(service.GetStepSeconds(), kSeconds);
        RunDueFrames();
        ASSERT_EQ(responses.size(), 1u);
    }

    TEST_F(ControlBatchTest, NestedCallToAnUnknownMethodIsAnErrorValue) {
        plan = {{"nope"}, {"ping"}};
        Post(MakeRequestText(1, "outer"));
        service.Drain();

        ASSERT_EQ(answers.size(), 2u);
        ASSERT_FALSE(answers[0].has_value());
        EXPECT_EQ(answers[0].error().code, ControlErrors::kUnknownMethod);
        EXPECT_TRUE(answers[1].has_value());
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(responses[0].IsOk());
    }

    TEST_F(ControlBatchTest, NestedCallWithoutAnAnswerIsAnInternalError) {
        plan = {{"silent"}, {"ping"}};
        Post(MakeRequestText(1, "outer"));
        service.Drain();

        ASSERT_EQ(answers.size(), 2u);
        ASSERT_FALSE(answers[0].has_value());
        EXPECT_EQ(answers[0].error().code, ControlErrors::kInternalError);
        EXPECT_TRUE(answers[1].has_value());
        EXPECT_EQ(order, (Vector<String>{"outer", "silent"}));
    }

    TEST_F(ControlBatchTest, NestedCallOfTheRunningMethodIsRefused) {
        plan = {{"outer"}};
        Post(MakeRequestText(1, "outer"));
        service.Drain();

        ASSERT_EQ(answers.size(), 1u);
        ASSERT_FALSE(answers[0].has_value());
        EXPECT_EQ(answers[0].error().code, ControlErrors::kInternalError);
        EXPECT_EQ(order, (Vector<String>{"outer"}));
    }

    TEST_F(ControlBatchTest, NestedCallHandlerMayReplaceItself) {
        // The nested handler registers its own name again: the replaced handler must live until it returns, and the new one is the one in the table afterwards
        int replacedRuns = 0;
        int replacementRuns = 0;
        service.RegisterHandler("swap", [this, &replacedRuns, &replacementRuns](ControlCall& call) {
            ++replacedRuns;
            service.RegisterHandler("swap", [&replacementRuns](ControlCall& again) {
                ++replacementRuns;
                again.ReplyEmpty();
            });
            call.ReplyEmpty();
        });
        plan = {{"swap"}, {"swap"}};
        Post(MakeRequestText(1, "outer"));
        service.Drain();

        EXPECT_EQ(replacedRuns, 1);
        EXPECT_EQ(replacementRuns, 1);
        ASSERT_EQ(answers.size(), 2u);
        EXPECT_TRUE(answers[0].has_value());
        EXPECT_TRUE(answers[1].has_value());
    }

    TEST_F(ControlBatchTest, NestedCallShutdownAnswersTheOuterAndFreesTheState) {
        plan = {{"step", FramesOf(5)}, {"count"}};
        Post(MakeRequestText(1, "outer"));
        Post(MakeRequestText(2, "count"));
        service.Drain();
        ASSERT_TRUE(service.IsStepDue());

        service.Shutdown();

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(responses[0].ErrorCode(), ControlErrors::kShuttingDown);
        EXPECT_EQ(responses[1].ErrorCode(), ControlErrors::kShuttingDown);
        EXPECT_TRUE(answers.empty());  // the continuation did not run
        EXPECT_EQ(order, (Vector<String>{"outer"}));
        EXPECT_FALSE(service.IsStepDue());
    }

    TEST_F(ControlBatchTest, NestedCallOfABypassingOuterCannotDeferIt) {
        // Another call is pending, so the barrier-bypassing outer is refused its deferral as it would be without a nested call
        service.RegisterHandler(
            "bypass",
            [this](ControlCall& call) {
                order.push_back("bypass");
                plan = {{"step", FramesOf(2)}};
                next = 0;
                answers.clear();
                RunPlan(call);
            },
            /*bypassBarrier=*/true);
        plan.clear();
        Post(MakeRequestText(1, "step", FramesOf(4)));
        Post(MakeRequestText(2, "bypass"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].IdText(), "2");
        EXPECT_EQ(responses[0].ErrorCode(), ControlErrors::kInternalError);
        EXPECT_EQ(RunDueFrames(), 4);  // the step that was pending runs on undisturbed
        EXPECT_EQ(responses.size(), 2u);
    }

    TEST_F(ControlBatchTest, BatchAnswersEveryCallInOrder) {
        Post(MakeRequestText(1, "batch", BatchOf(Vector<String>{CallOf("ping"), CallOf("count"), CallOf("status")})));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(responses[0].IsOk());
        ASSERT_EQ(ResultCount(0), 3u);
        EXPECT_TRUE(BoolOf(MemberOf(EntryOf(0, 0), "result"), "pong").value_or(false));
        EXPECT_TRUE(BoolOf(EntryOf(0, 1), "ok").value_or(false));
        EXPECT_TRUE(MemberOf(MemberOf(EntryOf(0, 2), "result"), "frame").IsValid());
        EXPECT_FALSE(MemberOf(ResultOf(0), "stopped").IsValid());
    }

    TEST_F(ControlBatchTest, BatchStopsAtTheFirstError) {
        Post(MakeRequestText(1, "batch", BatchOf(Vector<String>{CallOf("count"), CallOf("fail"), CallOf("count"), CallOf("count")})));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(responses[0].IsOk());  // the request is fine whatever its calls answered
        ASSERT_EQ(ResultCount(0), 2u);
        EXPECT_FALSE(BoolOf(EntryOf(0, 1), "ok").value_or(true));
        EXPECT_EQ(StringOf(MemberOf(EntryOf(0, 1), "error"), "code"), "boom");
        EXPECT_EQ(StringOf(MemberOf(EntryOf(0, 1), "error"), "message"), "it failed");
        EXPECT_EQ(IntOf(ResultOf(0), "stopped").value_or(-1), 1);
        EXPECT_EQ(order, (Vector<String>{"count", "fail"}));
    }

    TEST_F(ControlBatchTest, BatchWithContinueOnErrorRunsEveryCall) {
        Post(MakeRequestText(1, "batch", BatchOf(Vector<String>{CallOf("count"), CallOf("fail"), CallOf("nope"), CallOf("count")}, /*continueOnError=*/true)));
        service.Drain();

        ASSERT_EQ(ResultCount(0), 4u);
        EXPECT_FALSE(MemberOf(ResultOf(0), "stopped").IsValid());
        EXPECT_EQ(StringOf(MemberOf(EntryOf(0, 2), "error"), "code"), ControlErrors::kUnknownMethod);
        EXPECT_TRUE(BoolOf(EntryOf(0, 3), "ok").value_or(false));
        EXPECT_EQ(order, (Vector<String>{"count", "fail", "count"}));
    }

    TEST_F(ControlBatchTest, UnknownMethodEndsTheBatchWithItsOwnError) {
        Post(MakeRequestText(1, "batch", BatchOf(Vector<String>{CallOf("nope"), CallOf("count")})));
        service.Drain();

        ASSERT_EQ(ResultCount(0), 1u);
        EXPECT_EQ(StringOf(MemberOf(EntryOf(0, 0), "error"), "code"), ControlErrors::kUnknownMethod);
        EXPECT_EQ(IntOf(ResultOf(0), "stopped").value_or(-1), 0);
        EXPECT_TRUE(order.empty());
    }

    TEST_F(ControlBatchTest, BatchAndQuitAreRefusedInsideABatch) {
        QuitCounter quits;
        quits.Subscribe<ApplicationEvents::QuitEvent, &QuitCounter::OnQuit>(&quits);
        const String inner = BatchOf(Vector<String>{CallOf("count")});
        Post(MakeRequestText(1, "batch", BatchOf(Vector<String>{CallOf("batch", inner), CallOf("quit"), CallOf("count")}, /*continueOnError=*/true)));
        service.Drain();

        ASSERT_EQ(ResultCount(0), 3u);
        for (size_t index = 0; index < 2; ++index) {
            EXPECT_FALSE(BoolOf(EntryOf(0, index), "ok").value_or(true));
            EXPECT_EQ(StringOf(MemberOf(EntryOf(0, index), "error"), "code"), ControlErrors::kInvalidParams);
        }
        EXPECT_EQ(quits.count, 0);
        EXPECT_EQ(order, (Vector<String>{"count"}));  // the inner batch did not run either
    }

    TEST_F(ControlBatchTest, BatchWithABadShapeIsInvalidParamsAndRunsNothing) {
        Vector<String> tooMany;
        for (size_t index = 0; index <= kMaxBatchCalls; ++index) {
            tooMany.push_back(CallOf("count"));
        }

        const Vector<String> badParams = {
            "{}",                                                                            // no calls
            BatchOf(Vector<String>{}),                                                       // empty
            BatchOf(tooMany),                                                                // over the limit
            BatchOf(R"({})"),                                                                // not an array
            BatchOf(R"("count")"),                                                           // a string
            BatchOf(Vector<String>{CallOf("count"), CallOf("")}),                            // method ""
            BatchOf(Vector<String>{CallOf("count"), R"("count")"}),                          // an entry that is a string
            BatchOf(Vector<String>{CallOf("count"), "{}"}),                                  // an entry without a method
            BatchOf(Vector<String>{CallOf("count"), R"({"method":5})"}),                     // method 5
            BatchOf(Vector<String>{CallOf("count"), R"({"method":"count","params":[1]})"}),  // params [1]
            R"({"calls":[{"method":"count"}],"continueOnError":"yes"})",                     // a flag that is not a boolean
        };

        int64_t id = 0;
        for (const String& params : badParams) {
            Post(MakeRequestText(++id, "batch", params));
        }
        service.Drain();

        ASSERT_EQ(responses.size(), badParams.size());
        for (size_t index = 0; index < responses.size(); ++index) {
            EXPECT_EQ(responses[index].ErrorCode(), ControlErrors::kInvalidParams) << "request " << index;
        }
        EXPECT_TRUE(order.empty());
    }

    TEST_F(ControlBatchTest, BatchAcceptsTheMostCallsAndNullParams) {
        Vector<String> calls;
        for (size_t index = 0; index < kMaxBatchCalls; ++index) {
            calls.push_back(CallOf("count", "null"));  // like a request, null params are none
        }
        Post(MakeRequestText(1, "batch", BatchOf(calls)));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ResultCount(0), kMaxBatchCalls);
        EXPECT_EQ(order.size(), kMaxBatchCalls);
    }

    TEST_F(ControlBatchTest, BatchPassesTheParamsOfEveryCall) {
        Post(MakeRequestText(1, "batch", BatchOf(Vector<String>{CallOf("step", FramesOf(0))})));
        service.Drain();

        ASSERT_EQ(ResultCount(0), 1u);  // `step` refused its own parameters; the batch carried them
        EXPECT_EQ(StringOf(MemberOf(EntryOf(0, 0), "error"), "code"), ControlErrors::kInvalidParams);
    }

    TEST_F(ControlBatchTest, StepInTheMiddleHoldsTheCallsBehindIt) {
        Post(MakeRequestText(1, "batch", BatchOf(Vector<String>{CallOf("count"), CallOf("step", FramesOf(2)), CallOf("count")})));
        Post(MakeRequestText(2, "count"));
        service.Drain();

        EXPECT_TRUE(responses.empty());
        EXPECT_EQ(order, (Vector<String>{"count"}));  // the call after the step and the request behind the batch both wait
        ASSERT_TRUE(service.IsStepDue());

        EXPECT_EQ(RunDueFrames(), 2);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].IdText(), "1");
        EXPECT_EQ(ResultCount(0), 3u);
        EXPECT_EQ(order, (Vector<String>{"count", "count"}));  // the request behind the batch sees the state after the whole batch

        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(responses[1].IdText(), "2");
        EXPECT_EQ(order, (Vector<String>{"count", "count", "count"}));
    }

    TEST_F(ControlBatchTest, BatchThatEndsWithAStepAnswersWithItsClock) {
        Post(MakeRequestText(1, "batch", BatchOf(Vector<String>{CallOf("count"), CallOf("step", FramesOf(3))})));
        service.Drain();

        EXPECT_EQ(RunDueFrames(), 3);
        ASSERT_EQ(responses.size(), 1u);
        ASSERT_EQ(ResultCount(0), 2u);
        const JsonView clock = MemberOf(EntryOf(0, 1), "result");
        ASSERT_TRUE(clock.IsObject());
        EXPECT_EQ(IntOf(clock, "frame").value_or(-1), static_cast<int64_t>(CoreManager::GetTime().GetFrameCount()));
    }

    TEST_F(ControlBatchTest, BatchWithSeveralStepsWaitsForAllTheirFrames) {
        Post(MakeRequestText(1, "batch", BatchOf(Vector<String>{CallOf("step", FramesOf(2)), CallOf("count"), CallOf("step", FramesOf(3)), CallOf("count")})));
        service.Drain();

        EXPECT_EQ(RunDueFrames(), 5);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ResultCount(0), 4u);
        EXPECT_EQ(order, (Vector<String>{"count", "count"}));
    }

    TEST_F(ControlBatchTest, ErrorAfterAStepStopsTheBatch) {
        Post(MakeRequestText(1, "batch", BatchOf(Vector<String>{CallOf("step", FramesOf(2)), CallOf("fail"), CallOf("count")})));
        service.Drain();

        EXPECT_EQ(RunDueFrames(), 2);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ResultCount(0), 2u);
        EXPECT_EQ(IntOf(ResultOf(0), "stopped").value_or(-1), 1);
        EXPECT_EQ(order, (Vector<String>{"fail"}));
    }

    TEST_F(ControlBatchTest, ShutdownDuringABatchAnswersItAndRunsNothingMore) {
        Post(MakeRequestText(1, "batch", BatchOf(Vector<String>{CallOf("step", FramesOf(5)), CallOf("count")})));
        Post(MakeRequestText(2, "count"));
        service.Drain();
        ASSERT_TRUE(service.IsStepDue());

        service.Shutdown();

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(responses[0].ErrorCode(), ControlErrors::kShuttingDown);
        EXPECT_EQ(responses[1].ErrorCode(), ControlErrors::kShuttingDown);
        EXPECT_TRUE(order.empty());
    }

    TEST_F(ControlBatchTest, TheBatchAnswersTheSameBytes) {
        service.RegisterHandler("echo_text", [](ControlCall& call) {
            call.Reply([](JsonWriter& writer) {
                writer.BeginObject();
                writer.Member("list", 3);
                writer.Member("text", "quote \" caf\xC3\xA9\n");
                writer.EndObject();
            });
        });
        const Vector<String> calls = {CallOf("ping"), CallOf("echo_text"), CallOf("fail"), CallOf("ping")};
        Post(MakeRequestText(1, "batch", BatchOf(calls)));
        Post(MakeRequestText(2, "batch", BatchOf(calls, true)));
        Post(MakeRequestText(3, "batch", BatchOf(Vector<String>{CallOf("ping")})));
        service.Drain();

        ExpectTexts(
            raw,
            {R"json({"id":1,"ok":true,"result":{"results":[{"ok":true,"result":{"pong":true,"protocol":1}},{"ok":true,"result":{"list":3,"text":"quote \" café\n"}},{"error":{"code":"boom","message":"it failed"},"ok":false}],"stopped":2}})json",
             R"json({"id":2,"ok":true,"result":{"results":[{"ok":true,"result":{"pong":true,"protocol":1}},{"ok":true,"result":{"list":3,"text":"quote \" café\n"}},{"error":{"code":"boom","message":"it failed"},"ok":false},{"ok":true,"result":{"pong":true,"protocol":1}}]}})json",
             R"json({"id":3,"ok":true,"result":{"results":[{"ok":true,"result":{"pong":true,"protocol":1}}]}})json"});
    }

}  // namespace BECore::Tests
