#include <Common/EngineEnvironment.h>
#include <TaskSystem/Awaitables.h>
#include <TaskSystem/CancellationToken.h>
#include <TaskSystem/LiveTasks.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/TaskScheduler.h>
#include <TaskSystem/ThreadPool.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        using Handle = IntrusivePtr<TaskHandle<void>>;
        using TimePoint = TaskScheduler::TimePoint;
        using Duration = TaskScheduler::Duration;

        constexpr Duration kUnit = std::chrono::milliseconds(1);    // the fake time one unit of work takes
        constexpr Duration kBudget = std::chrono::milliseconds(3);  // the budget of a drain in the tests
        constexpr size_t kDrainLimit = 50;                          // a safety stop for a loop that waits for a task
        constexpr int kCancelledResult = -1;                        // what PoliteCancellable returns when its token fired

        // The clock of the budget in these tests: it moves only when a coroutine "works".
        TimePoint g_fakeNow{};

        TimePoint FakeNow() {
            return g_fakeNow;
        }

        void DoWork() {
            g_fakeNow += kUnit;
        }

        /// What a coroutine of these tests reports.
        struct Trace {
            eastl::vector<int> events;
            int steps = 0;
            bool shouldYieldSeen = false;
        };

        /// co_await Yield() bound to @p scheduler: the continuation goes through ScheduleYielded, with the thread
        /// type that YieldAwaiter picks in a single-thread build (the main thread is the only thread).
        struct YieldOf {
            TaskScheduler& scheduler;
            ThreadType threadType = ThreadType::MainThread;

            [[nodiscard]] bool await_ready() const noexcept {
                return false;
            }

            void await_suspend(std::coroutine_handle<> handle) noexcept {
                scheduler.ScheduleYielded([handle]() mutable { handle.resume(); }, threadType);
            }

            void await_resume() const noexcept {}
        };

        /// co_await YieldIfNeeded() bound to @p scheduler: suspends only when ShouldYield() says so.
        struct YieldIfNeededOf : YieldOf {
            [[nodiscard]] bool await_ready() const noexcept {
                return !scheduler.ShouldYield();
            }
        };

        /// A polite loop: asks first, then does a unit of work.
        Task<void> CheckFirstLoop(TaskScheduler& scheduler, Trace& trace, int units, int id = 0) {
            for (int i = 0; i < units; ++i) {
                co_await YieldIfNeededOf{scheduler};
                DoWork();
                ++trace.steps;
                trace.events.push_back(id);
            }
            co_return;
        }

        /// A polite loop: does a unit of work, then asks.
        Task<void> CheckLastLoop(TaskScheduler& scheduler, Trace& trace, int units) {
            for (int i = 0; i < units; ++i) {
                DoWork();
                ++trace.steps;
                co_await YieldIfNeededOf{scheduler};
            }
            co_return;
        }

        /// A loop that looks at its token before every unit of work.
        Task<void> CancellableLoop(TaskScheduler& scheduler, Trace& trace, CancellationToken token, int units) {
            for (int i = 0; i < units; ++i) {
                if (token.IsCancelled()) {
                    co_return;
                }
                co_await YieldIfNeededOf{scheduler};
                DoWork();
                ++trace.steps;
            }
            co_return;
        }

        Task<void> ExplicitYields(TaskScheduler& scheduler, Trace& trace) {
            trace.events.push_back(1);
            co_await YieldOf{scheduler};
            trace.events.push_back(2);
            co_await YieldOf{scheduler};
            trace.events.push_back(3);
            co_return;
        }

        Task<void> ChildTask(TaskScheduler& scheduler, Trace& trace) {
            trace.shouldYieldSeen = scheduler.ShouldYield();
            trace.events.push_back(3);
            co_return;
        }

        /// Yields once, then starts @p child as a background job from inside the slice.
        Task<void> ParentTask(TaskScheduler& scheduler, Trace& trace, Handle child) {
            co_await YieldOf{scheduler};
            trace.events.push_back(1);
            auto* rawChild = child.Get();
            scheduler.Schedule([rawChild]() { rawChild->Start(); }, TaskPriority::Normal, ThreadType::Background);
            trace.events.push_back(2);
            co_return;
        }

        /// A job of the main thread: looks at ShouldYield, yields, continues.
        Task<void> MainThreadJob(TaskScheduler& scheduler, Trace& trace) {
            trace.shouldYieldSeen = scheduler.ShouldYield();
            trace.events.push_back(1);
            co_await YieldOf{scheduler};
            trace.events.push_back(2);
            co_return;
        }

        Handle MakeHandle(Task<void> task) {
            return MakeIntrusive<TaskHandle<void>>(std::move(task));
        }

        bool WaitFor(const std::function<bool()>& condition) {
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (!condition()) {
                if (std::chrono::steady_clock::now() > limit) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return true;
        }

        /// A scheduler of its own over an inline pool, with a fake clock for the budget.
        class TaskSystemBudgetTest : public ::testing::Test {
        protected:
            void SetUp() override {
                g_fakeNow = TimePoint{};
                _pool = eastl::make_unique<ThreadPool>(ThreadPool::InlineTag{});
                _scheduler.Initialize(_pool.get());
                _scheduler.SetBudgetClock(&FakeNow);
            }

            void TearDown() override {
                _scheduler.Shutdown();
                _scheduler.SetBudgetClock(nullptr);
                _pool->Shutdown();
            }

            /// Starts @p handle as a background job, the way TaskManager::Run does.
            void StartInBackground(const Handle& handle) {
                auto* rawHandle = handle.Get();
                _scheduler.Schedule([rawHandle]() { rawHandle->Start(); }, TaskPriority::Normal, ThreadType::Background);
            }

            /// Starts @p handle as a main-thread job and runs the main-thread queue once.
            void StartOnMainThread(const Handle& handle) {
                auto* rawHandle = handle.Get();
                _scheduler.Schedule([rawHandle]() { rawHandle->Start(); }, TaskPriority::Normal, ThreadType::MainThread);
                _scheduler.ProcessMainThreadTasks();
            }

            /// A frame's drain with nothing parked: from now on the window counts as spent between drains.
            void EndFirstFrame() {
                EXPECT_EQ(_scheduler.ProcessBackgroundSlices(kBudget), 0u);
            }

            TaskScheduler _scheduler;
            eastl::unique_ptr<ThreadPool> _pool;
        };

    }  // namespace

    TEST_F(TaskSystemBudgetTest, BeforeTheFirstFrameNothingYieldsAndAPoliteTaskRunsToItsEnd) {
        Trace trace;
        EXPECT_FALSE(_scheduler.ShouldYield());
        auto handle = MakeHandle(CheckFirstLoop(_scheduler, trace, 5));

        StartInBackground(handle);

        EXPECT_TRUE(handle->IsDone());
        EXPECT_EQ(trace.steps, 5);
        EXPECT_EQ(_scheduler.GetBackgroundSliceCount(), 0u);
    }

    TEST_F(TaskSystemBudgetTest, EachDrainSpendsAtMostTheBudgetPlusOneStep) {
        Trace trace;
        EndFirstFrame();
        auto handle = MakeHandle(CheckLastLoop(_scheduler, trace, 10));

        StartInBackground(handle);  // the first slice ends at its first check: one unit done, the task parked
        EXPECT_EQ(trace.steps, 1);
        EXPECT_FALSE(handle->IsDone());
        EXPECT_EQ(_scheduler.GetBackgroundSliceCount(), 1u);

        size_t drains = 0;
        while (_scheduler.GetBackgroundSliceCount() > 0 && drains < kDrainLimit) {
            const TimePoint before = g_fakeNow;
            EXPECT_EQ(_scheduler.ProcessBackgroundSlices(kBudget), 1u);
            EXPECT_LE(g_fakeNow - before, kBudget + kUnit);
            ++drains;
        }

        // 3 units per drain: 4, 7, 10 units; the fourth drain only runs the task to its end
        EXPECT_EQ(drains, 4u);
        EXPECT_EQ(trace.steps, 10);
        EXPECT_TRUE(handle->IsDone());
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Completed);
    }

    TEST_F(TaskSystemBudgetTest, ATaskStartedBetweenDrainsYieldsAtItsFirstCheck) {
        Trace trace;
        EndFirstFrame();
        auto handle = MakeHandle(CheckFirstLoop(_scheduler, trace, 6));

        StartInBackground(handle);

        EXPECT_EQ(trace.steps, 0) << "The window is spent between drains: the first check parks the task";
        EXPECT_FALSE(handle->IsDone());
        EXPECT_EQ(_scheduler.GetBackgroundSliceCount(), 1u);

        EXPECT_EQ(_scheduler.ProcessBackgroundSlices(kBudget), 1u);
        EXPECT_EQ(trace.steps, 3);
        EXPECT_FALSE(handle->IsDone());

        EXPECT_EQ(_scheduler.ProcessBackgroundSlices(kBudget), 1u);
        EXPECT_EQ(trace.steps, 6);
        EXPECT_TRUE(handle->IsDone());
        EXPECT_EQ(_scheduler.GetBackgroundSliceCount(), 0u);
    }

    TEST_F(TaskSystemBudgetTest, ABudgetOfZeroOrLessStillRunsOneSlicePerDrain) {
        for (const Duration budget : {Duration::zero(), Duration(-std::chrono::milliseconds(5))}) {
            Trace trace;
            EndFirstFrame();
            auto first = MakeHandle(CheckFirstLoop(_scheduler, trace, 4, 1));
            auto second = MakeHandle(CheckFirstLoop(_scheduler, trace, 4, 2));
            auto third = MakeHandle(CheckFirstLoop(_scheduler, trace, 4, 3));
            StartInBackground(first);
            StartInBackground(second);
            StartInBackground(third);
            ASSERT_EQ(_scheduler.GetBackgroundSliceCount(), 3u);

            EXPECT_EQ(_scheduler.ProcessBackgroundSlices(budget), 1u);

            ASSERT_EQ(trace.events.size(), 1u) << "One slice, one unit of work, then it parks again";
            EXPECT_EQ(trace.events[0], 1);
            EXPECT_EQ(_scheduler.GetBackgroundSliceCount(), 3u);

            // Let the three tasks finish: one slice per drain, so the loop must end by itself
            size_t drains = 1;
            while (_scheduler.GetBackgroundSliceCount() > 0 && drains < kDrainLimit) {
                EXPECT_EQ(_scheduler.ProcessBackgroundSlices(budget), 1u);
                ++drains;
            }
            EXPECT_TRUE(first->IsDone());
            EXPECT_TRUE(second->IsDone());
            EXPECT_TRUE(third->IsDone());
        }
    }

    TEST_F(TaskSystemBudgetTest, ParkedTasksAreResumedInFifoOrder) {
        Trace trace;
        EndFirstFrame();
        auto a = MakeHandle(CheckFirstLoop(_scheduler, trace, 2, 1));
        auto b = MakeHandle(CheckFirstLoop(_scheduler, trace, 2, 2));
        auto c = MakeHandle(CheckFirstLoop(_scheduler, trace, 2, 3));
        StartInBackground(a);
        StartInBackground(b);
        StartInBackground(c);

        // One unit is the whole budget, so a drain runs one slice
        size_t drains = 0;
        while (_scheduler.GetBackgroundSliceCount() > 0 && drains < kDrainLimit) {
            EXPECT_EQ(_scheduler.ProcessBackgroundSlices(kUnit), 1u);
            ++drains;
        }

        const int expected[] = {1, 2, 3, 1, 2, 3};
        ASSERT_EQ(trace.events.size(), 6u);
        for (size_t i = 0; i < trace.events.size(); ++i) {
            EXPECT_EQ(trace.events[i], expected[i]) << "at " << i;
        }
        EXPECT_TRUE(a->IsDone() && b->IsDone() && c->IsDone());
    }

    TEST_F(TaskSystemBudgetTest, AnExplicitYieldFromABackgroundJobIsParkedAndResumedInTheSameDrainWhileTimeRemains) {
        Trace trace;
        auto handle = MakeHandle(ExplicitYields(_scheduler, trace));

        StartInBackground(handle);  // before the first frame: an explicit Yield parks anyway

        ASSERT_EQ(trace.events.size(), 1u);
        EXPECT_EQ(_scheduler.GetBackgroundSliceCount(), 1u);
        EXPECT_FALSE(handle->IsDone());

        // The fake clock does not move, so the budget is never spent: both parked slices run in this drain
        EXPECT_EQ(_scheduler.ProcessBackgroundSlices(std::chrono::milliseconds(5)), 2u);

        ASSERT_EQ(trace.events.size(), 3u);
        EXPECT_EQ(trace.events[2], 3);
        EXPECT_TRUE(handle->IsDone());
        EXPECT_EQ(_scheduler.GetBackgroundSliceCount(), 0u);
    }

    TEST_F(TaskSystemBudgetTest, AMainThreadJobKeepsItsYieldOnTheMainThreadQueueAndIsNeverBudgeted) {
        Trace trace;
        EndFirstFrame();
        EXPECT_FALSE(_scheduler.ShouldYield()) << "Outside any job";
        auto handle = MakeHandle(MainThreadJob(_scheduler, trace));

        StartOnMainThread(handle);

        ASSERT_EQ(trace.events.size(), 1u);
        EXPECT_FALSE(trace.shouldYieldSeen) << "The window is spent, but a main-thread job is not budgeted";
        EXPECT_EQ(_scheduler.GetBackgroundSliceCount(), 0u);
        EXPECT_EQ(_scheduler.GetMainThreadQueueSize(), 1u);

        EXPECT_EQ(_scheduler.ProcessBackgroundSlices(kBudget), 0u);
        EXPECT_EQ(trace.events.size(), 1u) << "The drain of the cooperative queue does not run the main-thread queue";

        _scheduler.ProcessMainThreadTasks();
        ASSERT_EQ(trace.events.size(), 2u);
        EXPECT_TRUE(handle->IsDone());
    }

    TEST_F(TaskSystemBudgetTest, CancellationBetweenSlicesEndsTheTaskInTheNextSlice) {
        Trace trace;
        CancellationToken token = CancellationToken::Create();
        EndFirstFrame();
        auto handle = MakeHandle(CancellableLoop(_scheduler, trace, token, 100));

        StartInBackground(handle);
        EXPECT_EQ(_scheduler.ProcessBackgroundSlices(kBudget), 1u);
        EXPECT_FALSE(handle->IsDone());
        const int stepsAtCancel = trace.steps;
        EXPECT_GT(stepsAtCancel, 0);

        token.Cancel();
        // The parked slice resumes after its check: at most one unit more, then the next token check ends it
        EXPECT_EQ(_scheduler.ProcessBackgroundSlices(kBudget), 1u);

        EXPECT_TRUE(handle->IsDone());
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Completed);
        EXPECT_LE(trace.steps, stepsAtCancel + 1);
        EXPECT_EQ(_scheduler.GetBackgroundSliceCount(), 0u);
    }

    TEST_F(TaskSystemBudgetTest, ShutdownDropsTheParkedSlices) {
        Trace trace;
        EndFirstFrame();
        auto handle = MakeHandle(CheckFirstLoop(_scheduler, trace, 5));
        StartInBackground(handle);
        ASSERT_EQ(_scheduler.GetBackgroundSliceCount(), 1u);

        _scheduler.Shutdown();

        EXPECT_EQ(_scheduler.GetBackgroundSliceCount(), 0u);
        EXPECT_EQ(_scheduler.ProcessBackgroundSlices(kBudget), 0u);
        EXPECT_FALSE(handle->IsDone());

        // What TaskManager::Shutdown does next: the parked task is cancelled and its frame is freed with the handle
        Detail::LiveTasks::GetInstance().AbandonAll();
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Cancelled);
    }

    TEST_F(TaskSystemBudgetTest, ShouldYieldIsFalseOnceTheSchedulerIsStopped) {
        EndFirstFrame();
        bool beforeShutdown = false;
        bool afterShutdown = true;
        _scheduler.Schedule(
            [&]() {
                beforeShutdown = _scheduler.ShouldYield();
                _scheduler.Shutdown();
                // Shutdown also sets the deadline to max, which alone would make ShouldYield false.
                // Put the fake clock past any deadline so that only the stopped flag can answer false.
                g_fakeNow = (TimePoint::max)();
                afterShutdown = _scheduler.ShouldYield();
            },
            TaskPriority::Normal, ThreadType::Background);

        EXPECT_TRUE(beforeShutdown) << "The window is spent between drains";
        EXPECT_FALSE(afterShutdown) << "A stopped scheduler (its pool may be gone) never asks for a yield";
    }

    TEST_F(TaskSystemBudgetTest, AChildStartedInsideASliceRunsAfterItAndTheJobFlagIsRestored) {
        Trace childTrace;
        Trace trace;
        auto child = MakeHandle(ChildTask(_scheduler, childTrace));
        auto parent = MakeHandle(ParentTask(_scheduler, trace, child));
        StartInBackground(parent);  // parks on its explicit Yield
        ASSERT_EQ(_scheduler.GetBackgroundSliceCount(), 1u);

        // A budget of zero: the window of the drain is spent at once, so a child that runs as a background job
        // of the drain sees ShouldYield() true
        EXPECT_EQ(_scheduler.ProcessBackgroundSlices(Duration::zero()), 1u);

        ASSERT_EQ(trace.events.size(), 2u);
        EXPECT_EQ(trace.events[0], 1);
        EXPECT_EQ(trace.events[1], 2) << "The child is queued: it does not run inside the slice";
        ASSERT_EQ(childTrace.events.size(), 1u);
        EXPECT_TRUE(childTrace.shouldYieldSeen) << "The child is a background job of the same drain";
        EXPECT_TRUE(parent->IsDone());
        EXPECT_TRUE(child->IsDone());
        EXPECT_FALSE(_scheduler.ShouldYield()) << "The flag of the job is restored when the drain is over";
    }

    TEST_F(TaskSystemBudgetTest, AHugeBudgetDoesNotOverflow) {
        Trace trace;
        EndFirstFrame();
        auto handle = MakeHandle(CheckFirstLoop(_scheduler, trace, 20));
        StartInBackground(handle);

        EXPECT_EQ(_scheduler.ProcessBackgroundSlices(Duration::max()), 1u);

        EXPECT_EQ(trace.steps, 20) << "The window never ends, so the task runs to its end in one slice";
        EXPECT_TRUE(handle->IsDone());
    }

    TEST(TaskSystemBudgetThreadedPoolTest, NothingIsBudgetedOnAWorkerPool) {
        std::atomic<int> ranOnPool{0};
        std::atomic<bool> shouldYieldOnPool{true};
        ThreadPool pool(2);
        TaskScheduler scheduler;
        scheduler.Initialize(&pool);
        scheduler.SetBudgetClock(&FakeNow);

        EXPECT_EQ(scheduler.ProcessBackgroundSlices(kBudget), 0u);
        EXPECT_FALSE(scheduler.ShouldYield());

        // From a pool job ShouldYield is false too, and a yield of a background job goes back to the pool
        scheduler.Schedule(
            [&]() {
                shouldYieldOnPool = scheduler.ShouldYield();
                scheduler.ScheduleYielded([&]() { ++ranOnPool; }, ThreadType::Background);
                ++ranOnPool;
            },
            TaskPriority::Normal, ThreadType::Background);
        ASSERT_TRUE(WaitFor([&]() { return ranOnPool.load() == 2; }));
        EXPECT_FALSE(shouldYieldOnPool.load());
        EXPECT_EQ(scheduler.GetBackgroundSliceCount(), 0u);

        // A main-thread yield behaves as Schedule: it waits in the main-thread queue
        int ranOnMain = 0;
        scheduler.ScheduleYielded([&]() { ++ranOnMain; }, ThreadType::MainThread);
        EXPECT_EQ(scheduler.GetMainThreadQueueSize(), 1u);
        EXPECT_EQ(scheduler.GetBackgroundSliceCount(), 0u);
        EXPECT_EQ(scheduler.ProcessBackgroundSlices(kBudget), 0u);
        EXPECT_EQ(ranOnMain, 0);
        scheduler.ProcessMainThreadTasks();
        EXPECT_EQ(ranOnMain, 1);

        scheduler.Shutdown();
        pool.Shutdown();
    }

    // =================================================================
    // The singleton TaskManager: the budget API and the real awaitables
    // =================================================================

    namespace {

#if !BE_SINGLE_THREAD
        /// Counts to @p units with a real YieldIfNeeded() before each count; no fake work, so it is safe on a worker.
        Task<int> PoliteCount(int units) {
            int sum = 0;
            for (int i = 0; i < units; ++i) {
                co_await YieldIfNeeded();
                sum += i;
            }
            co_return sum;
        }

        Task<int> ExplicitYieldCount(int yields) {
            for (int i = 0; i < yields; ++i) {
                co_await Yield();
            }
            co_return yields;
        }
#endif

#if BE_SINGLE_THREAD
        /// A unit of fake work per step, YieldIfNeeded() before each; the result is the sum of 0..units-1.
        Task<int> PoliteWork(int units) {
            int sum = 0;
            for (int i = 0; i < units; ++i) {
                co_await YieldIfNeeded();
                DoWork();
                sum += i;
            }
            co_return sum;
        }

        /// A long task that never checks: a coroutine cannot be preempted, so it runs to its end.
        Task<int> GreedyWork(int units) {
            int sum = 0;
            for (int i = 0; i < units; ++i) {
                DoWork();
                sum += i;
            }
            co_return sum;
        }

        Task<int> PoliteCancellable(CancellationToken token, int units, int& steps) {
            for (int i = 0; i < units; ++i) {
                if (token.IsCancelled()) {
                    co_return kCancelledResult;
                }
                co_await YieldIfNeeded();
                DoWork();
                ++steps;
            }
            co_return units;
        }

        Task<int> MainThreadStepper(int& steps) {
            ++steps;
            co_await Yield();
            ++steps;
            co_await Yield();
            ++steps;
            co_return steps;
        }
#endif

        class TaskManagerBudgetTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                ASSERT_TRUE(TaskManager::GetInstance().IsInitialized());
                ASSERT_TRUE(TaskManager::GetInstance().IsMainThread());
                g_fakeNow = TimePoint{};
                _savedBudget = TaskManager::GetInstance().GetFrameBudget();
                TaskManager::GetInstance().SetFrameBudget(kBudget);
                Scheduler().SetBudgetClock(&FakeNow);
            }

            void TearDown() override {
                // Let whatever a failed test left parked run to its end
                for (size_t frame = 0; frame < kDrainLimit && Scheduler().GetBackgroundSliceCount() > 0; ++frame) {
                    Frame();
                }
                Scheduler().SetBudgetClock(nullptr);
                TaskManager::GetInstance().SetFrameBudget(_savedBudget);
            }

            static TaskScheduler& Scheduler() {
                return *TaskManager::GetInstance().GetScheduler();
            }

            /// What TaskManager::Update does (it takes a PassKey, so a test cannot call it).
            static size_t Frame() {
                Scheduler().ProcessDelayedTasks();
                Scheduler().ProcessMainThreadTasks();
                return Scheduler().ProcessBackgroundSlices(TaskManager::GetInstance().GetFrameBudget());
            }

            Duration _savedBudget{};
        };

    }  // namespace

    TEST_F(TaskManagerBudgetTest, TheFrameBudgetIsFourMillisecondsByDefaultAndANegativeValueIsStoredAsZero) {
        EXPECT_EQ(TaskManager::kDefaultFrameBudget, std::chrono::milliseconds(4));

        TaskManager::GetInstance().SetFrameBudget(std::chrono::milliseconds(7));
        EXPECT_EQ(TaskManager::GetInstance().GetFrameBudget(), std::chrono::milliseconds(7));

        TaskManager::GetInstance().SetFrameBudget(-std::chrono::milliseconds(2));
        EXPECT_EQ(TaskManager::GetInstance().GetFrameBudget(), Duration::zero());

        TaskManager::GetInstance().SetFrameBudget(Duration::zero());
        EXPECT_EQ(TaskManager::GetInstance().GetFrameBudget(), Duration::zero());
    }

    TEST_F(TaskManagerBudgetTest, ShouldYieldIsFalseOutsideAnyJob) {
        EXPECT_FALSE(TaskManager::GetInstance().ShouldYield());
        Frame();
        EXPECT_FALSE(TaskManager::GetInstance().ShouldYield()) << "The window counts as spent after a frame, but no job runs";
    }

