#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <TaskSystem/Awaitables.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskGroup.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/TaskScheduler.h>
#include <TaskSystem/ThreadPool.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        /// Moves the rest of a coroutine to the background of @p scheduler, like SwitchToBackground does for the
        /// scheduler of the TaskManager. Lets these tests use a scheduler of their own over an inline pool.
        struct SwitchToBackgroundOf {
            TaskScheduler& scheduler;

            [[nodiscard]] bool await_ready() const noexcept {
                return false;
            }

            void await_suspend(std::coroutine_handle<> handle) noexcept {
                scheduler.Schedule([handle]() mutable { handle.resume(); }, TaskPriority::Normal, ThreadType::Background);
            }

            void await_resume() const noexcept {}
        };

        /// Records the steps and the thread of a coroutine that hops to the background twice.
        struct HopLog {
            eastl::vector<int> steps;
            std::thread::id threadAtEnd;
        };

        Task<void> HoppingTask(TaskScheduler& scheduler, HopLog& log) {
            log.steps.push_back(1);
            co_await SwitchToBackgroundOf{scheduler};
            log.steps.push_back(2);
            co_await SwitchToBackgroundOf{scheduler};
            log.steps.push_back(3);
            log.threadAtEnd = std::this_thread::get_id();
            co_return;
        }

        /// Starts @p handle as a background job of @p scheduler, the way TaskManager::Run does.
        void StartInBackground(TaskScheduler& scheduler, const IntrusivePtr<TaskHandle<void>>& handle) {
            auto* rawHandle = handle.Get();
            scheduler.Schedule([rawHandle]() { rawHandle->Start(); }, TaskPriority::Normal, ThreadType::Background);
        }

        /// A scheduler on its own over an inline pool: no thread exists, the test thread does everything.
        class TaskSystemInlineTest : public ::testing::Test {
        protected:
            void SetUp() override {
                _pool = eastl::make_unique<ThreadPool>(ThreadPool::InlineTag{});
                _scheduler.Initialize(_pool.get());
            }

            void TearDown() override {
                _scheduler.Shutdown();
                _pool->Shutdown();
            }

            TaskScheduler _scheduler;
            eastl::unique_ptr<ThreadPool> _pool;
        };

    }  // namespace

    TEST_F(TaskSystemInlineTest, HasNoThreads) {
        EXPECT_TRUE(_pool->IsInline());
        EXPECT_EQ(_pool->GetThreadCount(), 0u);
        EXPECT_EQ(_pool->GetPendingTaskCount(), 0u);
        EXPECT_FALSE(_pool->IsStopped());
    }

    TEST_F(TaskSystemInlineTest, SubmitRunsTheJobOnTheCallingThreadBeforeItReturns) {
        std::thread::id ranOn;
        bool ran = false;

        _pool->Submit([&] {
            ran = true;
            ranOn = std::this_thread::get_id();
        });

        EXPECT_TRUE(ran) << "The job must be done when Submit returns";
        EXPECT_EQ(ranOn, std::this_thread::get_id());
    }

    TEST_F(TaskSystemInlineTest, SubmitToThreadRunsTheJobAtOnceWhateverTheIndex) {
        int ran = 0;

        _pool->SubmitToThread(0, [&] { ++ran; });
        _pool->SubmitToThread(7, [&] { ++ran; });

        EXPECT_EQ(ran, 2);
    }

    TEST_F(TaskSystemInlineTest, NestedSubmitIsQueuedAndRunsAfterTheCurrentJobInOrder) {
        eastl::vector<int> order;
        size_t pendingInsideJob = 0;

        _pool->Submit([&] {
            order.push_back(1);
            _pool->Submit([&] {
                order.push_back(3);
                _pool->Submit([&] { order.push_back(5); });  // queued behind the sibling job 4
                order.push_back(4);
            });
            _pool->Submit([&] { order.push_back(4); });
            pendingInsideJob = _pool->GetPendingTaskCount();
            order.push_back(2);
        });

        // 1 and 2 are the outer job; then the first nested job (3, 4), the second one (4), and last the job 5
        // that the first nested job queued.
        EXPECT_EQ(pendingInsideJob, 2u) << "A nested job must wait in the queue, not run inside the outer one";
        const int expected[] = {1, 2, 3, 4, 4, 5};
        ASSERT_EQ(order.size(), 6u);
        for (size_t i = 0; i < order.size(); ++i) {
            EXPECT_EQ(order[i], expected[i]) << "at " << i;
        }
        EXPECT_EQ(_pool->GetPendingTaskCount(), 0u) << "The outer call must drain the queue before it returns";
    }

    TEST_F(TaskSystemInlineTest, PoolRunsJobsAgainAfterADrain) {
        int ran = 0;
        _pool->Submit([&] { _pool->Submit([&] { ++ran; }); });
        _pool->Submit([&] { ++ran; });

        EXPECT_EQ(ran, 2) << "A drained pool must not stay in the state of a running job";
    }

    TEST_F(TaskSystemInlineTest, CoroutineSwitchingToBackgroundCompletesDuringStart) {
        HopLog log;
        auto handle = MakeIntrusive<TaskHandle<void>>(HoppingTask(_scheduler, log));

        StartInBackground(_scheduler, handle);

        EXPECT_TRUE(handle->IsDone()) << "Every hop is a job of the inline pool, so the task is done when Schedule returns";
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Completed);
        ASSERT_EQ(log.steps.size(), 3u);
        EXPECT_EQ(log.steps[0], 1);
        EXPECT_EQ(log.steps[1], 2);
        EXPECT_EQ(log.steps[2], 3);
        EXPECT_EQ(log.threadAtEnd, std::this_thread::get_id());
    }

    TEST_F(TaskSystemInlineTest, WaitAllOverSuchTasksReturnsAtOnce) {
        HopLog first;
        HopLog second;
        TaskGroup group;
        auto a = MakeIntrusive<TaskHandle<void>>(HoppingTask(_scheduler, first));
        auto b = MakeIntrusive<TaskHandle<void>>(HoppingTask(_scheduler, second));
        group.Add(a);
        group.Add(b);

        StartInBackground(_scheduler, a);
        StartInBackground(_scheduler, b);

        EXPECT_TRUE(group.AllDone());
        group.WaitAll();  // would block for ever if a job were only queued for a later Update
        EXPECT_EQ(first.steps.size(), 3u);
        EXPECT_EQ(second.steps.size(), 3u);
    }

    TEST_F(TaskSystemInlineTest, SchedulerStillReleasesDelayedAndMainThreadTasks) {
        eastl::vector<int> order;

        _scheduler.Schedule([&] { order.push_back(1); }, TaskPriority::Normal, ThreadType::MainThread);
        _scheduler.ScheduleDelayed([&] { order.push_back(2); }, std::chrono::nanoseconds(0), TaskPriority::Normal, ThreadType::Background);
        _scheduler.ScheduleDelayed([&] { order.push_back(3); }, std::chrono::nanoseconds(0), TaskPriority::Normal, ThreadType::MainThread);
        _scheduler.ScheduleDelayed([&] { order.push_back(4); }, std::chrono::seconds(1), TaskPriority::Normal, ThreadType::MainThread, DelayClock::GameTime);

        EXPECT_TRUE(order.empty()) << "A main-thread task waits for ProcessMainThreadTasks";

        _scheduler.ProcessDelayedTasks();
        // The due background task ran inline when it was released; the main-thread ones wait for their queue.
        ASSERT_EQ(order.size(), 1u);
        EXPECT_EQ(order[0], 2);

        _scheduler.ProcessMainThreadTasks();
        ASSERT_EQ(order.size(), 3u);

        _scheduler.AdvanceGameTime(1.0);
        _scheduler.ProcessDelayedTasks();
        _scheduler.ProcessMainThreadTasks();
        ASSERT_EQ(order.size(), 4u);
        EXPECT_EQ(order[3], 4);
    }

    TEST_F(TaskSystemInlineTest, ShutdownTwiceIsHarmlessAndLaterJobsAreDropped) {
        _pool->Shutdown();
        _pool->Shutdown();
        EXPECT_TRUE(_pool->IsStopped());

        bool ran = false;
        _pool->Submit([&] { ran = true; });
        EXPECT_FALSE(ran) << "A stopped pool takes no more jobs";
    }

