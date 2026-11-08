#include <TaskSystem/TaskPriority.h>
#include <TaskSystem/TaskScheduler.h>
#include <TaskSystem/ThreadPool.h>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
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

            void Schedule(TaskScheduler::Duration delay, int id = 0, DelayClock clock = DelayClock::GameTime) {
                _scheduler.ScheduleDelayed(
                    [this, id] {
                        ++_ran;
                        _order.push_back(id);
                    },
                    delay, TaskPriority::Normal, ThreadType::MainThread, clock);
            }

            void ScheduleGame(TaskScheduler::Duration delay, int id = 0) {
                Schedule(delay, id, DelayClock::GameTime);
            }

            void ScheduleWall(TaskScheduler::Duration delay, int id = 0) {
                Schedule(delay, id, DelayClock::WallClock);
            }

            TaskScheduler _scheduler;
            std::unique_ptr<ThreadPool> _pool;
            int _ran = 0;
            std::vector<int> _order;
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

}  // namespace BECore::Tests
