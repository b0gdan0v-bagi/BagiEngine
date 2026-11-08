#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <TaskSystem/Awaitables.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/TaskPriority.h>
#include <TaskSystem/TaskScheduler.h>
#include <TaskSystem/ThreadPool.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        using std::chrono::milliseconds;
        using std::chrono::seconds;

        /// A scheduler on its own (no engine services, no worker thread is used): every task here is a
        /// MainThread task, so it runs only when the test calls ProcessMainThreadTasks. The wall clock
        /// is never waited for; the game clock moves only by AdvanceGameTime.
        class TaskSchedulerGameTimeTest : public ::testing::Test {
        protected:
            void SetUp() override {
                _pool = std::make_unique<ThreadPool>(1);
                _scheduler.Initialize(_pool.get());
            }

            void TearDown() override {
                _scheduler.Shutdown();
                _pool->Shutdown();
            }

            /// Releases what is due and runs it; returns how many tasks ran.
            int Pump() {
                const int before = _ran;
                _scheduler.ProcessDelayedTasks();
                _scheduler.ProcessMainThreadTasks();
                return _ran - before;
            }

            void Schedule(TaskScheduler::Duration delay, DelayClock clock, int id = 0) {
                _scheduler.ScheduleDelayed(
                    [this, id] {
                        ++_ran;
                        _order.push_back(id);
                    },
                    delay, TaskPriority::Normal, ThreadType::MainThread, clock);
            }

            void ScheduleGame(TaskScheduler::Duration delay, int id = 0) {
                Schedule(delay, DelayClock::GameTime, id);
            }

            void ScheduleWall(TaskScheduler::Duration delay, int id = 0) {
                Schedule(delay, DelayClock::WallClock, id);
            }

            TaskScheduler _scheduler;
            std::unique_ptr<ThreadPool> _pool;
            int _ran = 0;
            eastl::vector<int> _order;
        };

    }  // namespace

    TEST_F(TaskSchedulerGameTimeTest, NotReleasedBeforeTheGameTimeReachesTheDelay) {
        ScheduleGame(seconds(2));

        EXPECT_EQ(Pump(), 0);
        _scheduler.AdvanceGameTime(1.0);
        EXPECT_EQ(Pump(), 0);
        _scheduler.AdvanceGameTime(0.999);
        EXPECT_EQ(Pump(), 0);
        EXPECT_EQ(_scheduler.GetDelayedTaskCount(), 1u);
    }

    TEST_F(TaskSchedulerGameTimeTest, ReleasedExactlyAtTheDelay) {
        ScheduleGame(seconds(2));

        _scheduler.AdvanceGameTime(2.0);
        EXPECT_EQ(Pump(), 1);
        EXPECT_EQ(_scheduler.GetDelayedTaskCount(), 0u);
        EXPECT_EQ(Pump(), 0);  // once only
    }

    TEST_F(TaskSchedulerGameTimeTest, ReleasedAfterManySmallSteps) {
        ScheduleGame(seconds(1));

        // 1/64 is exact in binary and in nanoseconds, so 64 steps add up to exactly one second on every platform.
        constexpr double kStep = 1.0 / 64.0;
        for (int i = 0; i < 63; ++i) {
            _scheduler.AdvanceGameTime(kStep);
            ASSERT_EQ(Pump(), 0) << "released early at step " << i;
        }
        _scheduler.AdvanceGameTime(kStep);
        EXPECT_EQ(Pump(), 1);
        EXPECT_EQ(_scheduler.GetGameTime(), seconds(1));
    }

    TEST_F(TaskSchedulerGameTimeTest, RealSleepReleasesNothing) {
        ScheduleGame(milliseconds(1));

        std::this_thread::sleep_for(milliseconds(20));
        EXPECT_EQ(Pump(), 0);
        EXPECT_EQ(_scheduler.GetGameTime(), TaskScheduler::Duration::zero());
        EXPECT_EQ(_scheduler.GetDelayedTaskCount(), 1u);
    }

    TEST_F(TaskSchedulerGameTimeTest, WallDelayIsNotReleasedByGameTime) {
        ScheduleWall(seconds(3600));

        _scheduler.AdvanceGameTime(7200.0);
        EXPECT_EQ(Pump(), 0);
        EXPECT_EQ(_scheduler.GetDelayedTaskCount(), 1u);
    }

    TEST_F(TaskSchedulerGameTimeTest, WallDelayStillFollowsTheRealClock) {
        ScheduleWall(milliseconds(1));

        std::this_thread::sleep_for(milliseconds(20));
        EXPECT_EQ(Pump(), 1);
    }

    TEST_F(TaskSchedulerGameTimeTest, BadStepsChangeNothing) {
        ScheduleGame(milliseconds(1));

        _scheduler.AdvanceGameTime(std::numeric_limits<double>::quiet_NaN());
        _scheduler.AdvanceGameTime(-1.0);
        _scheduler.AdvanceGameTime(-0.0);
        _scheduler.AdvanceGameTime(0.0);
        _scheduler.AdvanceGameTime(std::numeric_limits<double>::infinity());
        _scheduler.AdvanceGameTime(-std::numeric_limits<double>::infinity());

        EXPECT_EQ(_scheduler.GetGameTime(), TaskScheduler::Duration::zero());
        EXPECT_EQ(Pump(), 0);
    }

    TEST_F(TaskSchedulerGameTimeTest, StepIsRoundedToTheNearestNanosecond) {
        using std::chrono::nanoseconds;

        _scheduler.AdvanceGameTime(0.4e-9);  // below half a tick: nothing
        EXPECT_EQ(_scheduler.GetGameTime(), nanoseconds(0));
        _scheduler.AdvanceGameTime(1.6e-9);  // above half a tick: up, not truncated down
        EXPECT_EQ(_scheduler.GetGameTime(), nanoseconds(2));

        // The frame delta is a float: float(1/60) is 16666667.53... ns, float(0.7) is 699999988.07... ns.
        _scheduler.AdvanceGameTime(static_cast<double>(1.0f / 60.0f));
        EXPECT_EQ(_scheduler.GetGameTime(), nanoseconds(2 + 16666668));
        _scheduler.AdvanceGameTime(static_cast<double>(0.7f));
        EXPECT_EQ(_scheduler.GetGameTime(), nanoseconds(2 + 16666668 + 699999988));
    }

    TEST_F(TaskSchedulerGameTimeTest, HugeStepSaturatesAndLargeDelaysDoNotOverflow) {
        _scheduler.AdvanceGameTime(1.0e300);
        EXPECT_EQ(_scheduler.GetGameTime(), TaskScheduler::Duration::max());
        _scheduler.AdvanceGameTime(1.0);  // stays at the end of the range, no wrap-around
        EXPECT_EQ(_scheduler.GetGameTime(), TaskScheduler::Duration::max());

        // A delay on top of the maximum saturates instead of wrapping to the past.
        ScheduleGame(seconds(10));
        EXPECT_EQ(Pump(), 1);
    }

    TEST_F(TaskSchedulerGameTimeTest, HugeDelayNeverWrapsIntoThePast) {
        _scheduler.AdvanceGameTime(5.0);
        ScheduleGame(TaskScheduler::Duration::max());

        EXPECT_EQ(Pump(), 0);
        _scheduler.AdvanceGameTime(1.0e6);
        EXPECT_EQ(Pump(), 0);
        EXPECT_EQ(_scheduler.GetDelayedTaskCount(), 1u);
    }

    TEST_F(TaskSchedulerGameTimeTest, TasksReleaseInTimeOrder) {
        ScheduleGame(seconds(3), 3);
        ScheduleGame(seconds(1), 1);
        ScheduleGame(seconds(2), 2);

        // Equal-priority tasks that become ready in one pump have no defined order among themselves
        // (the main thread queue is a heap), so the order is checked by step.
        _scheduler.AdvanceGameTime(1.0);
        EXPECT_EQ(Pump(), 1);
        ASSERT_EQ(_order.size(), 1u);
        EXPECT_EQ(_order[0], 1);

        _scheduler.AdvanceGameTime(1.0);
        EXPECT_EQ(Pump(), 1);
        ASSERT_EQ(_order.size(), 2u);
        EXPECT_EQ(_order[1], 2);

        _scheduler.AdvanceGameTime(1.0);
        EXPECT_EQ(Pump(), 1);
        ASSERT_EQ(_order.size(), 3u);
        EXPECT_EQ(_order[2], 3);
    }

    TEST_F(TaskSchedulerGameTimeTest, DelayIsRelativeToTheGameTimeAtSchedulingTime) {
        _scheduler.AdvanceGameTime(10.0);
        ScheduleGame(seconds(1));

        _scheduler.AdvanceGameTime(0.5);
        EXPECT_EQ(Pump(), 0);
        _scheduler.AdvanceGameTime(0.5);
        EXPECT_EQ(Pump(), 1);
    }

    TEST_F(TaskSchedulerGameTimeTest, ZeroOrNegativeDelayRunsWithoutAnyAdvance) {
        ScheduleGame(TaskScheduler::Duration::zero(), 1);
        ScheduleGame(seconds(-5), 2);
        ScheduleGame(TaskScheduler::Duration::min(), 3);

        EXPECT_EQ(Pump(), 3);
        EXPECT_EQ(_scheduler.GetDelayedTaskCount(), 0u);
    }

    TEST_F(TaskSchedulerGameTimeTest, DelayedTaskCountCountsBothQueues) {
        EXPECT_EQ(_scheduler.GetDelayedTaskCount(), 0u);
        ScheduleGame(seconds(1));
        EXPECT_EQ(_scheduler.GetDelayedTaskCount(), 1u);
        ScheduleWall(seconds(3600));
        EXPECT_EQ(_scheduler.GetDelayedTaskCount(), 2u);
        ScheduleGame(seconds(2));
        EXPECT_EQ(_scheduler.GetDelayedTaskCount(), 3u);

        _scheduler.AdvanceGameTime(1.0);
        EXPECT_EQ(Pump(), 1);
        EXPECT_EQ(_scheduler.GetDelayedTaskCount(), 2u);
    }

    TEST_F(TaskSchedulerGameTimeTest, ShutdownDropsBothQueues) {
        ScheduleGame(seconds(1));
        ScheduleWall(seconds(3600));
        ASSERT_EQ(_scheduler.GetDelayedTaskCount(), 2u);

        _scheduler.Shutdown();

        EXPECT_EQ(_scheduler.GetDelayedTaskCount(), 0u);
        _scheduler.AdvanceGameTime(10.0);
        EXPECT_EQ(Pump(), 0);
    }

    TEST_F(TaskSchedulerGameTimeTest, ReleasedTaskKeepsItsThreadTypeAndRunsOnlyOnTheMainThreadQueue) {
        ScheduleGame(seconds(1));
        _scheduler.AdvanceGameTime(1.0);

        _scheduler.ProcessDelayedTasks();
        EXPECT_EQ(_ran, 0);  // released into the main thread queue, not run yet
        EXPECT_EQ(_scheduler.GetMainThreadQueueSize(), 1u);
        _scheduler.ProcessMainThreadTasks();
        EXPECT_EQ(_ran, 1);
    }

    // =================================================================
    // Awaitable factories and the frame delta (the scheduler used directly)
    // =================================================================

    TEST_F(TaskSchedulerGameTimeTest, DelayGameFactoriesBuildGameClockAwaiters) {
        const DelayAwaiter ms = DelayGameMs(250);
        EXPECT_TRUE(ms.clock == DelayClock::GameTime);
        EXPECT_TRUE(ms.threadType == ThreadType::MainThread);
        EXPECT_EQ(ms.delay, milliseconds(250));
        EXPECT_FALSE(ms.await_ready());

        const DelayAwaiter sec = DelayGameSec(0.5);
        EXPECT_TRUE(sec.clock == DelayClock::GameTime);
        EXPECT_EQ(sec.delay, milliseconds(500));

        const DelayAwaiter generic = DelayGame(seconds(2), ThreadType::Background);
        EXPECT_TRUE(generic.clock == DelayClock::GameTime);
        EXPECT_TRUE(generic.threadType == ThreadType::Background);
        EXPECT_EQ(generic.delay, seconds(2));

        // The wall-clock factories keep their clock and their default thread
        EXPECT_TRUE(Delay(seconds(1)).clock == DelayClock::WallClock);
        EXPECT_TRUE(DelaySec(1.0).clock == DelayClock::WallClock);
        EXPECT_TRUE(DelayMs(1).clock == DelayClock::WallClock);
        EXPECT_TRUE(DelaySec(1.0).threadType == ThreadType::Background);
    }

    TEST_F(TaskSchedulerGameTimeTest, DelayGameSecIsReadyForNaNZeroAndNegative) {
        EXPECT_TRUE(DelayGameSec(std::numeric_limits<double>::quiet_NaN()).await_ready());
        EXPECT_TRUE(DelayGameSec(0.0).await_ready());
        EXPECT_TRUE(DelayGameSec(-0.0).await_ready());
        EXPECT_TRUE(DelayGameSec(-3.0).await_ready());
        EXPECT_TRUE(DelayGameSec(-std::numeric_limits<double>::infinity()).await_ready());
        EXPECT_TRUE(DelayGameMs(0).await_ready());
        EXPECT_TRUE(DelayGameMs(-5).await_ready());
        EXPECT_FALSE(DelayGameSec(1.0e-9).await_ready());
    }

    TEST_F(TaskSchedulerGameTimeTest, DelayGameSecSaturatesAHugeDelay) {
        EXPECT_EQ(DelayGameSec(1.0e300).delay, TaskScheduler::Duration::max());
        EXPECT_EQ(DelayGameSec(std::numeric_limits<double>::infinity()).delay, TaskScheduler::Duration::max());
        EXPECT_FALSE(DelayGameSec(1.0e300).await_ready());
    }

    namespace {

        /// Schedules the delay of @p awaiter the way DelayAwaiter::await_suspend does (the same clock, delay and thread type).
        void ScheduleLike(TaskScheduler& scheduler, const DelayAwaiter& awaiter, TaskQueue::TaskFunc task) {
            scheduler.ScheduleDelayed(std::move(task), awaiter.delay, TaskPriority::Normal, ThreadType::MainThread, awaiter.clock);
        }

    }  // namespace

    // The frame delta is a float and goes to AdvanceGameTime as a double; DelayGameSec rounds to the nearest nanosecond the
    // same way, so a delay written as that value is due on the frame it is reached. A decimal delay that equals a float delta
    // only after several frames (0.1 x N) may sit a few ns above the sum of the rounded steps: it is due within one frame.

    TEST_F(TaskSchedulerGameTimeTest, OneFrameOfAFloatDeltaReleasesTheDelayOfThatDelta) {
        for (const float dt : {1.0f / 60.0f, 0.1f, 0.7f, 0.016f, 0.25f}) {
            const int before = _ran;
            ScheduleLike(_scheduler, DelayGameSec(static_cast<double>(dt)), [this] { ++_ran; });

            ASSERT_EQ(Pump(), 0) << dt;
            _scheduler.AdvanceGameTime(static_cast<double>(dt));
            EXPECT_EQ(Pump(), 1) << "dt " << dt;
            EXPECT_EQ(_ran, before + 1);
        }
    }

    TEST_F(TaskSchedulerGameTimeTest, FramesOfAFloatDeltaReleaseTheProductOnTheLastFrame) {
        // Worked out by hand: round(N * dt) <= N * round(dt) for 1/60 (N = 1..10) and equal for 0.7 (N = 1..5).
        struct Case {
            float dt;
            int frames;
        };
        for (const Case c : {Case{1.0f / 60.0f, 3}, Case{1.0f / 60.0f, 10}, Case{0.7f, 2}, Case{0.7f, 5}}) {
            const int before = _ran;
            ScheduleLike(_scheduler, DelayGameSec(static_cast<double>(c.frames) * static_cast<double>(c.dt)), [this] { ++_ran; });

            for (int frame = 1; frame < c.frames; ++frame) {
                _scheduler.AdvanceGameTime(static_cast<double>(c.dt));
                ASSERT_EQ(Pump(), 0) << "dt " << c.dt << " released early at frame " << frame;
            }
            _scheduler.AdvanceGameTime(static_cast<double>(c.dt));
            EXPECT_EQ(Pump(), 1) << "dt " << c.dt << " frames " << c.frames;
            EXPECT_EQ(_ran, before + 1);
        }
    }

    TEST_F(TaskSchedulerGameTimeTest, AFloatTenthIsReleasedWithinOneFrameOfTheProduct) {
        // For N >= 2 round(N * float(0.1)) is a few ns above N * round(float(0.1)) (worked out by hand), so the delay is due one frame later.
        for (int frames = 2; frames <= 5; ++frames) {
            const float dt = 0.1f;
            const int before = _ran;
            ScheduleLike(_scheduler, DelayGameSec(static_cast<double>(frames) * static_cast<double>(dt)), [this] { ++_ran; });

            for (int frame = 1; frame < frames; ++frame) {
                _scheduler.AdvanceGameTime(static_cast<double>(dt));
                ASSERT_EQ(Pump(), 0) << "released early at frame " << frame;
            }
            _scheduler.AdvanceGameTime(static_cast<double>(dt));
            const int onTheFrame = Pump();
            if (onTheFrame == 0) {
                _scheduler.AdvanceGameTime(static_cast<double>(dt));
                EXPECT_EQ(Pump(), 1) << "frames " << frames << ": not released one frame after the product";
            }
            EXPECT_EQ(_ran, before + 1);
        }
    }

    // =================================================================
    // The real TaskManager: coroutines and RunDelayedGameTime
    // =================================================================

    // CoreManager::OnGameCycle (which feeds TaskManager::AdvanceGameTime with Time::GetDeltaTime() right before Update) cannot be called
    // from a test (PassKey<Application>), and TaskManager::AdvanceGameTime takes PassKey<CoreManager>. The one-line wiring in OnGameCycle
    // is read-checked; these tests drive the same TaskScheduler::AdvanceGameTime through GetScheduler() and pump
    // ProcessDelayedTasks + ProcessMainThreadTasks, which is what Update does. The game clock of the singleton is shared by the
    // process, so only relative game times are used.

    namespace {

        /// What a coroutine tells the test. The fixture owns it, so it outlives every coroutine of the test.
        struct GameDelayProbe {
            std::atomic<int> stage{0};  // 0 not started, 1 about to wait, 2 resumed
            std::atomic<bool> resumedOnMainThread{false};
        };

        template <typename Predicate>
        bool WaitUntilTrue(Predicate&& predicate) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!predicate()) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    return false;
                }
                std::this_thread::sleep_for(milliseconds(1));
            }
            return true;
        }

        void Resumed(GameDelayProbe& probe) {
            probe.resumedOnMainThread.store(TaskManager::GetInstance().IsMainThread(), std::memory_order_release);
            probe.stage.store(2, std::memory_order_release);
        }

        Task<void> WaitSec(GameDelayProbe& probe, double seconds) {
            probe.stage.store(1, std::memory_order_release);
            co_await DelayGameSec(seconds);
            Resumed(probe);
        }

        Task<void> WaitMs(GameDelayProbe& probe, int64_t milliseconds) {
            probe.stage.store(1, std::memory_order_release);
            co_await DelayGameMs(milliseconds);
            Resumed(probe);
        }

        Task<void> WaitDuration(GameDelayProbe& probe) {
            probe.stage.store(1, std::memory_order_release);
            co_await DelayGame(std::chrono::milliseconds(250));
            Resumed(probe);
        }

        Task<void> WaitWallSec(GameDelayProbe& probe, double seconds) {
            probe.stage.store(1, std::memory_order_release);
            co_await DelaySec(seconds, ThreadType::MainThread);
            Resumed(probe);
        }

        Task<void> WaitGameOnBackground(GameDelayProbe& probe) {
            probe.stage.store(1, std::memory_order_release);
            co_await DelayGameMs(10, ThreadType::Background);
            probe.stage.store(2, std::memory_order_release);
        }

        Task<void> Stamp(GameDelayProbe& probe) {
            Resumed(probe);
            co_return;
        }

    }  // namespace

    class TaskSchedulerGameTimeManagerTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ASSERT_TRUE(TaskManager::GetInstance().IsInitialized());
            ASSERT_TRUE(TaskManager::GetInstance().IsMainThread());
        }

        void TearDown() override {
            // Release whatever a failed test left suspended on the game clock while the probe is still alive.
            Scheduler().AdvanceGameTime(1.0e5);
            Pump();
        }

        static TaskScheduler& Scheduler() {
            return *TaskManager::GetInstance().GetScheduler();
        }

        /// What TaskManager::Update does with the delayed and main-thread queues.
        static void Pump() {
            Scheduler().ProcessDelayedTasks();
            Scheduler().ProcessMainThreadTasks();
        }

        [[nodiscard]] int Stage() const {
            return _probe.stage.load(std::memory_order_acquire);
        }

        GameDelayProbe _probe;
    };

    TEST_F(TaskSchedulerGameTimeManagerTest, CoroutineWaitsForTheGameTimeAndFinishesInThePumpThatReachesIt) {
        auto handle = TaskManager::GetInstance().RunOnMainThread(WaitSec(_probe, 0.5));

        Pump();
        ASSERT_EQ(Stage(), 1);  // started, suspended on the delay
        Pump();
        EXPECT_EQ(Stage(), 1);

        Scheduler().AdvanceGameTime(0.25);
        Pump();
        EXPECT_EQ(Stage(), 1);

        Scheduler().AdvanceGameTime(0.25);
        Pump();
        EXPECT_EQ(Stage(), 2) << "released and resumed in the pump that reached the delay";
        EXPECT_TRUE(_probe.resumedOnMainThread.load());
        EXPECT_TRUE(handle->IsDone());
    }

    TEST_F(TaskSchedulerGameTimeManagerTest, DelayGameMsAndTheDurationForm) {
        auto ms = TaskManager::GetInstance().RunOnMainThread(WaitMs(_probe, 300));
        GameDelayProbe other;
        auto duration = TaskManager::GetInstance().RunOnMainThread(WaitDuration(other));
        Pump();
        ASSERT_EQ(Stage(), 1);
        ASSERT_EQ(other.stage.load(), 1);

        Scheduler().AdvanceGameTime(0.25);
        Pump();
        EXPECT_EQ(Stage(), 1);
        EXPECT_EQ(other.stage.load(), 2) << "250 ms";

        Scheduler().AdvanceGameTime(0.05);
        Pump();
        EXPECT_EQ(Stage(), 2) << "300 ms";
    }

    TEST_F(TaskSchedulerGameTimeManagerTest, RunDelayedGameTimeStartsTheTaskOnlyAfterTheTime) {
        auto handle = TaskManager::GetInstance().RunDelayedGameTime(Stamp(_probe), milliseconds(300));

        Pump();
        EXPECT_EQ(Stage(), 0);
        Scheduler().AdvanceGameTime(0.299);
        Pump();
        EXPECT_EQ(Stage(), 0);

        Scheduler().AdvanceGameTime(0.001);
        Pump();
        EXPECT_EQ(Stage(), 2);
        EXPECT_TRUE(_probe.resumedOnMainThread.load()) << "the default thread type is the main thread";
        EXPECT_TRUE(handle->IsDone());
    }

    TEST_F(TaskSchedulerGameTimeManagerTest, ZeroNaNAndNegativeDelaysDoNotSuspend) {
        const size_t delayed = Scheduler().GetDelayedTaskCount();
        for (const double seconds : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
            GameDelayProbe probe;
            TaskManager::GetInstance().RunOnMainThread(WaitSec(probe, seconds));

            Pump();  // one pump, no advance
            EXPECT_EQ(probe.stage.load(), 2) << "delay " << seconds;
            EXPECT_EQ(Scheduler().GetDelayedTaskCount(), delayed);
        }
    }

    TEST_F(TaskSchedulerGameTimeManagerTest, AWallClockDelayIsNotReleasedByGameTime) {
        auto handle = TaskManager::GetInstance().RunOnMainThread(WaitWallSec(_probe, 3600.0));
        Pump();
        ASSERT_EQ(Stage(), 1);

        Scheduler().AdvanceGameTime(7200.0);
        Pump();
        EXPECT_EQ(Stage(), 1) << "DelaySec stays on the wall clock";
        // The parked task is abandoned with the others at shutdown, like ParkedTask in TaskSystemTest.
    }

    TEST_F(TaskSchedulerGameTimeManagerTest, TheGameDelayDoesNotMoveWithTheRealClock) {
        auto handle = TaskManager::GetInstance().RunOnMainThread(WaitMs(_probe, 5));
        Pump();
        ASSERT_EQ(Stage(), 1);

        std::this_thread::sleep_for(milliseconds(40));
        Pump();
        EXPECT_EQ(Stage(), 1);

        Scheduler().AdvanceGameTime(0.005);
        Pump();
        EXPECT_EQ(Stage(), 2);
    }

    TEST_F(TaskSchedulerGameTimeManagerTest, CancellingTheHandleBeforeTheTimeIsHarmless) {
        auto handle = TaskManager::GetInstance().RunOnMainThread(WaitSec(_probe, 0.5));
        Pump();
        ASSERT_EQ(Stage(), 1);

        handle->Cancel();
        Scheduler().AdvanceGameTime(0.5);
        Pump();  // the delayed job still resumes the frame, which the handle keeps alive

        EXPECT_TRUE(handle->IsCancelled());
        handle->Wait();  // returns at once for a cancelled handle
        handle.Reset();
        Pump();
    }

    TEST_F(TaskSchedulerGameTimeManagerTest, ACancelledDelayedTaskNeverStarts) {
        auto handle = TaskManager::GetInstance().RunDelayedGameTime(Stamp(_probe), milliseconds(100));
        handle->Cancel();

        Scheduler().AdvanceGameTime(1.0);
        Pump();
        EXPECT_EQ(Stage(), 0);
        EXPECT_TRUE(handle->IsCancelled());
    }

    TEST_F(TaskSchedulerGameTimeManagerTest, AGameDelayOnABackgroundThreadResumesOnTheWorkerAfterTheTime) {
        const size_t delayed = Scheduler().GetDelayedTaskCount();
        auto handle = TaskManager::GetInstance().Run(WaitGameOnBackground(_probe));

        // Wait until the worker has parked the delay, so the advance below cannot come first.
        ASSERT_TRUE(WaitUntilTrue([&] { return Scheduler().GetDelayedTaskCount() > delayed; }));
        Pump();
        EXPECT_EQ(Stage(), 1);

        Scheduler().AdvanceGameTime(0.01);
        Pump();  // ProcessDelayedTasks hands the job to the pool
        EXPECT_TRUE(WaitUntilTrue([&] { return Stage() == 2; })) << "the background job never resumed";
    }

}  // namespace BECore::Tests
