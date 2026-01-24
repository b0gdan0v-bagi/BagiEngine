#include "TaskSystemTest.h"

#include <thread>
#include <atomic>

namespace BECore::Tests {

    // Helper: простая корутина возвращающая void
    Task<void> SimpleVoidTask() {
        co_return;
    }

    // Helper: корутина возвращающая значение
    Task<int> SimpleIntTask(int value) {
        co_return value * 2;
    }

    // Helper: корутина с вложенным co_await
    Task<int> NestedTask() {
        int a = co_await SimpleIntTask(10);
        int b = co_await SimpleIntTask(20);
        co_return a + b;
    }

    bool TaskSystemTest::Run() {
        bool allPassed = true;

        LOG_INFO("[TaskSystemTest] Running Task basic tests...");
        if (!TestTaskBasic()) {
            LOG_ERROR("[TaskSystemTest] TestTaskBasic FAILED");
            allPassed = false;
        }

        LOG_INFO("[TaskSystemTest] Running Task with value tests...");
        if (!TestTaskWithValue()) {
            LOG_ERROR("[TaskSystemTest] TestTaskWithValue FAILED");
            allPassed = false;
        }

        LOG_INFO("[TaskSystemTest] Running TaskQueue tests...");
        if (!TestTaskQueue()) {
            LOG_ERROR("[TaskSystemTest] TestTaskQueue FAILED");
            allPassed = false;
        }

        LOG_INFO("[TaskSystemTest] Running ThreadPool tests...");
        if (!TestThreadPool()) {
            LOG_ERROR("[TaskSystemTest] TestThreadPool FAILED");
            allPassed = false;
        }

        LOG_INFO("[TaskSystemTest] Running CancellationToken tests...");
        if (!TestCancellationToken()) {
            LOG_ERROR("[TaskSystemTest] TestCancellationToken FAILED");
            allPassed = false;
        }

        LOG_INFO("[TaskSystemTest] Running TaskGroup tests...");
        if (!TestTaskGroup()) {
            LOG_ERROR("[TaskSystemTest] TestTaskGroup FAILED");
            allPassed = false;
        }

        LOG_INFO("[TaskSystemTest] Running TaskStatistics tests...");
        if (!TestTaskStatistics()) {
            LOG_ERROR("[TaskSystemTest] TestTaskStatistics FAILED");
            allPassed = false;
        }

        if (allPassed) {
            LOG_INFO("[TaskSystemTest] All tests PASSED");
        }

        return allPassed;
    }

    bool TaskSystemTest::TestTaskBasic() {
        // Test 1: Create and run simple void task
        {
            auto task = SimpleVoidTask();
            EXPECT(task.IsValid(), "Task should be valid");
            EXPECT(!task.IsDone(), "Task should not be done before execution");
            
            task.GetResult(); // Run to completion
            EXPECT(task.IsDone(), "Task should be done after GetResult");
        }

        // Test 2: Move semantics
        {
            auto task1 = SimpleVoidTask();
            auto task2 = std::move(task1);
            
            EXPECT(!task1.IsValid(), "Moved-from task should be invalid");
            EXPECT(task2.IsValid(), "Moved-to task should be valid");
        }

        return true;
    }

    bool TaskSystemTest::TestTaskWithValue() {
        // Test 1: Task returning int
        {
            auto task = SimpleIntTask(21);
            int result = task.GetResult();
            EXPECT(result == 42, "SimpleIntTask(21) should return 42");
        }

        // Test 2: Nested tasks
        {
            auto task = NestedTask();
            int result = task.GetResult();
            // NestedTask: (10*2) + (20*2) = 20 + 40 = 60
            EXPECT(result == 60, "NestedTask should return 60");
        }

        return true;
    }