#if !BE_SINGLE_THREAD
    TEST_F(TaskManagerBudgetTest, OnAThreadedBuildYieldIfNeededNeverParksATask) {
        constexpr int kUnits = 200;
        auto handle = TaskManager::GetInstance().Run(PoliteCount(kUnits));

        // Nothing but the pool runs the task; the cooperative queue stays empty the whole time
        size_t parkedMax = 0;
        EXPECT_TRUE(WaitFor([&]() {
            parkedMax = std::max(parkedMax, Scheduler().GetBackgroundSliceCount());
            return handle->IsDone();
        }));
        handle->Wait();

        EXPECT_EQ(parkedMax, 0u);
        EXPECT_EQ(Scheduler().GetBackgroundSliceCount(), 0u);
        auto result = handle->GetResult();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, kUnits * (kUnits - 1) / 2);
    }

    TEST_F(TaskManagerBudgetTest, OnAThreadedBuildAnExplicitYieldStillGoesBackToThePool) {
        auto handle = TaskManager::GetInstance().Run(ExplicitYieldCount(3));

        handle->Wait();  // resumed on a worker each time: nobody drains anything here

        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Completed);
        auto result = handle->GetResult();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, 3);
        EXPECT_EQ(Scheduler().GetBackgroundSliceCount(), 0u);
        EXPECT_EQ(Frame(), 0u) << "No cooperative queue in a threaded build";
    }
