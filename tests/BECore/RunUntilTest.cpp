#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameControlTest.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Time/Time.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Which clock the frames of a test come from
        CORE_ENUM(RunUntilClock, uint8_t, Manual, Real, RealPaused)

        StringView View(StringView text) {
            return text;
        }

        Json ConditionPointer(StringView pointer, Json equals) {
            Json condition = Json::object();
            condition["pointer"] = ToStdString(pointer);
            condition["equals"] = std::move(equals);
            return condition;
        }

        Json ConditionVersionMoved() {
            Json condition = Json::object();
            condition["versionMoved"] = true;
            return condition;
        }

        Json ConditionEvent(StringView name, Json equal = Json()) {
            Json event = Json::object();
            event["name"] = ToStdString(name);
            if (!equal.is_null()) {
                event["equal"] = std::move(equal);
            }
            Json condition = Json::object();
            condition["event"] = std::move(event);
            return condition;
        }

        Json RunUntilParams(Json condition, int64_t maxFrames = 0) {
            Json params = Json::object();
            params["condition"] = std::move(condition);
            if (maxFrames > 0) {
                params["maxFrames"] = maxFrames;
            }
            return params;
        }

        class RunUntilTest : public ::testing::TestWithParam<RunUntilClock> {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                CoreManager::GetTime().Reset();
                GameManager::GetInstance().Initialize();
                GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
                RegisterControlBuiltins(service);
                // Before the hooks of the game layer, so what the test does in a frame is seen by them in that frame
                service.SetFrameEndHook("test", [this] {
                    if (action && CoreManager::GetTime().GetFrameCount() == actionFrame) {
                        auto run = std::move(action);
                        action = nullptr;
                        run();
                    }
                });
                RegisterGameControl(service);
                service.SetPause(&pause);
                service.SetManualClock(GetParam() == RunUntilClock::Manual);
                pause.SetPaused(GetParam() != RunUntilClock::Real);
            }

            void TearDown() override {
                service.Shutdown();
                GameManager::GetInstance().Shutdown();
                CoreManager::GetTime().Reset();
            }

            static FakeControllableGame* Fake() {
                for (const IntrusivePtr<IGame>& game : GameManager::GetInstance().GetAll()) {
                    if (auto* fake = game->Cast<FakeControllableGame>()) {
                        return fake;
                    }
                }
                return nullptr;
            }

            /// Runs @p run at the end of the frame after which the game clock reads @p frame
            void AtFrame(uint64_t frame, MoveOnlyFunction<void()> run) {
                actionFrame = frame;
                action = std::move(run);
            }

            void SetCounterAtFrame(uint64_t frame, int counter) {
                AtFrame(frame, [counter] {
                    Fake()->counter = counter;
                    ++Fake()->version;
                });
            }

            void Post(int64_t id, StringView method, const Json& params = Json()) {
                service.Post(MakeRequest(id, method, params), [this](String text) {
                    auto parsed = ParseJson(text);
                    responses.push_back(parsed ? std::move(*parsed) : Json());
                });
            }

            /// The application loop over the pause, as the clock of the test would run it, until @p answers answers are in. Returns the kinds of the frames that ran
            Vector<FrameKind> RunFrames(size_t answers) {
                constexpr int kGuard = 1000;
                Vector<FrameKind> kinds;
                while (responses.size() < answers && static_cast<int>(kinds.size()) < kGuard) {
                    if (service.IsManualClock() && !pause.IsStepDue()) {
                        service.Drain();  // the idle cycle
                        if (!pause.IsStepDue()) {
                            break;
                        }
                    }
                    const FrameDecision frame = pause.BeginFrame();
                    switch (frame.kind) {
                        case FrameKind::Running:
                            CoreManager::GetTime().Advance(kRealFrameSeconds);
                            break;
                        case FrameKind::Stepped:
                            CoreManager::GetTime().Advance(frame.seconds);
                            break;
                        case FrameKind::Held:
                            CoreManager::GetTime().Hold();
                            break;
                    }
                    kinds.push_back(frame.kind);
                    service.Drain();
                    service.EndFrame();
                }
                return kinds;
            }

            /// Posts a `run_until` and plays frames until it is answered; the answer is `responses.back()`
            const Json* RunUntil(Json params) {
                const size_t answered = responses.size();
                Post(++lastId, "run_until", params);
                RunFrames(answered + 1);
                return Result(responses.size() - 1);
            }

            const Json* Result(size_t index) const {
                return index < responses.size() ? FindJsonMember(responses[index], "result") : nullptr;
            }

            static constexpr double kRealFrameSeconds = 1.0 / 64;

            ControlService service;
            GamePause pause;
            Vector<Json> responses;
            int64_t lastId = 0;
            uint64_t actionFrame = 0;
            MoveOnlyFunction<void()> action;
        };

        //-NoStd: gtest asks a name generator for std::string
        std::string ClockName(const ::testing::TestParamInfo<RunUntilClock>& info) {
            switch (info.param) {
                case RunUntilClock::Manual:
                    return "Manual";
                case RunUntilClock::Real:
                    return "Real";
                case RunUntilClock::RealPaused:
                    return "RealPaused";
            }
            return "Unknown";
        }

    }  // namespace

    INSTANTIATE_TEST_SUITE_P(RunUntilClocks, RunUntilTest, ::testing::Values(RunUntilClock::Manual, RunUntilClock::Real, RunUntilClock::RealPaused), ClockName);

    TEST_P(RunUntilTest, PointerIsMetAfterTheFramesItTook) {
        SetCounterAtFrame(3, 7);

        const Json* result = RunUntil(RunUntilParams(ConditionPointer("/state/counter", 7)));

        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonBool(*result, "met").value_or(false));
        EXPECT_EQ(GetJsonInt(*result, "frames").value_or(-1), 3);
        EXPECT_EQ(GetJsonString(*result, "reason").value_or(""), "pointer");
        EXPECT_EQ(GetJsonString(*result, "game").value_or(""), "FakeControllable");
        EXPECT_EQ(GetJsonInt(*result, "version").value_or(-1), 1);
        EXPECT_EQ(GetJsonInt(*result, "frame").value_or(-1), 3);
    }

    TEST_P(RunUntilTest, PointerAlreadyMetAnswersWithoutAFrame) {
        Post(1, "run_until", RunUntilParams(ConditionPointer("/state/counter", 0)));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        const Json* result = Result(0);
        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonBool(*result, "met").value_or(false));
        EXPECT_EQ(GetJsonInt(*result, "frames").value_or(-1), 0);
        EXPECT_EQ(GetJsonInt(*result, "frame").value_or(-1), 0);
        EXPECT_FALSE(service.IsStepDue());
        EXPECT_EQ(service.GetPublishObserverCount(), 0u);
    }

    TEST_P(RunUntilTest, TheLimitIsAnAnswerAndNotAnError) {
        const Json* result = RunUntil(RunUntilParams(ConditionPointer("/state/counter", 99), 4));

        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonBool(responses[0], "ok").value_or(false));
        EXPECT_FALSE(GetJsonBool(*result, "met").value_or(true));
        EXPECT_EQ(GetJsonInt(*result, "frames").value_or(-1), 4);
        EXPECT_EQ(FindJsonMember(*result, "reason"), nullptr);
        EXPECT_EQ(GetJsonInt(*result, "frame").value_or(-1), 4);
    }

    TEST_P(RunUntilTest, ConditionMetOnTheLastAllowedFrameCountsAsMet) {
        SetCounterAtFrame(2, 5);

        const Json* result = RunUntil(RunUntilParams(ConditionPointer("/state/counter", 5), 2));

        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonBool(*result, "met").value_or(false));
        EXPECT_EQ(GetJsonInt(*result, "frames").value_or(-1), 2);
    }

    TEST_P(RunUntilTest, AMissingPointerPathIsNotMetAndNotAnError) {
        const Json* result = RunUntil(RunUntilParams(ConditionPointer("/state/nothing/here", 1), 2));

        ASSERT_NE(result, nullptr);
        EXPECT_FALSE(GetJsonBool(*result, "met").value_or(true));
        EXPECT_EQ(GetJsonInt(*result, "frames").value_or(-1), 2);
    }

    TEST_P(RunUntilTest, PointerEqualityIsJsonEquality) {
        const Json* result = RunUntil(RunUntilParams(ConditionPointer("/state/counter", true), 1));

        ASSERT_NE(result, nullptr);
        EXPECT_FALSE(GetJsonBool(*result, "met").value_or(true));
    }

    TEST_P(RunUntilTest, PointerCanNameTheWholeAnswerOrTheVersion) {
        Json whole = Json::object();
        whole["game"] = "FakeControllable";
        whole["version"] = 0;
        Json state = Json::object();
        state["counter"] = 0;
        whole["state"] = state;

        const Json* result = RunUntil(RunUntilParams(ConditionPointer("", whole)));

        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonBool(*result, "met").value_or(false));
        EXPECT_EQ(GetJsonInt(*result, "frames").value_or(-1), 0);
    }

    TEST_P(RunUntilTest, VersionMovedIsMetWhenTheVersionDiffersFromTheStart) {
        SetCounterAtFrame(2, 1);

        const Json* result = RunUntil(RunUntilParams(ConditionVersionMoved()));

        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonBool(*result, "met").value_or(false));
        EXPECT_EQ(GetJsonInt(*result, "frames").value_or(-1), 2);
        EXPECT_EQ(GetJsonString(*result, "reason").value_or(""), "versionMoved");
    }

    TEST_P(RunUntilTest, VersionMovedStartsFromTheVersionAtTheCallEvenInsideABatchAfterAnAction) {
        Json inc = Json::object();
        inc["type"] = "inc";
        Json calls = Json::array();
        calls.push_back(Json{{"method", "perform_action"}, {"params", inc}});
        calls.push_back(Json{{"method", "run_until"}, {"params", RunUntilParams(ConditionVersionMoved(), 2)}});
        calls.push_back(Json{{"method", "perform_action"}, {"params", inc}});
        calls.push_back(Json{{"method", "run_until"}, {"params", RunUntilParams(ConditionVersionMoved(), 2)}});
        Json params = Json::object();
        params["calls"] = calls;

        Post(1, "batch", params);
        RunFrames(1);

        const Json* result = Result(0);
        ASSERT_NE(result, nullptr);
        const Json* results = FindJsonMember(*result, "results");
        ASSERT_NE(results, nullptr);
        ASSERT_EQ(results->size(), 4u);
        EXPECT_FALSE(GetJsonBool(Require(FindJsonMember((*results)[1], "result")), "met").value_or(true));
        EXPECT_FALSE(GetJsonBool(Require(FindJsonMember((*results)[3], "result")), "met").value_or(true));
        EXPECT_EQ(GetJsonInt(Require(FindJsonMember((*results)[3], "result")), "version").value_or(-1), 2);
    }

    TEST_P(RunUntilTest, EventIsMetWhenItIsPublished) {
        AtFrame(2, [this] { service.Publish("ping", Json::object()); });

        const Json* result = RunUntil(RunUntilParams(ConditionEvent("ping")));

        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonBool(*result, "met").value_or(false));
        EXPECT_EQ(GetJsonInt(*result, "frames").value_or(-1), 2);
        EXPECT_EQ(GetJsonString(*result, "reason").value_or(""), "event");
        EXPECT_EQ(service.GetPublishObserverCount(), 0u);
    }

    TEST_P(RunUntilTest, EventWithEqualNeedsTheMembersToMatch) {
        AtFrame(1, [this] {
            Json other = Json::object();
            other["n"] = 3;
            service.Publish("ping", other);
        });

        const Json* missed = RunUntil(RunUntilParams(ConditionEvent("ping", Json{{"n", 2}}), 2));
        ASSERT_NE(missed, nullptr);
        EXPECT_FALSE(GetJsonBool(*missed, "met").value_or(true));

        // The first wait took frames 1 and 2; the clock goes on from there
        const uint64_t frame = CoreManager::GetTime().GetFrameCount();
        AtFrame(frame + 1, [this] { service.Publish("ping", Json{{"n", 2}, {"more", "x"}}); });
        const Json* met = RunUntil(RunUntilParams(ConditionEvent("ping", Json{{"n", 2}}), 3));
        ASSERT_NE(met, nullptr);
        EXPECT_TRUE(GetJsonBool(*met, "met").value_or(false));
        EXPECT_EQ(GetJsonInt(*met, "frames").value_or(-1), 1);
    }

    TEST_P(RunUntilTest, AnEventOfAnotherNameOrBeforeTheCallDoesNotCount) {
        service.Publish("ping", Json::object());
        AtFrame(1, [this] { service.Publish("pong", Json::object()); });

        const Json* result = RunUntil(RunUntilParams(ConditionEvent("ping"), 2));

        ASSERT_NE(result, nullptr);
        EXPECT_FALSE(GetJsonBool(*result, "met").value_or(true));
    }

    TEST_P(RunUntilTest, EventNeedsNoActiveGameAndLeavesTheGameOut) {
        GameManager::GetInstance().Shutdown();
        AtFrame(1, [this] { service.Publish("ping", Json::object()); });

        const Json* result = RunUntil(RunUntilParams(ConditionEvent("ping")));

        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonBool(*result, "met").value_or(false));
        EXPECT_EQ(FindJsonMember(*result, "game"), nullptr);
        EXPECT_EQ(FindJsonMember(*result, "version"), nullptr);
    }

    TEST_P(RunUntilTest, StateChangedOfTheFrameThatEndsTheWaitCounts) {
        AtFrame(2, [] { ++Fake()->version; });  // the state_changed hook of GameControl runs after this one, in the same frame
        Json equal = Json::object();
        equal["version"] = 1;

        const Json* result = RunUntil(RunUntilParams(ConditionEvent("state_changed", equal)));

        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonBool(*result, "met").value_or(false));
        EXPECT_EQ(GetJsonInt(*result, "frames").value_or(-1), 2);
    }

    TEST_P(RunUntilTest, WithoutAControllableGamePointerAndVersionMovedFail) {
        GameManager::GetInstance().Shutdown();
        Post(1, "run_until", RunUntilParams(ConditionPointer("/state", 1)));
        Post(2, "run_until", RunUntilParams(ConditionVersionMoved()));
        service.Drain();
        EXPECT_EQ(ErrorCode(responses[0]), "no_active_game");
        EXPECT_EQ(ErrorCode(responses[1]), "no_active_game");

        GameManager::GetInstance().Initialize();
        GameManager::GetInstance().SwitchTo("FakePlain"_intern);
        Post(3, "run_until", RunUntilParams(ConditionVersionMoved()));
        service.Drain();
        EXPECT_EQ(ErrorCode(responses[2]), "not_controllable");
        EXPECT_FALSE(service.IsStepDue());
        EXPECT_EQ(service.GetPublishObserverCount(), 0u);
    }

    TEST_P(RunUntilTest, TheGameVanishingDuringTheWaitFailsTheCall) {
        AtFrame(2, [] { GameManager::GetInstance().Shutdown(); });

        RunUntil(RunUntilParams(ConditionVersionMoved(), 5));

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ErrorCode(responses[0]), "no_active_game");
    }

    TEST_P(RunUntilTest, TheAnswerCarriesTheClockOfStep) {
        const Json* result = RunUntil(RunUntilParams(ConditionPointer("/state/counter", 9), 2));

        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(GetJsonInt(*result, "frame").has_value());
        EXPECT_TRUE(GetJsonNumber(*result, "totalTime").has_value());
        EXPECT_TRUE(GetJsonNumber(*result, "deltaTime").has_value());
        EXPECT_TRUE(GetJsonNumber(*result, "fixedDeltaTime").has_value());
    }

    TEST_P(RunUntilTest, DtSetsTheLengthOfTheFramesOfTheManualClockOnly) {
        Json params = RunUntilParams(ConditionPointer("/state/counter", 9), 2);
        params["dt"] = 0.25;

        const Json* result = RunUntil(params);

        ASSERT_NE(result, nullptr);
        const double total = GetJsonNumber(*result, "totalTime").value_or(-1.0);
        if (GetParam() == RunUntilClock::Manual) {
            EXPECT_DOUBLE_EQ(total, 0.5);
        } else if (GetParam() == RunUntilClock::RealPaused) {
            EXPECT_DOUBLE_EQ(total, 0.5);  // a paused real clock takes the steps from the pause, which the call asked for with this dt
        } else {
            EXPECT_NEAR(total, 2 * kRealFrameSeconds, 1e-9);
        }
    }

    TEST_P(RunUntilTest, EveryFrameOfTheWaitIsASteppedOrRunningFrameNeverAnExtraHeldOne) {
        const size_t answered = responses.size();
        Post(1, "run_until", RunUntilParams(ConditionPointer("/state/counter", 9), 3));
        const Vector<FrameKind> kinds = RunFrames(answered + 1);

        const size_t held = static_cast<size_t>(std::count(kinds.begin(), kinds.end(), FrameKind::Held));
        // A paused real clock holds the frame that drained the request; nothing else is held. The manual clock drains between frames
        EXPECT_EQ(held, GetParam() == RunUntilClock::RealPaused ? 1u : 0u);
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 3u);
    }

    TEST_P(RunUntilTest, TheRequestsBehindAWaitingRunUntilWait) {
        SetCounterAtFrame(3, 4);
        Post(1, "run_until", RunUntilParams(ConditionPointer("/state/counter", 4)));
        Post(2, "ping");

        RunFrames(1);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(GetJsonInt(responses[0], "id").value_or(-1), 1);

        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(GetJsonInt(responses[1], "id").value_or(-1), 2);
    }

    TEST_P(RunUntilTest, ShutdownDuringTheWaitAnswersShuttingDownAndGivesTheObserverBack) {
        Post(1, "run_until", RunUntilParams(ConditionEvent("never")));
        service.Drain();
        ASSERT_EQ(responses.size(), 0u);
        EXPECT_EQ(service.GetPublishObserverCount(), 1u);

        service.Shutdown();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(ErrorCode(responses[0]), "shutting_down");
        EXPECT_EQ(service.GetPublishObserverCount(), 0u);
    }

    TEST_P(RunUntilTest, ADroppedServiceGivesTheObserverBackToo) {
        auto owner = MakeUnique<ControlService>();
        RegisterGameControl(*owner);
        owner->Post(MakeRequest(1, "run_until", RunUntilParams(ConditionEvent("never"))), [](String) {});
        owner->Drain();
        ASSERT_EQ(owner->GetPublishObserverCount(), 1u);

        owner.reset();  // no Shutdown: the pending call dies with the service
        SUCCEED();
    }

    TEST_P(RunUntilTest, InsideABatchTheNextCallRunsAfterTheWait) {
        SetCounterAtFrame(2, 2);
        Json calls = Json::array();
        calls.push_back(Json{{"method", "run_until"}, {"params", RunUntilParams(ConditionPointer("/state/counter", 2), 5)}});
        calls.push_back(Json{{"method", "get_state"}});
        Json params = Json::object();
        params["calls"] = calls;

        Post(1, "batch", params);
        Post(2, "ping");
        RunFrames(1);
        service.Drain();

        ASSERT_EQ(responses.size(), 2u);
        const Json* results = FindJsonMember(Require(Result(0)), "results");
        ASSERT_NE(results, nullptr);
        ASSERT_EQ(results->size(), 2u);
        const Json* waited = FindJsonMember((*results)[0], "result");
        ASSERT_NE(waited, nullptr);
        EXPECT_TRUE(GetJsonBool(*waited, "met").value_or(false));
        EXPECT_EQ(GetJsonInt(*waited, "frames").value_or(-1), 2);
        const Json* state = FindJsonMember(Require(FindJsonMember((*results)[1], "result")), "state");
        ASSERT_NE(state, nullptr);
        EXPECT_EQ(GetJsonInt(*state, "counter").value_or(-1), 2);
        EXPECT_EQ(GetJsonInt(responses[1], "id").value_or(-1), 2);
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 2u);
    }

    TEST_P(RunUntilTest, AStepAfterARunUntilInsideABatchWaitsForExactlyItsFrames) {
        Json calls = Json::array();
        calls.push_back(Json{{"method", "run_until"}, {"params", RunUntilParams(ConditionPointer("/state/counter", 9), 2)}});
        calls.push_back(Json{{"method", "step"}, {"params", Json{{"frames", 3}}}});
        Json params = Json::object();
        params["calls"] = calls;

        Post(1, "batch", params);
        const Vector<FrameKind> kinds = RunFrames(1);

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 5u);
        // A step that starts in a continuation does not wait for a held frame after its own: only the frame that drained the batch is held
        EXPECT_EQ(std::count(kinds.begin(), kinds.end(), FrameKind::Held), GetParam() == RunUntilClock::RealPaused ? 1 : 0);
        EXPECT_EQ(kinds.size(), GetParam() == RunUntilClock::RealPaused ? 6u : 5u);
    }

    TEST_P(RunUntilTest, InvalidConditionsAndLimitsAreInvalidParamsAndNothingRuns) {
        const Vector<Json> bad = {
            Json::object(),
            Json{{"condition", 1}},
            Json{{"condition", Json::object()}},
            Json{{"condition", Json{{"pointer", "/a"}}}},
            Json{{"condition", Json{{"pointer", 1}, {"equals", 1}}}},
            Json{{"condition", Json{{"pointer", "a"}, {"equals", 1}}}},
            Json{{"condition", Json{{"pointer", "/a~"}, {"equals", 1}}}},
            Json{{"condition", Json{{"pointer", "/a"}, {"equals", 1}, {"extra", 1}}}},
            Json{{"condition", Json{{"pointer", "/a"}, {"equals", 1}, {"versionMoved", true}}}},
            Json{{"condition", Json{{"versionMoved", false}}}},
            Json{{"condition", Json{{"versionMoved", true}, {"equals", 1}}}},
            Json{{"condition", Json{{"event", "x"}}}},
            Json{{"condition", Json{{"event", Json{{"name", ""}}}}}},
            Json{{"condition", Json{{"event", Json{{"name", "x"}, {"equal", 1}}}}}},
            Json{{"condition", Json{{"event", Json{{"name", "x"}, {"other", 1}}}}}},
            Json{{"condition", ConditionVersionMoved()}, {"maxFrames", 0}},
            Json{{"condition", ConditionVersionMoved()}, {"maxFrames", 1.5}},
            Json{{"condition", ConditionVersionMoved()}, {"maxFrames", "3"}},
            Json{{"condition", ConditionVersionMoved()}, {"maxFrames", static_cast<int64_t>(kMaxStepFrames) + 1}},
            Json{{"condition", ConditionEvent("x")}, {"maxFrames", 0}},
            Json{{"condition", ConditionVersionMoved()}, {"dt", -1}},
            Json{{"condition", ConditionVersionMoved()}, {"dt", 1.0}},
            Json{{"condition", ConditionVersionMoved()}, {"dt", "x"}},
        };

        int64_t id = 0;
        for (const Json& params : bad) {
            Post(++id, "run_until", params);
        }
        service.Drain();

        ASSERT_EQ(responses.size(), bad.size());
        for (size_t i = 0; i < bad.size(); ++i) {
            EXPECT_EQ(ErrorCode(responses[i]), "invalid_params") << DumpJson(bad[i]).c_str();
        }
        EXPECT_FALSE(service.IsStepDue());
        EXPECT_EQ(service.GetPublishObserverCount(), 0u);
    }

    TEST_P(RunUntilTest, IsListedWithADescription) {
        bool found = false;
        for (const ControlMethodListing& listing : service.GetMethods()) {
            if (listing.name == "run_until") {
                found = true;
                ASSERT_NE(listing.info, nullptr);
                EXPECT_FALSE(listing.info->summary.empty());
                EXPECT_EQ(listing.info->params.size(), 3u);
            }
        }
        EXPECT_TRUE(found);
    }

}  // namespace BECore::Tests