    bool TaskSystemTest::TestTaskQueue() {
        TaskQueue queue;
        
        // Test 1: Push and Pop
        {
            std::atomic<int> counter{0};
            
            queue.Push([&counter]() { counter++; });
            queue.Push([&counter]() { counter++; });
            
            EXPECT(queue.Size() == 2, "Queue should have 2 tasks");
            
            TaskQueue::TaskFunc task;
            EXPECT(queue.TryPop(task), "TryPop should succeed");
            task();
            EXPECT(counter == 1, "Counter should be 1 after first task");
            
            EXPECT(queue.TryPop(task), "Second TryPop should succeed");
            task();
            EXPECT(counter == 2, "Counter should be 2 after second task");
            
            EXPECT(!queue.TryPop(task), "TryPop on empty queue should fail");
        }

        // Test 2: TrySteal (work stealing)
        {
            queue.Push([]() {});
            queue.Push([]() {});
            
            TaskQueue::TaskFunc task;
            EXPECT(queue.TrySteal(task), "TrySteal should succeed");
            EXPECT(queue.Size() == 1, "Queue should have 1 task after steal");
        }

        // Test 3: Stop
        {
            queue.Stop();
            EXPECT(queue.IsStopped(), "Queue should be stopped");
        }

        return true;
    }

    bool TaskSystemTest::TestThreadPool() {
        // Test 1: Basic execution
        {
            ThreadPool pool(2);
            std::atomic<int> counter{0};
            
            pool.Submit([&counter]() { counter++; });
            pool.Submit([&counter]() { counter++; });
            pool.Submit([&counter]() { counter++; });
            
            // Wait for completion
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            EXPECT(counter == 3, "All 3 tasks should be executed");
            
            pool.Shutdown();
            EXPECT(pool.IsStopped(), "Pool should be stopped after Shutdown");
        }

        // Test 2: Thread count
        {
            ThreadPool pool(4);
            EXPECT(pool.GetThreadCount() == 4, "Thread count should be 4");
        }

        return true;
    }

    bool TaskSystemTest::TestCancellationToken() {
        // Test 1: Basic cancellation
        {
            auto token = CancellationToken::Create();
            EXPECT(token.IsValid(), "Token should be valid");
            EXPECT(!token.IsCancelled(), "Token should not be cancelled initially");
            
            token.Cancel();
            EXPECT(token.IsCancelled(), "Token should be cancelled after Cancel()");
        }

        // Test 2: ThrowIfCancelled
        {
            auto token = CancellationToken::Create();
            token.Cancel();
            
            bool exceptionThrown = false;
            try {
                token.ThrowIfCancelled();
            } catch (const TaskCancelledException&) {
                exceptionThrown = true;
            }
            
            EXPECT(exceptionThrown, "ThrowIfCancelled should throw on cancelled token");
        }

        return true;
    }

    bool TaskSystemTest::TestTaskGroup() {
        // Test 1: Basic group operations
        {
            TaskGroup group;
            EXPECT(group.Size() == 0, "New group should be empty");
            
            // Add mock handles
            auto handle1 = MakeIntrusive<TaskHandle<void>>(SimpleVoidTask());
            auto handle2 = MakeIntrusive<TaskHandle<void>>(SimpleVoidTask());
            
            group.Add(handle1);
            group.Add(handle2);
            
            EXPECT(group.Size() == 2, "Group should have 2 handles");
        }

        // Test 2: CancelAll
        {
            TaskGroup group;
            auto handle = MakeIntrusive<TaskHandle<void>>(SimpleVoidTask());
            group.Add(handle);
            
            group.CancelAll();
            EXPECT(handle->IsCancelled(), "Handle should be cancelled after CancelAll");
        }

        return true;
    }

    bool TaskSystemTest::TestTaskStatistics() {
        TaskStatistics stats;

        // Test 1: Recording
        {
            stats.RecordTaskScheduled();
            stats.RecordTaskScheduled();
            stats.RecordTaskCompleted(false, 10.0);
            stats.RecordTaskCancelled();
            
            auto snapshot = stats.GetSnapshot();
            EXPECT(snapshot.totalTasksScheduled == 2, "Should have 2 scheduled tasks");
            EXPECT(snapshot.totalTasksCompleted == 1, "Should have 1 completed task");
            EXPECT(snapshot.totalTasksCancelled == 1, "Should have 1 cancelled task");
            EXPECT(snapshot.backgroundTasksCompleted == 1, "Should have 1 background task completed");
        }

        // Test 2: Reset
        {
            stats.Reset();
            auto snapshot = stats.GetSnapshot();
            EXPECT(snapshot.totalTasksScheduled == 0, "After reset, scheduled should be 0");
            EXPECT(snapshot.totalTasksCompleted == 0, "After reset, completed should be 0");
        }

        return true;
    }

} // namespace BECore::Tests
