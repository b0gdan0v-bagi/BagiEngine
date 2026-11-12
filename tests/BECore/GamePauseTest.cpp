#include <BECore/Time/GamePause.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Powers of two, so the seconds compare exactly
        constexpr double kHalfStep = 0.5;
        constexpr double kQuarterStep = 0.25;

    }  // namespace

    TEST(GamePauseTest, StartsRunning) {
        GamePause pause;

        EXPECT_FALSE(pause.IsPaused());
        EXPECT_EQ(pause.GetPendingSteps(), 0u);
        EXPECT_EQ(pause.BeginFrame().kind, FrameKind::Running);
    }

    TEST(GamePauseTest, RunningFramesCarryNoSeconds) {
        GamePause pause;

        const FrameDecision frame = pause.BeginFrame();

        EXPECT_EQ(frame.kind, FrameKind::Running);
        EXPECT_EQ(frame.seconds, 0.0);
    }

    TEST(GamePauseTest, PausedWithNothingRequestedHoldsEveryFrame) {
        GamePause pause;
        pause.SetPaused(true);

        for (int frame = 0; frame < 5; ++frame) {
            const FrameDecision decision = pause.BeginFrame();
            EXPECT_EQ(decision.kind, FrameKind::Held);
            EXPECT_EQ(decision.seconds, 0.0);
        }
        EXPECT_TRUE(pause.IsPaused());
    }

    TEST(GamePauseTest, ResumingRunsAgain) {
        GamePause pause;
        pause.SetPaused(true);
        ASSERT_EQ(pause.BeginFrame().kind, FrameKind::Held);

        pause.SetPaused(false);

        EXPECT_EQ(pause.BeginFrame().kind, FrameKind::Running);
    }

    TEST(GamePauseTest, ARequestGivesExactlyThatManySteppedFramesThenHeldAgain) {
        GamePause pause;
        pause.SetPaused(true);
        pause.RequestSteps(3);

        EXPECT_TRUE(pause.IsStepDue());
        EXPECT_EQ(pause.GetPendingSteps(), 3u);
        for (uint32_t step = 0; step < 3; ++step) {
            EXPECT_EQ(pause.BeginFrame().kind, FrameKind::Stepped) << "step " << step;
        }
        EXPECT_FALSE(pause.IsStepDue());
        EXPECT_EQ(pause.GetPendingSteps(), 0u);
        EXPECT_EQ(pause.BeginFrame().kind, FrameKind::Held);
        EXPECT_EQ(pause.BeginFrame().kind, FrameKind::Held);
    }

    TEST(GamePauseTest, SteppedFramesCarryTheSecondsOfTheRequest) {
        GamePause pause;
        pause.SetPaused(true);
        pause.RequestSteps(2, kHalfStep);

        EXPECT_EQ(pause.GetStepSeconds(), kHalfStep);
        EXPECT_EQ(pause.BeginFrame().seconds, kHalfStep);
        EXPECT_EQ(pause.BeginFrame().seconds, kHalfStep);
    }

    TEST(GamePauseTest, DefaultStepIsOneFrameAtSixtyFps) {
        GamePause pause;
        pause.SetPaused(true);
        pause.RequestSteps(1);

        EXPECT_EQ(pause.BeginFrame().seconds, kDefaultStepSeconds);
    }

    TEST(GamePauseTest, RequestsAddUpAndTheLatestSecondsApplyToAll) {
        GamePause pause;
        pause.SetPaused(true);
        pause.RequestSteps(2, kHalfStep);
        pause.RequestSteps(1, kQuarterStep);

        EXPECT_EQ(pause.GetPendingSteps(), 3u);
        for (int step = 0; step < 3; ++step) {
            EXPECT_EQ(pause.BeginFrame().seconds, kQuarterStep);
        }
    }

    TEST(GamePauseTest, ZeroStepsAddNothingAndKeepTheSeconds) {
        GamePause pause;
        pause.SetPaused(true);
        pause.RequestSteps(1, kHalfStep);

        pause.RequestSteps(0, kQuarterStep);

        EXPECT_EQ(pause.GetPendingSteps(), 1u);
        EXPECT_EQ(pause.GetStepSeconds(), kHalfStep);
    }

    TEST(GamePauseTest, StepsRequestedWhileRunningAreTakenOnlyOnceItIsPaused) {
        GamePause pause;
        pause.RequestSteps(1, kHalfStep);

        EXPECT_FALSE(pause.IsStepDue());
        EXPECT_EQ(pause.BeginFrame().kind, FrameKind::Running);
        EXPECT_EQ(pause.GetPendingSteps(), 1u);

        pause.SetPaused(true);

        EXPECT_EQ(pause.BeginFrame().kind, FrameKind::Stepped);
    }

    TEST(GamePauseTest, PausingKeepsThePendingSteps) {
        GamePause pause;
        pause.SetPaused(true);
        pause.RequestSteps(2);
        ASSERT_EQ(pause.BeginFrame().kind, FrameKind::Stepped);

        pause.SetPaused(true);

        EXPECT_EQ(pause.GetPendingSteps(), 1u);
        EXPECT_EQ(pause.BeginFrame().kind, FrameKind::Stepped);
    }

    TEST(GamePauseTest, ResumingDropsThePendingSteps) {
        GamePause pause;
        pause.SetPaused(true);
        pause.RequestSteps(2);

        pause.SetPaused(false);
        pause.SetPaused(true);

        EXPECT_EQ(pause.GetPendingSteps(), 0u);
        EXPECT_EQ(pause.BeginFrame().kind, FrameKind::Held);
    }

}  // namespace BECore::Tests
