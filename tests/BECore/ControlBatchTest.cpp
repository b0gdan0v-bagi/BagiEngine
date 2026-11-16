#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Time/Time.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        StringView View(StringView text) {
            return text;
        }

        /// A request text built through Json, so that the test data is always valid
        String MakeRequest(int64_t id, StringView method, const Json& params = Json()) {
            Json request = Json::object();
            request["id"] = id;
            request["method"] = ToStdString(method);
            if (!params.is_null()) {
                request["params"] = params;
            }
            return DumpJson(request);
        }

        bool IsOk(const Json& response) {
            return GetJsonBool(response, "ok").value_or(false);
        }

        int64_t IdOf(const Json& response) {
            return GetJsonInt(response, "id").value_or(-1);
        }

        String ErrorCode(const Json& response) {
            const Json* error = FindJsonMember(response, "error");
            const auto code = error != nullptr ? GetJsonString(*error, "code") : Optional<String>();
            return code ? *code : String();
        }

        Json FramesOf(int64_t frames) {
            Json params = Json::object();
            params["frames"] = frames;
            return params;
        }

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
                service.RegisterHandler("silent", [this](ControlCall&) { order.push_back("silent"); });
            }

            void TearDown() override {
                CoreManager::GetTime().Reset();
            }

            ControlReplyFn Collect() {
                return [this](String text) {
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
                    auto answer = service.RunNested(outer, View(step.method), step.params, [this](ControlCall& again, ControlAnswer later) {
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

            ControlService service;
            Vector<Json> responses;
            Vector<String> order;
            Vector<NestedStep> plan;
            Vector<ControlAnswer> answers;
            size_t next = 0;
        };

    }  // namespace

    TEST_F(ControlBatchTest, NestedCallThatAnswersAtOnceNeedsNoFrame) {
        plan = {{"ping"}, {"count"}};
        Post(MakeRequest(1, "outer"));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_TRUE(IsOk(responses[0]));
        EXPECT_EQ(GetJsonInt(*FindJsonMember(responses[0], "result"), "ran").value_or(-1), 2);
        EXPECT_FALSE(service.IsStepDue());
        ASSERT_EQ(answers.size(), 2u);
        ASSERT_TRUE(answers[0].has_value());
        EXPECT_TRUE(GetJsonBool(*answers[0], "pong").value_or(false));
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
        EXPECT_EQ(GetJsonInt(*FindJsonMember(responses[0], "result"), "ran").value_or(-1), 3);
        ASSERT_EQ(answers.size(), 3u);
        ASSERT_TRUE(answers[1].has_value());
        EXPECT_NE(FindJsonMember(*answers[1], "frame"), nullptr);  // the answer of `step`
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
        EXPECT_EQ(GetJsonInt(*FindJsonMember(responses[0], "result"), "ran").value_or(-1), 3);
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

}  // namespace BECore::Tests
