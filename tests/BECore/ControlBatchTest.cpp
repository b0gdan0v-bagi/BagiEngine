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

        Json FramesOf(int64_t frames) {
            Json params = Json::object();
            params["frames"] = frames;
            return params;
        }

        /// One entry of the `calls` of a batch
        Json CallOf(StringView method, Json params = Json()) {
            Json call = Json::object();
            call["method"] = ToStdString(method);
            if (!params.is_null()) {
                call["params"] = std::move(params);
            }
            return call;
        }

        Json BatchOf(Json calls, bool continueOnError = false) {
            Json params = Json::object();
            params["calls"] = std::move(calls);
            if (continueOnError) {
                params["continueOnError"] = true;
            }
            return params;
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
            Json params = Json::object();
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
                    call.Reply(Json::object());
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
                    auto parsed = ParseJson(text);
                    responses.push_back(parsed ? std::move(*parsed) : Json());
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
                Json result = Json::object();
                result["ran"] = static_cast<int64_t>(answers.size());
                outer.Reply(std::move(result));
            }

            /// The params of @p step as a view of a tape that lives as long as the test: a nested call that defers keeps its view until the outer call is answered
            JsonView ParamsOf(const NestedStep& step) {
                RequestBuffers& buffers = *tapes.emplace_back(MakeUnique<RequestBuffers>());
                buffers.text = DumpJson(step.params);
                EXPECT_TRUE(buffers.tape.Parse(buffers.text).has_value());
                return JsonView(buffers.tape, 0);
            }

            /// The answer of a nested call as a DOM, null when it is an error or does not parse
            static Json Parsed(const ControlAnswer& answer) {
                auto parsed = answer ? ParseJson(*answer) : std::expected<Json, ControlError>(std::unexpected(answer.error()));
                return parsed ? std::move(*parsed) : Json();
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

            const Json* ResultOf(size_t index) const {
                return index < responses.size() ? FindJsonMember(responses[index], "result") : nullptr;
            }

            /// The entry @p index of `results` in the answer to the request @p response, or null
            const Json* EntryOf(size_t response, size_t index) const {
                const Json* result = ResultOf(response);
                const Json* results = result != nullptr ? FindJsonMember(*result, "results") : nullptr;
                return results != nullptr && results->is_array() && index < results->size() ? &(*results)[index] : nullptr;
            }

            size_t ResultCount(size_t response) const {
                const Json* result = ResultOf(response);
                const Json* results = result != nullptr ? FindJsonMember(*result, "results") : nullptr;
                return results != nullptr && results->is_array() ? results->size() : 0;
            }

            ControlService service;
            Vector<Json> responses;
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
        Post(MakeRequest(1, "outer"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(IsOk(responses[0]));
        EXPECT_EQ(GetJsonInt(Require(FindJsonMember(responses[0], "result")), "ran").value_or(-1), 2);
        EXPECT_FALSE(service.IsStepDue());
        ASSERT_EQ(answers.size(), 2u);
        ASSERT_TRUE(answers[0].has_value());
        EXPECT_TRUE(GetJsonBool(Parsed(answers[0]), "pong").value_or(false));
        EXPECT_EQ(order, (Vector<String>{"outer", "count"}));
    }

    TEST_F(ControlBatchTest, NestedCallStepDefersTheOuterAndHoldsTheRequestsBehindIt) {
        plan = {{"ping"}, {"step", FramesOf(3)}, {"count"}};
        Post(MakeRequest(1, "outer"));
        Post(MakeRequest(2, "count"));
        service.Drain();

        EXPECT_TRUE(responses.empty());
        ASSERT_TRUE(service.IsStepDue());
        EXPECT_EQ(order, (Vector<String>{"outer"}));  // `count` of the nested plan and the posted one both wait

        EXPECT_EQ(RunDueFrames(), 3);
        ASSERT_EQ(responses.size(), 1u);  // the outer is answered; the request behind it is still in the inbox
        EXPECT_EQ(IdOf(responses[0]), 1);
        EXPECT_EQ(GetJsonInt(Require(FindJsonMember(responses[0], "result")), "ran").value_or(-1), 3);
        ASSERT_EQ(answers.size(), 3u);
        ASSERT_TRUE(answers[1].has_value());
        EXPECT_NE(FindJsonMember(Parsed(answers[1]), "frame"), nullptr);  // the answer of `step`
        EXPECT_EQ(order, (Vector<String>{"outer", "continued", "count"}));

        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(IdOf(responses[1]), 2);
    }

    TEST_F(ControlBatchTest, NestedCallCanDeferASecondTimeFromTheContinuation) {
        plan = {{"step", FramesOf(2)}, {"step", FramesOf(3)}, {"count"}};
        Post(MakeRequest(1, "outer"));
        service.Drain();

        EXPECT_EQ(RunDueFrames(), 5);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(IsOk(responses[0]));
        EXPECT_EQ(GetJsonInt(Require(FindJsonMember(responses[0], "result")), "ran").value_or(-1), 3);
        EXPECT_EQ(order, (Vector<String>{"outer", "continued", "continued", "count"}));
        EXPECT_FALSE(service.IsStepDue());
    }

    TEST_F(ControlBatchTest, NestedCallStepKeepsTheSecondsOfItsFrames) {
        constexpr double kSeconds = 0.25;
        Json params = FramesOf(2);
        params["dt"] = kSeconds;
        plan = {{"step", params}};
        Post(MakeRequest(1, "outer"));
        service.Drain();

        ASSERT_TRUE(service.IsStepDue());
        EXPECT_DOUBLE_EQ(service.GetStepSeconds(), kSeconds);
        RunDueFrames();
        ASSERT_EQ(responses.size(), 1u);
    }

    TEST_F(ControlBatchTest, NestedCallToAnUnknownMethodIsAnErrorValue) {
        plan = {{"nope"}, {"ping"}};
        Post(MakeRequest(1, "outer"));
        service.Drain();

        ASSERT_EQ(answers.size(), 2u);
        ASSERT_FALSE(answers[0].has_value());
        EXPECT_EQ(answers[0].error().code, ControlErrors::kUnknownMethod);
        EXPECT_TRUE(answers[1].has_value());
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(IsOk(responses[0]));
    }

    TEST_F(ControlBatchTest, NestedCallWithoutAnAnswerIsAnInternalError) {
        plan = {{"silent"}, {"ping"}};
        Post(MakeRequest(1, "outer"));
        service.Drain();

        ASSERT_EQ(answers.size(), 2u);
        ASSERT_FALSE(answers[0].has_value());
        EXPECT_EQ(answers[0].error().code, ControlErrors::kInternalError);
        EXPECT_TRUE(answers[1].has_value());
        EXPECT_EQ(order, (Vector<String>{"outer", "silent"}));
    }

    TEST_F(ControlBatchTest, NestedCallOfTheRunningMethodIsRefused) {
        plan = {{"outer"}};
        Post(MakeRequest(1, "outer"));
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
                again.Reply(Json::object());
            });
            call.Reply(Json::object());
        });
        plan = {{"swap"}, {"swap"}};
        Post(MakeRequest(1, "outer"));
        service.Drain();

        EXPECT_EQ(replacedRuns, 1);
        EXPECT_EQ(replacementRuns, 1);
        ASSERT_EQ(answers.size(), 2u);
        EXPECT_TRUE(answers[0].has_value());
        EXPECT_TRUE(answers[1].has_value());
    }

    TEST_F(ControlBatchTest, NestedCallShutdownAnswersTheOuterAndFreesTheState) {
        plan = {{"step", FramesOf(5)}, {"count"}};
        Post(MakeRequest(1, "outer"));
        Post(MakeRequest(2, "count"));
        service.Drain();
        ASSERT_TRUE(service.IsStepDue());

        service.Shutdown();

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(ErrorCode(responses[0]), ControlErrors::kShuttingDown);
        EXPECT_EQ(ErrorCode(responses[1]), ControlErrors::kShuttingDown);
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
        Post(MakeRequest(1, "step", FramesOf(4)));
        Post(MakeRequest(2, "bypass"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(IdOf(responses[0]), 2);
        EXPECT_EQ(ErrorCode(responses[0]), ControlErrors::kInternalError);
        EXPECT_EQ(RunDueFrames(), 4);  // the step that was pending runs on undisturbed
        EXPECT_EQ(responses.size(), 2u);
    }

    TEST_F(ControlBatchTest, BatchAnswersEveryCallInOrder) {
        Post(MakeRequest(1, "batch", BatchOf(Json::array({CallOf("ping"), CallOf("count"), CallOf("status")}))));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(IsOk(responses[0]));
        ASSERT_EQ(ResultCount(0), 3u);
        EXPECT_TRUE(GetJsonBool(Require(FindJsonMember(*EntryOf(0, 0), "result")), "pong").value_or(false));
        EXPECT_TRUE(GetJsonBool(*EntryOf(0, 1), "ok").value_or(false));
        EXPECT_NE(FindJsonMember(Require(FindJsonMember(*EntryOf(0, 2), "result")), "frame"), nullptr);
        EXPECT_EQ(FindJsonMember(*ResultOf(0), "stopped"), nullptr);
    }

    TEST_F(ControlBatchTest, BatchStopsAtTheFirstError) {
        Post(MakeRequest(1, "batch", BatchOf(Json::array({CallOf("count"), CallOf("fail"), CallOf("count"), CallOf("count")}))));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(IsOk(responses[0]));  // the request is fine whatever its calls answered
        ASSERT_EQ(ResultCount(0), 2u);
        EXPECT_FALSE(GetJsonBool(*EntryOf(0, 1), "ok").value_or(true));
        EXPECT_EQ(GetJsonString(Require(FindJsonMember(*EntryOf(0, 1), "error")), "code").value_or(""), "boom");
        EXPECT_EQ(GetJsonString(Require(FindJsonMember(*EntryOf(0, 1), "error")), "message").value_or(""), "it failed");
        EXPECT_EQ(GetJsonInt(*ResultOf(0), "stopped").value_or(-1), 1);
        EXPECT_EQ(order, (Vector<String>{"count", "fail"}));
    }

    TEST_F(ControlBatchTest, BatchWithContinueOnErrorRunsEveryCall) {
        Post(MakeRequest(1, "batch", BatchOf(Json::array({CallOf("count"), CallOf("fail"), CallOf("nope"), CallOf("count")}), /*continueOnError=*/true)));
        service.Drain();

        ASSERT_EQ(ResultCount(0), 4u);
        EXPECT_EQ(FindJsonMember(*ResultOf(0), "stopped"), nullptr);
        EXPECT_EQ(GetJsonString(Require(FindJsonMember(*EntryOf(0, 2), "error")), "code").value_or(""), ControlErrors::kUnknownMethod);
        EXPECT_TRUE(GetJsonBool(*EntryOf(0, 3), "ok").value_or(false));
        EXPECT_EQ(order, (Vector<String>{"count", "fail", "count"}));
    }

    TEST_F(ControlBatchTest, UnknownMethodEndsTheBatchWithItsOwnError) {
        Post(MakeRequest(1, "batch", BatchOf(Json::array({CallOf("nope"), CallOf("count")}))));
        service.Drain();

        ASSERT_EQ(ResultCount(0), 1u);
        EXPECT_EQ(GetJsonString(Require(FindJsonMember(*EntryOf(0, 0), "error")), "code").value_or(""), ControlErrors::kUnknownMethod);
        EXPECT_EQ(GetJsonInt(*ResultOf(0), "stopped").value_or(-1), 0);
        EXPECT_TRUE(order.empty());
    }

    TEST_F(ControlBatchTest, BatchAndQuitAreRefusedInsideABatch) {
        QuitCounter quits;
        quits.Subscribe<ApplicationEvents::QuitEvent, &QuitCounter::OnQuit>(&quits);
        const Json inner = BatchOf(Json::array({CallOf("count")}));
        Post(MakeRequest(1, "batch", BatchOf(Json::array({CallOf("batch", inner), CallOf("quit"), CallOf("count")}), /*continueOnError=*/true)));
        service.Drain();

        ASSERT_EQ(ResultCount(0), 3u);
        for (size_t index = 0; index < 2; ++index) {
            EXPECT_FALSE(GetJsonBool(*EntryOf(0, index), "ok").value_or(true));
            EXPECT_EQ(GetJsonString(Require(FindJsonMember(*EntryOf(0, index), "error")), "code").value_or(""), ControlErrors::kInvalidParams);
        }
        EXPECT_EQ(quits.count, 0);
        EXPECT_EQ(order, (Vector<String>{"count"}));  // the inner batch did not run either
    }

    TEST_F(ControlBatchTest, BatchWithABadShapeIsInvalidParamsAndRunsNothing) {
        Json tooMany = Json::array();
        for (size_t index = 0; index <= kMaxBatchCalls; ++index) {
            tooMany.push_back(CallOf("count"));
        }
        Json emptyMethod = Json::array({CallOf("count"), CallOf("")});
        Json notAnObject = Json::array({CallOf("count"), "count"});
        Json noMethod = Json::array({CallOf("count"), Json::object()});
        Json methodNotAString = Json::array({CallOf("count"), Json::object({{"method", 5}})});
        Json paramsNotAnObject = Json::array({CallOf("count"), Json::object({{"method", "count"}, {"params", Json::array({1})}})});

        Vector<Json> badParams;
        badParams.push_back(Json::object());                         // no calls
        badParams.push_back(BatchOf(Json::array()));                 // empty
        badParams.push_back(BatchOf(std::move(tooMany)));            // over the limit
        badParams.push_back(BatchOf(Json::object()));                // not an array
        badParams.push_back(BatchOf("count"));                       // a string
        badParams.push_back(BatchOf(std::move(emptyMethod)));        // method ""
        badParams.push_back(BatchOf(std::move(notAnObject)));        // an entry that is a string
        badParams.push_back(BatchOf(std::move(noMethod)));           // an entry without a method
        badParams.push_back(BatchOf(std::move(methodNotAString)));   // method 5
        badParams.push_back(BatchOf(std::move(paramsNotAnObject)));  // params [1]
        Json badFlag = BatchOf(Json::array({CallOf("count")}));
        badFlag["continueOnError"] = "yes";
        badParams.push_back(std::move(badFlag));

        int64_t id = 0;
        for (const Json& params : badParams) {
            Post(MakeRequest(++id, "batch", params));
        }
        service.Drain();

        ASSERT_EQ(responses.size(), badParams.size());
        for (size_t index = 0; index < responses.size(); ++index) {
            EXPECT_EQ(ErrorCode(responses[index]), ControlErrors::kInvalidParams) << "request " << index;
        }
        EXPECT_TRUE(order.empty());
    }

    TEST_F(ControlBatchTest, BatchAcceptsTheMostCallsAndNullParams) {
        Json calls = Json::array();
        for (size_t index = 0; index < kMaxBatchCalls; ++index) {
            Json call = CallOf("count");
            call["params"] = Json();  // like a request, null params are none
            calls.push_back(std::move(call));
        }
        Post(MakeRequest(1, "batch", BatchOf(std::move(calls))));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ResultCount(0), kMaxBatchCalls);
        EXPECT_EQ(order.size(), kMaxBatchCalls);
    }

    TEST_F(ControlBatchTest, BatchPassesTheParamsOfEveryCall) {
        Post(MakeRequest(1, "batch", BatchOf(Json::array({CallOf("step", FramesOf(0))}))));
        service.Drain();

        ASSERT_EQ(ResultCount(0), 1u);  // `step` refused its own parameters; the batch carried them
        EXPECT_EQ(GetJsonString(Require(FindJsonMember(*EntryOf(0, 0), "error")), "code").value_or(""), ControlErrors::kInvalidParams);
    }

    TEST_F(ControlBatchTest, StepInTheMiddleHoldsTheCallsBehindIt) {
        Post(MakeRequest(1, "batch", BatchOf(Json::array({CallOf("count"), CallOf("step", FramesOf(2)), CallOf("count")}))));
        Post(MakeRequest(2, "count"));
        service.Drain();

        EXPECT_TRUE(responses.empty());
        EXPECT_EQ(order, (Vector<String>{"count"}));  // the call after the step and the request behind the batch both wait
        ASSERT_TRUE(service.IsStepDue());

        EXPECT_EQ(RunDueFrames(), 2);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(IdOf(responses[0]), 1);
        EXPECT_EQ(ResultCount(0), 3u);
        EXPECT_EQ(order, (Vector<String>{"count", "count"}));  // the request behind the batch sees the state after the whole batch

        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(IdOf(responses[1]), 2);
        EXPECT_EQ(order, (Vector<String>{"count", "count", "count"}));
    }

    TEST_F(ControlBatchTest, BatchThatEndsWithAStepAnswersWithItsClock) {
        Post(MakeRequest(1, "batch", BatchOf(Json::array({CallOf("count"), CallOf("step", FramesOf(3))}))));
        service.Drain();

        EXPECT_EQ(RunDueFrames(), 3);
        ASSERT_EQ(responses.size(), 1u);
        ASSERT_EQ(ResultCount(0), 2u);
        const Json* clock = FindJsonMember(*EntryOf(0, 1), "result");
        ASSERT_NE(clock, nullptr);
        EXPECT_EQ(GetJsonInt(*clock, "frame").value_or(-1), static_cast<int64_t>(CoreManager::GetTime().GetFrameCount()));
    }

    TEST_F(ControlBatchTest, BatchWithSeveralStepsWaitsForAllTheirFrames) {
        Post(MakeRequest(1, "batch", BatchOf(Json::array({CallOf("step", FramesOf(2)), CallOf("count"), CallOf("step", FramesOf(3)), CallOf("count")}))));
        service.Drain();

        EXPECT_EQ(RunDueFrames(), 5);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ResultCount(0), 4u);
        EXPECT_EQ(order, (Vector<String>{"count", "count"}));
    }

    TEST_F(ControlBatchTest, ErrorAfterAStepStopsTheBatch) {
        Post(MakeRequest(1, "batch", BatchOf(Json::array({CallOf("step", FramesOf(2)), CallOf("fail"), CallOf("count")}))));
        service.Drain();

        EXPECT_EQ(RunDueFrames(), 2);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ResultCount(0), 2u);
        EXPECT_EQ(GetJsonInt(*ResultOf(0), "stopped").value_or(-1), 1);
        EXPECT_EQ(order, (Vector<String>{"fail"}));
    }

    TEST_F(ControlBatchTest, ShutdownDuringABatchAnswersItAndRunsNothingMore) {
        Post(MakeRequest(1, "batch", BatchOf(Json::array({CallOf("step", FramesOf(5)), CallOf("count")}))));
        Post(MakeRequest(2, "count"));
        service.Drain();
        ASSERT_TRUE(service.IsStepDue());

        service.Shutdown();

        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(ErrorCode(responses[0]), ControlErrors::kShuttingDown);
        EXPECT_EQ(ErrorCode(responses[1]), ControlErrors::kShuttingDown);
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
        Json calls = Json::array();
        calls.push_back(CallOf("ping"));
        calls.push_back(CallOf("echo_text"));
        calls.push_back(CallOf("fail"));
        calls.push_back(CallOf("ping"));
        Post(MakeRequest(1, "batch", BatchOf(calls)));
        Post(MakeRequest(2, "batch", BatchOf(calls, true)));
        Post(MakeRequest(3, "batch", BatchOf(Json::array({CallOf("ping")}))));
        service.Drain();

        ExpectTexts(
            raw,
            {R"json({"id":1,"ok":true,"result":{"results":[{"ok":true,"result":{"pong":true,"protocol":1}},{"ok":true,"result":{"list":3,"text":"quote \" café\n"}},{"error":{"code":"boom","message":"it failed"},"ok":false}],"stopped":2}})json",
             R"json({"id":2,"ok":true,"result":{"results":[{"ok":true,"result":{"pong":true,"protocol":1}},{"ok":true,"result":{"list":3,"text":"quote \" café\n"}},{"error":{"code":"boom","message":"it failed"},"ok":false},{"ok":true,"result":{"pong":true,"protocol":1}}]}})json",
             R"json({"id":3,"ok":true,"result":{"results":[{"ok":true,"result":{"pong":true,"protocol":1}}]}})json"});
    }

}  // namespace BECore::Tests