#endif

#if BE_SINGLE_THREAD
    TEST_F(TaskManagerBudgetTest, ARunAfterAFrameReturnsWithAPoliteTaskUnfinishedAndFinishesOverFrames) {
        EXPECT_EQ(Frame(), 0u);
        constexpr int kUnits = 10;

        auto handle = TaskManager::GetInstance().Run(PoliteWork(kUnits));

        EXPECT_FALSE(handle->IsDone()) << "The first slice ends at the first check";
        EXPECT_EQ(Scheduler().GetBackgroundSliceCount(), 1u);

        size_t frames = 0;
        while (!handle->IsDone() && frames < kDrainLimit) {
            const TimePoint before = g_fakeNow;
            EXPECT_EQ(Frame(), 1u);
            EXPECT_LE(g_fakeNow - before, kBudget + kUnit);
            ++frames;
        }

        // 3 units per frame: 3, 6, 9, then the last unit
        EXPECT_EQ(frames, 4u);
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Completed);
        auto result = handle->GetResult();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, kUnits * (kUnits - 1) / 2);
    }

    TEST_F(TaskManagerBudgetTest, ATaskThatNeverChecksStillRunsToItsEndInline) {
        EXPECT_EQ(Frame(), 0u);

        constexpr int kUnits = 30;
        auto handle = TaskManager::GetInstance().Run(GreedyWork(kUnits));

        EXPECT_TRUE(handle->IsDone());
        auto result = handle->GetResult();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, kUnits * (kUnits - 1) / 2);
        EXPECT_EQ(Scheduler().GetBackgroundSliceCount(), 0u);
    }

    TEST_F(TaskManagerBudgetTest, TheParkedTaskIsCountedAsPending) {
        EXPECT_EQ(Frame(), 0u);
        const size_t pendingBefore = TaskManager::GetInstance().GetPendingTaskCount();

        auto handle = TaskManager::GetInstance().Run(PoliteWork(5));

        ASSERT_FALSE(handle->IsDone());
        EXPECT_EQ(TaskManager::GetInstance().GetPendingTaskCount(), pendingBefore + 1);

        size_t frames = 0;
        while (!handle->IsDone() && frames < kDrainLimit) {
            Frame();
            ++frames;
        }
        EXPECT_EQ(TaskManager::GetInstance().GetPendingTaskCount(), pendingBefore);
    }

    TEST_F(TaskManagerBudgetTest, TwoPoliteTasksAndAMainThreadTaskDoNotStarveEachOther) {
        EXPECT_EQ(Frame(), 0u);
        constexpr int kUnits = 9;
        int mainSteps = 0;

        auto first = TaskManager::GetInstance().Run(PoliteWork(kUnits));
        auto second = TaskManager::GetInstance().Run(PoliteWork(kUnits));
        auto onMain = TaskManager::GetInstance().RunOnMainThread(MainThreadStepper(mainSteps));

        size_t frames = 0;
        while (!(first->IsDone() && second->IsDone() && onMain->IsDone()) && frames < kDrainLimit) {
            Frame();
            ++frames;
        }

        EXPECT_TRUE(first->IsDone());
        EXPECT_TRUE(second->IsDone());
        EXPECT_TRUE(onMain->IsDone());
        EXPECT_EQ(mainSteps, 3);
        EXPECT_LT(frames, kDrainLimit);
        auto firstResult = first->GetResult();
        auto secondResult = second->GetResult();
        ASSERT_TRUE(firstResult.has_value() && secondResult.has_value());
        EXPECT_EQ(*firstResult, kUnits * (kUnits - 1) / 2);
        EXPECT_EQ(*secondResult, kUnits * (kUnits - 1) / 2);
    }

    TEST_F(TaskManagerBudgetTest, CancellationByTokenBetweenSlicesEndsTheTaskInTheNextFrame) {
        EXPECT_EQ(Frame(), 0u);
        CancellationToken token = CancellationToken::Create();
        int steps = 0;
        auto handle = TaskManager::GetInstance().Run(PoliteCancellable(token, 100, steps));
        ASSERT_FALSE(handle->IsDone());

        EXPECT_EQ(Frame(), 1u);
        ASSERT_FALSE(handle->IsDone());
        const int stepsAtCancel = steps;
        EXPECT_GT(stepsAtCancel, 0);

        token.Cancel();
        EXPECT_EQ(Frame(), 1u);

        EXPECT_TRUE(handle->IsDone());
        EXPECT_LE(steps, stepsAtCancel + 1);
        auto result = handle->GetResult();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, kCancelledResult);
        EXPECT_EQ(Scheduler().GetBackgroundSliceCount(), 0u);
    }
#endif

}  // namespace BECore::Tests
