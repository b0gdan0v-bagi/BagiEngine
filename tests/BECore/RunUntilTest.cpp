#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameControlTest.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/ClassMeta.h>
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

        /// The conditions and params below are JSON texts; @p equalsText and @p equalText are JSON values, and a name or a pointer holds nothing that needs an escape
        String ConditionPointer(StringView pointer, StringView equalsText) {
            return Format(R"({{"equals":{},"pointer":"{}"}})", equalsText, pointer);
        }

        String ConditionVersionMoved() {
            return String(R"({"versionMoved":true})");
        }

        String ConditionEvent(StringView name, StringView equalText = {}) {
            if (equalText.empty()) {
                return Format(R"({{"event":{{"name":"{}"}}}})", name);
            }
            return Format(R"({{"event":{{"equal":{},"name":"{}"}}}})", equalText, name);
        }

        String RunUntilParams(StringView conditionText, int64_t maxFrames = 0) {
            if (maxFrames > 0) {
                return Format(R"({{"condition":{},"maxFrames":{}}})", conditionText, maxFrames);
            }
            return Format(R"({{"condition":{}}})", conditionText);
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

            void Post(int64_t id, StringView method, StringView paramsText = {}) {
                service.Post(MakeRequestText(id, method, paramsText), [this](const String& text) { responses.push_back(ControlReply(StringView(text.c_str(), text.size()))); });
            }

            /// Publishes the event @p name with the JSON object @p dataText as its data
            void PublishText(StringView name, StringView dataText = R"({})") {
                service.Publish(name, [dataText](JsonWriter& writer) { writer.Raw(dataText); });
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
            JsonView RunUntil(StringView params) {
                const size_t answered = responses.size();
                Post(++lastId, "run_until", params);
                RunFrames(answered + 1);
                return Result(responses.size() - 1);
            }

            /// The `result` of the answer @p index; not valid when there is none
            JsonView Result(size_t index) const {
                return index < responses.size() ? responses[index].Result() : JsonView();
            }

            static constexpr double kRealFrameSeconds = 1.0 / 64;

            ControlService service;
            GamePause pause;
            Vector<ControlReply> responses;
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

        const JsonView result = RunUntil(RunUntilParams(ConditionPointer("/state/counter", "7")));

        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(BoolOf(result, "met").value_or(false));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 3);
        EXPECT_EQ(StringOf(result, "reason"), "pointer");
        EXPECT_EQ(StringOf(result, "game"), "FakeControllable");
        EXPECT_EQ(IntOf(result, "version").value_or(-1), 1);
        EXPECT_EQ(IntOf(result, "frame").value_or(-1), 3);
    }

    TEST_P(RunUntilTest, PointerAlreadyMetAnswersWithoutAFrame) {
        Post(1, "run_until", RunUntilParams(ConditionPointer("/state/counter", "0")));
        service.Drain();

        ASSERT_EQ(responses.size(), 1u);
        const JsonView result = Result(0);
        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(BoolOf(result, "met").value_or(false));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 0);
        EXPECT_EQ(IntOf(result, "frame").value_or(-1), 0);
        EXPECT_FALSE(service.IsStepDue());
        EXPECT_EQ(service.GetPublishObserverCount(), 0u);
    }

    TEST_P(RunUntilTest, TheLimitIsAnAnswerAndNotAnError) {
        const JsonView result = RunUntil(RunUntilParams(ConditionPointer("/state/counter", "99"), 4));

        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(responses[0].IsOk());
        EXPECT_FALSE(BoolOf(result, "met").value_or(true));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 4);
        EXPECT_FALSE(MemberOf(result, "reason").IsValid());
        EXPECT_EQ(IntOf(result, "frame").value_or(-1), 4);
    }

    TEST_P(RunUntilTest, ConditionMetOnTheLastAllowedFrameCountsAsMet) {
        SetCounterAtFrame(2, 5);

        const JsonView result = RunUntil(RunUntilParams(ConditionPointer("/state/counter", "5"), 2));

        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(BoolOf(result, "met").value_or(false));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 2);
    }

    TEST_P(RunUntilTest, AMissingPointerPathIsNotMetAndNotAnError) {
        const JsonView result = RunUntil(RunUntilParams(ConditionPointer("/state/nothing/here", "1"), 2));

        ASSERT_TRUE(result.IsObject());
        EXPECT_FALSE(BoolOf(result, "met").value_or(true));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 2);
    }

    TEST_P(RunUntilTest, PointerEqualityIsJsonEquality) {
        const JsonView result = RunUntil(RunUntilParams(ConditionPointer("/state/counter", "true"), 1));

        ASSERT_TRUE(result.IsObject());
        EXPECT_FALSE(BoolOf(result, "met").value_or(true));
    }

    TEST_P(RunUntilTest, EqualsComparesNumbersByValue) {
        Fake()->rawState = R"({"n":1.0})";

        const JsonView result = RunUntil(RunUntilParams(ConditionPointer("/state/n", "1")));

        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(BoolOf(result, "met").value_or(false));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 0);
    }

    TEST_P(RunUntilTest, ObjectEqualsIgnoresTheOrderOfKeys) {
        Fake()->rawState = R"({"a":1,"b":{"c":2,"d":[1,2]}})";

        // Written in another order than the state: the keys are matched by name
        const JsonView result = RunUntil(RunUntilParams(ConditionPointer("/state", R"({"b":{"d":[1,2],"c":2},"a":1})")));

        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(BoolOf(result, "met").value_or(false));
    }

    TEST_P(RunUntilTest, AStringEqualsByItsDecodedText) {
        Fake()->rawState = R"({"s":"\u0041\n","k\u0041":1})";

        const JsonView text = RunUntil(RunUntilParams(ConditionPointer("/state/s", R"("A\n")")));
        ASSERT_TRUE(text.IsObject());
        EXPECT_TRUE(BoolOf(text, "met").value_or(false));

        const JsonView key = RunUntil(RunUntilParams(ConditionPointer("/state/kA", "1")));
        ASSERT_TRUE(key.IsObject());
        EXPECT_TRUE(BoolOf(key, "met").value_or(false));
    }

    TEST_P(RunUntilTest, ANumberPastADoubleInEqualsIsAValueAndNotAnError) {
        Post(1, "run_until", R"({"condition":{"pointer":"/state/counter","equals":1e999},"maxFrames":1})");
        RunFrames(1);

        const JsonView result = Result(0);
        ASSERT_TRUE(result.IsObject());
        EXPECT_FALSE(BoolOf(result, "met").value_or(true));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 1);
    }

    TEST_P(RunUntilTest, TheDocumentIsWrittenOncePerVersion) {

        const JsonView result = RunUntil(RunUntilParams(ConditionPointer("/state/counter", "9"), 3));

        ASSERT_TRUE(result.IsObject());
        EXPECT_FALSE(BoolOf(result, "met").value_or(true));
        EXPECT_EQ(Fake()->writeStateCalls, 1);
    }

    TEST_P(RunUntilTest, AStateThatFailsToWriteFailsThePointerWait) {
        Fake()->failWrites = true;

        RunUntil(RunUntilParams(ConditionPointer("/state/counter", "0"), 2));

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].ErrorCode(), "write_failed");
        EXPECT_EQ(service.GetPublishObserverCount(), 0u);
    }

    TEST_P(RunUntilTest, PointerCanNameTheWholeAnswerOrTheVersion) {
        const JsonView result = RunUntil(RunUntilParams(ConditionPointer("", R"({"game":"FakeControllable","state":{"counter":0},"version":0})")));

        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(BoolOf(result, "met").value_or(false));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 0);
    }

    TEST_P(RunUntilTest, VersionMovedIsMetWhenTheVersionDiffersFromTheStart) {
        SetCounterAtFrame(2, 1);

        const JsonView result = RunUntil(RunUntilParams(ConditionVersionMoved()));

        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(BoolOf(result, "met").value_or(false));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 2);
        EXPECT_EQ(StringOf(result, "reason"), "versionMoved");
    }

    TEST_P(RunUntilTest, VersionMovedStartsFromTheVersionAtTheCallEvenInsideABatchAfterAnAction) {
        const String moved = RunUntilParams(ConditionVersionMoved(), 2);
        const String params = Format(
            R"({{"calls":[{{"method":"perform_action","params":{{"type":"inc"}}}},{{"method":"run_until","params":{}}},{{"method":"perform_action","params":{{"type":"inc"}}}},{{"method":"run_until","params":{}}}]}})",
            moved, moved);

        Post(1, "batch", params);
        RunFrames(1);

        const JsonView result = Result(0);
        ASSERT_TRUE(result.IsObject());
        const JsonView results = MemberOf(result, "results");
        ASSERT_TRUE(results.IsArray());
        ASSERT_EQ(results.Size(), 4u);
        EXPECT_FALSE(BoolOf(MemberOf(ItemAt(results, 1), "result"), "met").value_or(true));
        EXPECT_FALSE(BoolOf(MemberOf(ItemAt(results, 3), "result"), "met").value_or(true));
        EXPECT_EQ(IntOf(MemberOf(ItemAt(results, 3), "result"), "version").value_or(-1), 2);
    }

    TEST_P(RunUntilTest, EventIsMetWhenItIsPublished) {
        AtFrame(2, [this] { PublishText("ping"); });

        const JsonView result = RunUntil(RunUntilParams(ConditionEvent("ping")));

        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(BoolOf(result, "met").value_or(false));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 2);
        EXPECT_EQ(StringOf(result, "reason"), "event");
        EXPECT_EQ(service.GetPublishObserverCount(), 0u);
    }

    TEST_P(RunUntilTest, EventWithEqualNeedsTheMembersToMatch) {
        AtFrame(1, [this] { PublishText("ping", R"({"n":3})"); });

        const JsonView missed = RunUntil(RunUntilParams(ConditionEvent("ping", R"({"n":2})"), 2));
        ASSERT_TRUE(missed.IsObject());
        EXPECT_FALSE(BoolOf(missed, "met").value_or(true));

        // The first wait took frames 1 and 2; the clock goes on from there
        const uint64_t frame = CoreManager::GetTime().GetFrameCount();
        AtFrame(frame + 1, [this] { PublishText("ping", R"({"more":"x","n":2})"); });
        const JsonView met = RunUntil(RunUntilParams(ConditionEvent("ping", R"({"n":2})"), 3));
        ASSERT_TRUE(met.IsObject());
        EXPECT_TRUE(BoolOf(met, "met").value_or(false));
        EXPECT_EQ(IntOf(met, "frames").value_or(-1), 1);
    }

    TEST_P(RunUntilTest, EventEqualComparesNestedValuesAndIgnoresOtherMembers) {
        AtFrame(1, [this] { PublishText("ping", R"({"extra":true,"n":{"a":[1,2]}})"); });

        const JsonView result = RunUntil(RunUntilParams(ConditionEvent("ping", R"({"n":{"a":[1,2.0]}})"), 3));

        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(BoolOf(result, "met").value_or(false));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 1);
    }

    TEST_P(RunUntilTest, AnEventOfAnotherNameOrBeforeTheCallDoesNotCount) {
        PublishText("ping");
        AtFrame(1, [this] { PublishText("pong"); });

        const JsonView result = RunUntil(RunUntilParams(ConditionEvent("ping"), 2));

        ASSERT_TRUE(result.IsObject());
        EXPECT_FALSE(BoolOf(result, "met").value_or(true));
    }

    TEST_P(RunUntilTest, EventNeedsNoActiveGameAndLeavesTheGameOut) {
        GameManager::GetInstance().Shutdown();
        AtFrame(1, [this] { PublishText("ping"); });

        const JsonView result = RunUntil(RunUntilParams(ConditionEvent("ping")));

        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(BoolOf(result, "met").value_or(false));
        EXPECT_FALSE(MemberOf(result, "game").IsValid());
        EXPECT_FALSE(MemberOf(result, "version").IsValid());
    }

    TEST_P(RunUntilTest, StateChangedOfTheFrameThatEndsTheWaitCounts) {
        AtFrame(2, [] { ++Fake()->version; });  // the state_changed hook of GameControl runs after this one, in the same frame

        const JsonView result = RunUntil(RunUntilParams(ConditionEvent("state_changed", R"({"version":1})")));

        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(BoolOf(result, "met").value_or(false));
        EXPECT_EQ(IntOf(result, "frames").value_or(-1), 2);
    }

    TEST_P(RunUntilTest, WithoutAControllableGamePointerAndVersionMovedFail) {
        GameManager::GetInstance().Shutdown();
        Post(1, "run_until", RunUntilParams(ConditionPointer("/state", "1")));
        Post(2, "run_until", RunUntilParams(ConditionVersionMoved()));
        service.Drain();
        EXPECT_EQ(responses[0].ErrorCode(), "no_active_game");
        EXPECT_EQ(responses[1].ErrorCode(), "no_active_game");

        GameManager::GetInstance().Initialize();
        GameManager::GetInstance().SwitchTo("FakePlain"_intern);
        Post(3, "run_until", RunUntilParams(ConditionVersionMoved()));
        service.Drain();
        EXPECT_EQ(responses[2].ErrorCode(), "not_controllable");
        EXPECT_FALSE(service.IsStepDue());
        EXPECT_EQ(service.GetPublishObserverCount(), 0u);
    }

    TEST_P(RunUntilTest, TheGameVanishingDuringTheWaitFailsTheCall) {
        AtFrame(2, [] { GameManager::GetInstance().Shutdown(); });

        RunUntil(RunUntilParams(ConditionVersionMoved(), 5));

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].ErrorCode(), "no_active_game");
    }

    TEST_P(RunUntilTest, TheAnswerCarriesTheClockOfStep) {
        const JsonView result = RunUntil(RunUntilParams(ConditionPointer("/state/counter", "9"), 2));

        ASSERT_TRUE(result.IsObject());
        EXPECT_TRUE(IntOf(result, "frame").has_value());
        EXPECT_TRUE(MemberOf(result, "totalTime").AsDouble().has_value());
        EXPECT_TRUE(MemberOf(result, "deltaTime").AsDouble().has_value());
        EXPECT_TRUE(MemberOf(result, "fixedDeltaTime").AsDouble().has_value());
    }

    TEST_P(RunUntilTest, DtSetsTheLengthOfTheFramesOfTheManualClockOnly) {
        const String params = Format(R"({{"condition":{},"dt":0.25,"maxFrames":2}})", ConditionPointer("/state/counter", "9"));

        const JsonView result = RunUntil(params);

        ASSERT_TRUE(result.IsObject());
        const double total = MemberOf(result, "totalTime").AsDouble().value_or(-1.0);
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
        Post(1, "run_until", RunUntilParams(ConditionPointer("/state/counter", "9"), 3));
        const Vector<FrameKind> kinds = RunFrames(answered + 1);

        const size_t held = static_cast<size_t>(std::count(kinds.begin(), kinds.end(), FrameKind::Held));
        // A paused real clock holds the frame that drained the request; nothing else is held. The manual clock drains between frames
        EXPECT_EQ(held, GetParam() == RunUntilClock::RealPaused ? 1u : 0u);
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 3u);
    }

    TEST_P(RunUntilTest, TheRequestsBehindAWaitingRunUntilWait) {
        SetCounterAtFrame(3, 4);
        Post(1, "run_until", RunUntilParams(ConditionPointer("/state/counter", "4")));
        Post(2, "ping");

        RunFrames(1);
        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].IdText(), "1");

        service.Drain();
        ASSERT_EQ(responses.size(), 2u);
        EXPECT_EQ(responses[1].IdText(), "2");
    }

    TEST_P(RunUntilTest, ShutdownDuringTheWaitAnswersShuttingDownAndGivesTheObserverBack) {
        Post(1, "run_until", RunUntilParams(ConditionEvent("never")));
        service.Drain();
        ASSERT_EQ(responses.size(), 0u);
        EXPECT_EQ(service.GetPublishObserverCount(), 1u);

        service.Shutdown();

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(responses[0].ErrorCode(), "shutting_down");
        EXPECT_EQ(service.GetPublishObserverCount(), 0u);
    }

    TEST_P(RunUntilTest, ADroppedServiceGivesTheObserverBackToo) {
        auto owner = MakeUnique<ControlService>();
        RegisterGameControl(*owner);
        owner->Post(MakeRequestText(1, "run_until", RunUntilParams(ConditionEvent("never"))), [](String) {});
        owner->Drain();
        ASSERT_EQ(owner->GetPublishObserverCount(), 1u);

        owner.reset();  // no Shutdown: the pending call dies with the service
        SUCCEED();
    }

    TEST_P(RunUntilTest, InsideABatchTheNextCallRunsAfterTheWait) {
        SetCounterAtFrame(2, 2);
        const String params = Format(R"({{"calls":[{{"method":"run_until","params":{}}},{{"method":"get_state"}}]}})", RunUntilParams(ConditionPointer("/state/counter", "2"), 5));

        Post(1, "batch", params);
        Post(2, "ping");
        RunFrames(1);
        service.Drain();

        ASSERT_EQ(responses.size(), 2u);
        const JsonView results = MemberOf(Result(0), "results");
        ASSERT_TRUE(results.IsArray());
        ASSERT_EQ(results.Size(), 2u);
        const JsonView waited = MemberOf(ItemAt(results, 0), "result");
        ASSERT_TRUE(waited.IsObject());
        EXPECT_TRUE(BoolOf(waited, "met").value_or(false));
        EXPECT_EQ(IntOf(waited, "frames").value_or(-1), 2);
        const JsonView state = MemberOf(MemberOf(ItemAt(results, 1), "result"), "state");
        ASSERT_TRUE(state.IsObject());
        EXPECT_EQ(IntOf(state, "counter").value_or(-1), 2);
        EXPECT_EQ(responses[1].IdText(), "2");
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 2u);
    }

    TEST_P(RunUntilTest, AStepAfterARunUntilInsideABatchWaitsForExactlyItsFrames) {
        const String params = Format(R"({{"calls":[{{"method":"run_until","params":{}}},{{"method":"step","params":{{"frames":3}}}}]}})", RunUntilParams(ConditionPointer("/state/counter", "9"), 2));

        Post(1, "batch", params);
        const Vector<FrameKind> kinds = RunFrames(1);

        ASSERT_EQ(responses.size(), 1u);
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 5u);
        // A step that starts in a continuation does not wait for a held frame after its own: only the frame that drained the batch is held
        EXPECT_EQ(std::count(kinds.begin(), kinds.end(), FrameKind::Held), GetParam() == RunUntilClock::RealPaused ? 1 : 0);
        EXPECT_EQ(kinds.size(), GetParam() == RunUntilClock::RealPaused ? 6u : 5u);
    }

    TEST_P(RunUntilTest, InvalidConditionsAndLimitsAreInvalidParamsAndNothingRuns) {
        const String moved = ConditionVersionMoved();
        const Vector<String> bad = {
            "{}",
            R"({"condition":1})",
            R"({"condition":{}})",
            R"({"condition":{"pointer":"/a"}})",
            R"({"condition":{"pointer":1,"equals":1}})",
            R"({"condition":{"pointer":"a","equals":1}})",
            R"({"condition":{"pointer":"/a~","equals":1}})",
            R"({"condition":{"pointer":"/a~2","equals":1}})",
            R"({"condition":{"equals":1,"extra":1,"pointer":"/a"}})",
            R"({"condition":{"equals":1,"pointer":"/a","versionMoved":true}})",
            R"({"condition":{"versionMoved":false}})",
            R"({"condition":{"equals":1,"versionMoved":true}})",
            R"({"condition":{"event":"x"}})",
            R"({"condition":{"event":{"name":""}}})",
            R"({"condition":{"event":{"equal":1,"name":"x"}}})",
            R"({"condition":{"event":{"name":"x","other":1}}})",
            Format(R"({{"condition":{},"maxFrames":0}})", moved),
            Format(R"({{"condition":{},"maxFrames":1.5}})", moved),
            Format(R"({{"condition":{},"maxFrames":"3"}})", moved),
            Format(R"({{"condition":{},"maxFrames":{}}})", moved, static_cast<int64_t>(kMaxStepFrames) + 1),
            Format(R"({{"condition":{},"maxFrames":0}})", ConditionEvent("x")),
            Format(R"({{"condition":{},"dt":-1}})", moved),
            Format(R"({{"condition":{},"dt":1.0}})", moved),
            Format(R"({{"condition":{},"dt":"x"}})", moved),
        };

        int64_t id = 0;
        for (const String& params : bad) {
            Post(++id, "run_until", params);
        }
        service.Drain();

        ASSERT_EQ(responses.size(), bad.size());
        for (size_t i = 0; i < bad.size(); ++i) {
            EXPECT_EQ(responses[i].ErrorCode(), "invalid_params") << bad[i].c_str();
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