#if BE_SINGLE_THREAD
    namespace {

        /// The real TaskManager of a single-thread build: start-up and the frame work with no worker thread.
        class TaskManagerSingleThreadTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }
        };

        Task<int> HopAndAnswer(int value) {
            co_await SwitchToBackground();
            co_return value * 2;
        }

        Task<int> HopToBackgroundAndBack(int value) {
            co_await SwitchToBackground();
            co_await SwitchToMainThread();
            co_return value + 1;
        }

    }  // namespace

    TEST_F(TaskManagerSingleThreadTest, HasNoWorkerThreads) {
        EXPECT_EQ(CoreManager::GetTaskManager().GetWorkerCount(), 0u);
        ASSERT_NE(CoreManager::GetTaskManager().GetThreadPool(), nullptr);
        EXPECT_TRUE(CoreManager::GetTaskManager().GetThreadPool()->IsInline());
        EXPECT_TRUE(CoreManager::GetTaskManager().IsMainThread());
    }

    TEST_F(TaskManagerSingleThreadTest, RunWithBackgroundHopFinishesBeforeRunReturns) {
        auto handle = CoreManager::GetTaskManager().Run(HopAndAnswer(21));

        EXPECT_TRUE(handle->IsDone());
        auto result = handle->GetResult();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, 42);
    }

    TEST_F(TaskManagerSingleThreadTest, BackgroundHopThenMainThreadHopFinishesBeforeRunReturns) {
        auto handle = CoreManager::GetTaskManager().Run(HopToBackgroundAndBack(41));

        EXPECT_TRUE(handle->IsDone());
        auto result = handle->GetResult();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, 42);
    }

    TEST_F(TaskManagerSingleThreadTest, StartUpWaitAllOverBackgroundTasksDoesNotBlock) {
        TaskGroup group;
        auto first = CoreManager::GetTaskManager().Run(HopAndAnswer(1));
        auto second = CoreManager::GetTaskManager().Run(HopAndAnswer(2));
        group.Add(first);
        group.Add(second);

        group.WaitAll();

        EXPECT_TRUE(group.AllDone());
    }
#endif

}  // namespace BECore::Tests
