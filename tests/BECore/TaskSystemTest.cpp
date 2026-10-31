#include <Common/Printers.h>
#include <TaskSystem/CancellationToken.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskGroup.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskQueue.h>
#include <TaskSystem/TaskStatistics.h>
#include <TaskSystem/ThreadPool.h>
#include <chrono>
#include <thread>

namespace BECore::Tests {

    namespace {

        // Helper: simple coroutine returning void
        Task<void> SimpleVoidTask() {
            co_return;
        }

        // Helper: coroutine returning a value
        Task<int> SimpleIntTask(int value) {
            co_return value * 2;
        }

        // Helper: coroutine with nested co_await
        Task<int> NestedTask() {
            auto resultA = co_await SimpleIntTask(10);
            auto resultB = co_await SimpleIntTask(20);

            // On error return 0 (real code needs proper error handling)
            if (!resultA.has_value() || !resultB.has_value()) {
                co_return 0;
            }

            co_return* resultA + *resultB;
        }

        /// Polls @p predicate until it holds or @p timeout expires. Replaces a fixed sleep, which is
        /// either flaky on a loaded machine or needlessly slow.
        template <typename Predicate>
        bool WaitUntil(Predicate&& predicate, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (!predicate()) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return true;
        }

    }  // namespace

    TEST(TaskSystemTest, TaskBasic) {
        {
            auto task = SimpleVoidTask();
            EXPECT_TRUE(task.IsValid());
            EXPECT_FALSE(task.IsDone()) << "Task should not be done before execution";

            auto result = task.GetResult();  // Run to completion
            EXPECT_TRUE(result.has_value()) << "Task should complete successfully";
            EXPECT_TRUE(task.IsDone()) << "Task should be done after GetResult";
        }

        // Move semantics
        {
            auto task1 = SimpleVoidTask();
            auto task2 = std::move(task1);

            EXPECT_FALSE(task1.IsValid()) << "Moved-from task should be invalid";
            EXPECT_TRUE(task2.IsValid()) << "Moved-to task should be valid";
        }
    }

    TEST(TaskSystemTest, TaskWithValue) {
        {
            auto task = SimpleIntTask(21);
            auto result = task.GetResult();
            ASSERT_TRUE(result.has_value()) << "Task should return a value";
            EXPECT_EQ(*result, 42);
        }

        // Nested tasks: (10*2) + (20*2) = 60
        {
            auto task = NestedTask();
            auto result = task.GetResult();
            ASSERT_TRUE(result.has_value()) << "Nested task should return a value";
            EXPECT_EQ(*result, 60);
        }
    }

    TEST(TaskSystemTest, TaskQueue) {
        TaskQueue queue;

        // Push and Pop
        {
            std::atomic<int> counter{0};

            queue.Push([&counter]() { counter++; });
            queue.Push([&counter]() { counter++; });

            EXPECT_EQ(queue.Size(), 2u);

            TaskQueue::TaskFunc task;
            ASSERT_TRUE(queue.TryPop(task)) << "TryPop should succeed";
            task();
            EXPECT_EQ(counter.load(), 1);

            ASSERT_TRUE(queue.TryPop(task)) << "Second TryPop should succeed";
            task();
            EXPECT_EQ(counter.load(), 2);

            EXPECT_FALSE(queue.TryPop(task)) << "TryPop on empty queue should fail";
        }

        // TrySteal (work stealing)
        {
            queue.Push([]() {});
            queue.Push([]() {});

            TaskQueue::TaskFunc task;
            EXPECT_TRUE(queue.TrySteal(task)) << "TrySteal should succeed";
            EXPECT_EQ(queue.Size(), 1u) << "Queue should have 1 task after steal";
        }

        // Stop
        {
            queue.Stop();
            EXPECT_TRUE(queue.IsStopped());
        }
    }

    TEST(TaskSystemTest, ThreadPoolExecutesTasks) {
        ThreadPool pool(2);
        std::atomic<int> counter{0};

        pool.Submit([&counter]() { counter++; });
        pool.Submit([&counter]() { counter++; });
        pool.Submit([&counter]() { counter++; });

        EXPECT_TRUE(WaitUntil([&counter]() { return counter.load() == 3; })) << "All 3 tasks should be executed, got " << counter.load();

        pool.Shutdown();
        EXPECT_TRUE(pool.IsStopped()) << "Pool should be stopped after Shutdown";
    }

    TEST(TaskSystemTest, ThreadPoolThreadCount) {
        ThreadPool pool(4);
        EXPECT_EQ(pool.GetThreadCount(), 4u);
    }

    TEST(TaskSystemTest, CancellationToken) {
        // Basic cancellation
        {
            auto token = CancellationToken::Create();
            EXPECT_TRUE(token.IsValid());
            EXPECT_FALSE(token.IsCancelled()) << "Token should not be cancelled initially";

            token.Cancel();
            EXPECT_TRUE(token.IsCancelled()) << "Token should be cancelled after Cancel()";
        }

        // CheckCancellation (instead of ThrowIfCancelled)
        {
            auto token = CancellationToken::Create();
            token.Cancel();

            auto result = token.CheckCancellation();
            ASSERT_FALSE(result.has_value()) << "CheckCancellation should return an error on a cancelled token";
            EXPECT_TRUE(result.error() == TaskError::Cancelled) << "Error should be Cancelled";
        }
    }

    TEST(TaskSystemTest, TaskGroupOperations) {
        TaskGroup group;
        EXPECT_EQ(group.Size(), 0u) << "New group should be empty";

        auto handle1 = MakeIntrusive<TaskHandle<void>>(SimpleVoidTask());
        auto handle2 = MakeIntrusive<TaskHandle<void>>(SimpleVoidTask());

        group.Add(handle1);
        group.Add(handle2);

        EXPECT_EQ(group.Size(), 2u);
    }

    TEST(TaskSystemTest, TaskGroupCancelAll) {
        TaskGroup group;
        auto handle = MakeIntrusive<TaskHandle<void>>(SimpleVoidTask());
        group.Add(handle);

        group.CancelAll();
        EXPECT_TRUE(handle->IsCancelled()) << "Handle should be cancelled after CancelAll";
    }

    TEST(TaskSystemTest, TaskStatistics) {
        TaskStatistics stats;

        stats.RecordTaskScheduled();
        stats.RecordTaskScheduled();
        stats.RecordTaskCompleted(false, 10.0);
        stats.RecordTaskCancelled();

        auto snapshot = stats.GetSnapshot();
        EXPECT_EQ(snapshot.totalTasksScheduled, 2u);
        EXPECT_EQ(snapshot.totalTasksCompleted, 1u);
        EXPECT_EQ(snapshot.totalTasksCancelled, 1u);
        EXPECT_EQ(snapshot.backgroundTasksCompleted, 1u);

        stats.Reset();
        snapshot = stats.GetSnapshot();
        EXPECT_EQ(snapshot.totalTasksScheduled, 0u) << "After reset, scheduled should be 0";
        EXPECT_EQ(snapshot.totalTasksCompleted, 0u) << "After reset, completed should be 0";
    }

}  // namespace BECore::Tests
