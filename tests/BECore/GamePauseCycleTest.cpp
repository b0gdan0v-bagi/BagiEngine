#include "TimeTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Time/GamePause.h>
#include <Common/EngineEnvironment.h>
#include <TaskSystem/Awaitables.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskManager.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        using std::chrono::milliseconds;

        constexpr double kRunningFrameSeconds = 1.0 / 64;
        constexpr float kTweenSeconds = 1.0f;
        constexpr double kTolerance = 1e-6;

        // Between a step (about 16.7 ms) and two of them
        constexpr milliseconds kDueAfterOneStep{10};
        constexpr milliseconds kDueAfterTwoSteps{30};

        constexpr const char* kProbeSceneXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="A"><nodes>
    <node name="Probe"><components>
      <component type="SpeedProbe"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        struct TaskProbe {
            std::atomic<bool> ranOnMainThread{false};
            std::atomic<bool> workerFinished{false};
            std::atomic<bool> continuedOnMainThread{false};
        };

        Task<void> Stamp(TaskProbe& probe) {
            probe.ranOnMainThread.store(true, std::memory_order_release);
            co_return;
        }

        /// Works on a pool thread, then asks for the main thread
        Task<void> FinishOnWorker(TaskProbe& probe) {
            probe.workerFinished.store(true, std::memory_order_release);
            co_await SwitchToMainThread();
            probe.continuedOnMainThread.store(true, std::memory_order_release);
        }

    }  // namespace

    /// The game side of the frame that Application::RunFrame runs (the decision, the clock, the part a pause skips), over the real services and a scene with a probe.
    class GamePauseCycleTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetTime().Reset();
            CoreManager::GetPause().SetPaused(false);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kProbeSceneXml));
            _manager = BECore::New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);

            _probe = _manager->GetActiveScene()->GetRootNode().GetChildren().front()->GetComponent<SpeedProbe>();
            ASSERT_TRUE(_probe);

            _tween = CoreManager::GetTweens().Start<float>(0.0f, 1.0f, TweenSettings{.duration = kTweenSeconds}, [this](const float& value) { _tweenValue = value; });
            TaskManager::GetInstance().RunDelayedGameTime(Stamp(_oneStep), kDueAfterOneStep);
            TaskManager::GetInstance().RunDelayedGameTime(Stamp(_twoSteps), kDueAfterTwoSteps);
        }

        void TearDown() override {
            // Release what a failed test left on the game clock while the probes are alive
            CoreManager::GetPause().SetPaused(false);
            CoreManager::GetTime().Advance(Time::kMaxFrameTime);
            EngineEnvironment::UpdateGame(FrameKind::Running);
            CoreManager::GetPause().SetPaused(false);
            _tween = nullptr;
            _probe = nullptr;
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetTime().Reset();
        }

        /// One frame as Application::RunFrame does it, with a fake clock for the running ones
        FrameKind RunFrame() {
            GamePause& pause = CoreManager::GetPause();
            Time& time = CoreManager::GetTime();
            const FrameDecision frame = pause.BeginFrame();
            switch (frame.kind) {
                case FrameKind::Running:
                    time.Advance(kRunningFrameSeconds);
                    break;
                case FrameKind::Stepped:
                    time.Advance(frame.seconds);
                    break;
                case FrameKind::Held:
                    time.Hold();
                    break;
            }
            EngineEnvironment::UpdateGame(frame.kind);
            return frame.kind;
        }

        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<SpeedProbe> _probe;
        IntrusivePtr<TweenHandle> _tween;
        float _tweenValue = 0.0f;
        TaskProbe _oneStep;
        TaskProbe _twoSteps;
    };

    TEST_F(GamePauseCycleTest, ARunningGameAdvancesEverything) {
        ASSERT_EQ(RunFrame(), FrameKind::Running);

        EXPECT_EQ(_probe->updateCount, 1);
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 1u);
        EXPECT_EQ(CoreManager::GetTime().GetTotalTime(), kRunningFrameSeconds);
        EXPECT_NEAR(_tweenValue, kRunningFrameSeconds / kTweenSeconds, kTolerance);
        EXPECT_TRUE(_oneStep.ranOnMainThread.load());
        EXPECT_FALSE(_twoSteps.ranOnMainThread.load());
    }

    TEST_F(GamePauseCycleTest, HeldFramesAdvanceNothing) {
        CoreManager::GetPause().SetPaused(true);

        for (int frame = 0; frame < 10; ++frame) {
            ASSERT_EQ(RunFrame(), FrameKind::Held);
        }

        EXPECT_EQ(_probe->updateCount, 0);
        EXPECT_EQ(_probe->fixedStepCount, 0);
        EXPECT_EQ(_tweenValue, 0.0f);
        EXPECT_EQ(_tween->GetState(), TweenState::Waiting);
        EXPECT_FALSE(_oneStep.ranOnMainThread.load());
        EXPECT_FALSE(_twoSteps.ranOnMainThread.load());
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 0u);
        EXPECT_EQ(CoreManager::GetTime().GetTotalTime(), 0.0);
        EXPECT_EQ(CoreManager::GetTime().GetDeltaTime(), 0.0f);
        EXPECT_EQ(CoreManager::GetTime().GetFixedStepCount(), 0u);
    }

    TEST_F(GamePauseCycleTest, AStepAdvancesEverythingByExactlyOneStepAndTheNextFrameIsHeld) {
        CoreManager::GetPause().SetPaused(true);
        CoreManager::GetPause().RequestSteps(1);

        ASSERT_EQ(RunFrame(), FrameKind::Stepped);

        EXPECT_EQ(_probe->updateCount, 1);
        EXPECT_EQ(_probe->lastDeltaTime, static_cast<float>(kDefaultStepSeconds));
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 1u);
        EXPECT_EQ(CoreManager::GetTime().GetTotalTime(), kDefaultStepSeconds);
        EXPECT_NEAR(_tweenValue, kDefaultStepSeconds / kTweenSeconds, kTolerance);
        EXPECT_TRUE(_oneStep.ranOnMainThread.load());
        EXPECT_FALSE(_twoSteps.ranOnMainThread.load());

        ASSERT_EQ(RunFrame(), FrameKind::Held);

        EXPECT_EQ(_probe->updateCount, 1);
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 1u);
        EXPECT_NEAR(_tweenValue, kDefaultStepSeconds / kTweenSeconds, kTolerance);
        EXPECT_FALSE(_twoSteps.ranOnMainThread.load());
    }

    TEST_F(GamePauseCycleTest, StepsOfAnotherLengthAdvanceByThatLength) {
        constexpr double kStep = 1.0 / 8;
        CoreManager::GetPause().SetPaused(true);
        CoreManager::GetPause().RequestSteps(2, kStep);

        ASSERT_EQ(RunFrame(), FrameKind::Stepped);
        ASSERT_EQ(RunFrame(), FrameKind::Stepped);

        EXPECT_EQ(_probe->updateCount, 2);
        EXPECT_EQ(CoreManager::GetTime().GetTotalTime(), 2 * kStep);
        EXPECT_NEAR(_tweenValue, 2 * kStep / kTweenSeconds, kTolerance);
        EXPECT_TRUE(_twoSteps.ranOnMainThread.load());
    }

    TEST_F(GamePauseCycleTest, ResumingContinuesFromWhereThePauseStopped) {
        CoreManager::GetPause().SetPaused(true);
        for (int frame = 0; frame < 3; ++frame) {
            RunFrame();
        }

        CoreManager::GetPause().SetPaused(false);
        ASSERT_EQ(RunFrame(), FrameKind::Running);

        EXPECT_EQ(_probe->updateCount, 1);
        EXPECT_EQ(CoreManager::GetTime().GetFrameCount(), 1u);
        EXPECT_NEAR(_tweenValue, kRunningFrameSeconds / kTweenSeconds, kTolerance);
    }

    TEST_F(GamePauseCycleTest, ATaskFinishedOnAWorkerWhilePausedContinuesOnlyInTheNextGameFrame) {
        if (TaskManager::GetInstance().GetWorkerCount() == 0) {
            GTEST_SKIP() << "needs worker threads";
        }
        TaskProbe probe;
        CoreManager::GetPause().SetPaused(true);
        TaskManager::GetInstance().Run(FinishOnWorker(probe));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!probe.workerFinished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(milliseconds(1));
        }
        ASSERT_TRUE(probe.workerFinished.load());

        std::this_thread::sleep_for(milliseconds(50));  // the continuation is queued for the main thread by now, which makes the negative check below mean something
        for (int frame = 0; frame < 5; ++frame) {
            ASSERT_EQ(RunFrame(), FrameKind::Held);
        }
        EXPECT_FALSE(probe.continuedOnMainThread.load()) << "a held frame runs no main-thread task";

        // The worker may still be about to queue the continuation; step until it arrives
        while (!probe.continuedOnMainThread.load() && std::chrono::steady_clock::now() < deadline) {
            CoreManager::GetPause().RequestSteps(1);
            ASSERT_EQ(RunFrame(), FrameKind::Stepped);
            std::this_thread::sleep_for(milliseconds(1));
        }
        EXPECT_TRUE(probe.continuedOnMainThread.load()) << "a stepped frame runs it";
    }

}  // namespace BECore::Tests
