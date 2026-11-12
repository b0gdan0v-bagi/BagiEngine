#include "TimeTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Time/Time.h>
#include <Events/SceneEvents.h>
#include <Generated/TimeTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    void SpeedProbe::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &SpeedProbe::OnUpdate>(this);
        SubscribeInScene<SceneEvents::SceneFixedUpdateEvent, &SpeedProbe::OnFixedUpdate>(this);
    }

    void SpeedProbe::OnUpdate(const SceneEvents::SceneUpdateEvent& event) {
        distance += kSpeed * event.deltaTime;
        lastDeltaTime = event.deltaTime;
        ++updateCount;
        updateOrder = ++eventOrder;
    }

    void SpeedProbe::OnFixedUpdate(const SceneEvents::SceneFixedUpdateEvent& event) {
        fixedDistance += kSpeed * event.deltaTime;
        ++fixedStepCount;
        lastFixedOrder = ++eventOrder;
    }

    namespace {

        // Frame times and the fixed step are powers of two, so the sums below are exact in floating point
        constexpr double kFixedStep = 1.0 / 64.0;
        constexpr int kFrameRates[] = {32, 64, 128, 256};

        constexpr const char* kProbeSceneXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="A"><nodes>
    <node name="Probe"><components>
      <component type="SpeedProbe"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        /// Advances @p time by one second of fake time in frames of 1 / @p framesPerSecond seconds.
        template <typename OnFrame>
        void RunOneSecond(Time& time, int framesPerSecond, OnFrame&& onFrame) {
            const double frameTime = 1.0 / framesPerSecond;
            for (int frame = 0; frame < framesPerSecond; ++frame) {
                time.Advance(frameTime);
                onFrame();
            }
        }

    }  // namespace

    static_assert(Time::kDefaultFixedDeltaTime > 0.0);
    static_assert(Time::kMaxFrameTime > Time::kDefaultFixedDeltaTime);

    TEST(TimeTest, CompileTimeChecks) {
        SUCCEED();
    }

    TEST(TimeTest, StartsAtZero) {
        Time time;

        EXPECT_EQ(time.GetDeltaTime(), 0.0f);
        EXPECT_EQ(time.GetTotalTime(), 0.0);
        EXPECT_EQ(time.GetFrameCount(), 0u);
        EXPECT_EQ(time.GetFixedStepCount(), 0u);
        EXPECT_FLOAT_EQ(time.GetFixedDeltaTime(), static_cast<float>(Time::kDefaultFixedDeltaTime));
    }

    TEST(TimeTest, AdvanceUpdatesDeltaTotalAndFrameCount) {
        Time time;

        time.Advance(0.125);
        EXPECT_EQ(time.GetDeltaTime(), 0.125f);
        EXPECT_EQ(time.GetTotalTime(), 0.125);
        EXPECT_EQ(time.GetFrameCount(), 1u);

        time.Advance(0.0625);
        EXPECT_EQ(time.GetDeltaTime(), 0.0625f);
        EXPECT_EQ(time.GetTotalTime(), 0.1875);
        EXPECT_EQ(time.GetFrameCount(), 2u);
    }

    TEST(TimeTest, LongFrameIsClampedAndTotalCountsTheClampedValue) {
        Time time;

        time.Advance(5.0);

        EXPECT_EQ(time.GetDeltaTime(), static_cast<float>(Time::kMaxFrameTime));
        EXPECT_EQ(time.GetTotalTime(), Time::kMaxFrameTime);
    }

    TEST(TimeTest, NegativeFrameCountsAsZero) {
        Time time;

        time.Advance(-1.0);

        EXPECT_EQ(time.GetDeltaTime(), 0.0f);
        EXPECT_EQ(time.GetTotalTime(), 0.0);
        EXPECT_EQ(time.GetFrameCount(), 1u);
    }

    TEST(TimeTest, ResetReturnsToTheStateBeforeTheFirstFrame) {
        Time time;
        time.SetFixedDeltaTime(kFixedStep);
        time.Advance(0.25);

        time.Reset();

        EXPECT_EQ(time.GetDeltaTime(), 0.0f);
        EXPECT_EQ(time.GetTotalTime(), 0.0);
        EXPECT_EQ(time.GetFrameCount(), 0u);
        EXPECT_EQ(time.GetFixedStepCount(), 0u);
        EXPECT_FLOAT_EQ(time.GetFixedDeltaTime(), static_cast<float>(kFixedStep));
    }

    TEST(TimeTest, FixedStepRemainderCarriesOverToTheNextFrame) {
        Time time;
        time.SetFixedDeltaTime(kFixedStep);

        time.Advance(kFixedStep / 2);
        EXPECT_EQ(time.GetFixedStepCount(), 0u);

        time.Advance(kFixedStep / 2);
        EXPECT_EQ(time.GetFixedStepCount(), 1u);

        time.Advance(kFixedStep * 2.5);
        EXPECT_EQ(time.GetFixedStepCount(), 2u);

        time.Advance(kFixedStep / 2);
        EXPECT_EQ(time.GetFixedStepCount(), 1u);
    }

    TEST(TimeTest, FixedStepsPerSecondDoNotDependOnTheFrameRate) {
        for (const int framesPerSecond : kFrameRates) {
            Time time;
            time.SetFixedDeltaTime(kFixedStep);

            uint32_t steps = 0;
            RunOneSecond(time, framesPerSecond, [&] { steps += time.GetFixedStepCount(); });

            EXPECT_EQ(steps, 64u) << framesPerSecond << " frames per second";
            EXPECT_EQ(time.GetTotalTime(), 1.0) << framesPerSecond << " frames per second";
            EXPECT_EQ(time.GetFrameCount(), static_cast<uint64_t>(framesPerSecond));
        }
    }

    TEST(TimeTest, ClampedFrameQueuesALimitedNumberOfFixedSteps) {
        Time time;
        time.SetFixedDeltaTime(kFixedStep);

        time.Advance(60.0);

        EXPECT_EQ(time.GetFixedStepCount(), static_cast<uint32_t>(Time::kMaxFrameTime / kFixedStep));
    }

    TEST(TimeTest, HoldLeavesTotalTimeAndFrameCountAndZeroesTheFrame) {
        Time time;
        time.SetFixedDeltaTime(kFixedStep);
        time.Advance(kFixedStep * 2.5);
        ASSERT_EQ(time.GetFixedStepCount(), 2u);

        time.Hold();

        EXPECT_EQ(time.GetDeltaTime(), 0.0f);
        EXPECT_EQ(time.GetFixedStepCount(), 0u);
        EXPECT_EQ(time.GetFrameCount(), 1u);
        EXPECT_EQ(time.GetTotalTime(), kFixedStep * 2.5);
    }

    TEST(TimeTest, HoldKeepsTheFixedStepRemainder) {
        Time time;
        time.SetFixedDeltaTime(kFixedStep);
        time.Advance(kFixedStep / 2);

        time.Hold();
        time.Advance(kFixedStep / 2);

        EXPECT_EQ(time.GetFixedStepCount(), 1u);
    }

    TEST(TimeTest, TickAfterALongHoldMeasuresOneFrame) {
        constexpr auto kFrame = std::chrono::milliseconds(16);
        constexpr auto kLongHold = std::chrono::hours(1);
        const Time::Clock::time_point start = Time::Clock::now();
        Time time;
        time.TickAt(start);

        time.Hold(start + kLongHold);
        time.TickAt(start + kLongHold + kFrame);

        EXPECT_NEAR(time.GetDeltaTime(), 0.016, 1e-6);
        EXPECT_EQ(time.GetFrameCount(), 2u);
    }

    TEST(TimeTest, TickWithoutAHoldJumpsByTheClampedGap) {
        const Time::Clock::time_point start = Time::Clock::now();
        Time time;
        time.TickAt(start);

        time.TickAt(start + std::chrono::hours(1));

        EXPECT_EQ(time.GetDeltaTime(), static_cast<float>(Time::kMaxFrameTime));
    }

    TEST(TimeTest, AdvanceMovesTheWallClockMarkOfAStartedClock) {
        Time time;
        time.TickAt(Time::Clock::now() - std::chrono::hours(1));

        time.Advance(kFixedStep);
        time.TickAt(Time::Clock::now());

        EXPECT_LT(time.GetDeltaTime(), 0.1f);
    }

    TEST(TimeTest, AdvanceDoesNotStartTheWallClock) {
        Time time;

        time.Advance(kFixedStep);
        time.TickAt(Time::Clock::now() + std::chrono::hours(1));

        EXPECT_EQ(time.GetDeltaTime(), 0.0f);
    }

    /// Components read the frame from CoreManager::GetTime(), driven here by a fake clock instead of the game loop.
    class TimeSceneTest : public ::testing::Test {
    protected:
        void SetUp() override {
            CoreManager::GetTime().Reset();
            CoreManager::GetTime().SetFixedDeltaTime(kFixedStep);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kProbeSceneXml));
            _manager = BECore::New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();

            _probe = _manager->GetActiveScene()->GetRootNode().GetChildren().front()->GetComponent<SpeedProbe>();
            ASSERT_TRUE(_probe);
        }

        void TearDown() override {
            // Destroying the scenes drops the components' event connections
            _probe = nullptr;
            _manager.Reset();
            CoreManager::GetTime().SetFixedDeltaTime(Time::kDefaultFixedDeltaTime);
            CoreManager::GetTime().Reset();
        }

        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<SpeedProbe> _probe;
    };

    TEST_F(TimeSceneTest, UpdateEventCarriesTheFrameTime) {
        CoreManager::GetTime().Advance(0.125);
        _manager->UpdateAll();

        EXPECT_EQ(_probe->updateCount, 1);
        EXPECT_EQ(_probe->lastDeltaTime, 0.125f);
    }

    TEST_F(TimeSceneTest, DistanceTravelledInOneSecondDoesNotDependOnTheFrameRate) {
        for (const int framesPerSecond : kFrameRates) {
            const float before = _probe->distance;

            RunOneSecond(CoreManager::GetTime(), framesPerSecond, [&] { _manager->UpdateAll(); });

            EXPECT_FLOAT_EQ(_probe->distance - before, SpeedProbe::kSpeed) << framesPerSecond << " frames per second";
        }
    }

    TEST_F(TimeSceneTest, FixedUpdateRunsTheSameNumberOfStepsAtAnyFrameRate) {
        for (const int framesPerSecond : kFrameRates) {
            const int stepsBefore = _probe->fixedStepCount;
            const float distanceBefore = _probe->fixedDistance;

            RunOneSecond(CoreManager::GetTime(), framesPerSecond, [&] { _manager->UpdateAll(); });

            EXPECT_EQ(_probe->fixedStepCount - stepsBefore, 64) << framesPerSecond << " frames per second";
            EXPECT_FLOAT_EQ(_probe->fixedDistance - distanceBefore, SpeedProbe::kSpeed) << framesPerSecond << " frames per second";
        }
    }

    TEST_F(TimeSceneTest, FixedStepsRunBeforeTheUpdateOfTheSameFrame) {
        CoreManager::GetTime().Advance(kFixedStep * 2);
        _manager->UpdateAll();

        EXPECT_EQ(_probe->fixedStepCount, 2);
        EXPECT_EQ(_probe->updateCount, 1);
        EXPECT_EQ(_probe->lastFixedOrder, 2);
        EXPECT_EQ(_probe->updateOrder, 3);
    }

}  // namespace BECore::Tests
