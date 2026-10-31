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

        /// Spins until @p gate opens, then raises @p finished. Lets a test hold a task "running" on a worker
        /// for exactly as long as it needs, with no sleeps.
        Task<void> GatedTask(std::atomic<bool>& gate, std::atomic<bool>& finished) {
            while (!gate.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            finished.store(true, std::memory_order_release);
            co_return;
        }

        Task<void> FlagTask(std::atomic<bool>& ran) {
            ran.store(true, std::memory_order_release);
            co_return;
        }

        /// Starts @p task on @p pool. The pool must be destroyed before the returned handle: the queued lambda
        /// keeps a raw pointer, exactly like TaskManager::Run.
        IntrusivePtr<TaskHandle<void>> StartOnPool(ThreadPool& pool, Task<void> task) {
            auto handle = MakeIntrusive<TaskHandle<void>>(std::move(task));
            auto* rawHandle = handle.Get();
            pool.Submit([rawHandle]() { rawHandle->Start(); });
            return handle;
        }

        /// Runs handle->Wait() on its own thread so that a Wait() that never returns fails the test instead of
        /// hanging it. The thread owns its references, so a stuck one can be detached without dangling.
        struct AsyncWaiter {
            explicit AsyncWaiter(IntrusivePtr<TaskHandle<void>> handle)
                : thread([handle, flag = returned]() {
                      handle->Wait();
                      flag->store(true, std::memory_order_release);
                  }) {}

            ~AsyncWaiter() {
                if (!thread.joinable()) {
                    return;
                }
                if (returned->load(std::memory_order_acquire)) {
                    thread.join();
                } else {
                    thread.detach();  // the test has already failed; do not hang the run on top of it
                }
            }

            AsyncWaiter(const AsyncWaiter&) = delete;
            AsyncWaiter& operator=(const AsyncWaiter&) = delete;

            [[nodiscard]] bool HasReturned() const {
                return returned->load(std::memory_order_acquire);
            }

            eastl::shared_ptr<std::atomic<bool>> returned = eastl::make_shared<std::atomic<bool>>(false);
            std::thread thread;
        };

        constexpr auto kLetWaiterBlock = std::chrono::milliseconds(30);

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

    TEST(TaskSystemTest, WaitBlocksUntilTaskFinishes) {
        std::atomic<bool> gate{false};
        std::atomic<bool> finished{false};
        IntrusivePtr<TaskHandle<void>> handle;
        ThreadPool pool(1);
        handle = StartOnPool(pool, GatedTask(gate, finished));

        AsyncWaiter waiter(handle);
        std::this_thread::sleep_for(kLetWaiterBlock);
        EXPECT_FALSE(waiter.HasReturned()) << "Wait() returned while the task was still running";

        gate.store(true, std::memory_order_release);
        EXPECT_TRUE(WaitUntil([&waiter]() { return waiter.HasReturned(); })) << "Wait() did not return after the task finished";
        EXPECT_TRUE(finished.load(std::memory_order_acquire)) << "Wait() returned before the task body completed";
        EXPECT_TRUE(handle->IsDone());
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Completed);
    }

    TEST(TaskSystemTest, WaitWakesAllWaiters) {
        std::atomic<bool> gate{false};
        std::atomic<bool> finished{false};
        IntrusivePtr<TaskHandle<void>> handle;
        ThreadPool pool(1);
        handle = StartOnPool(pool, GatedTask(gate, finished));

        AsyncWaiter first(handle);
        AsyncWaiter second(handle);
        AsyncWaiter third(handle);
        std::this_thread::sleep_for(kLetWaiterBlock);
        EXPECT_FALSE(first.HasReturned() || second.HasReturned() || third.HasReturned());

        gate.store(true, std::memory_order_release);
        EXPECT_TRUE(WaitUntil([&]() { return first.HasReturned() && second.HasReturned() && third.HasReturned(); })) << "Not every waiter was woken when the task finished";
    }

    TEST(TaskSystemTest, WaitAfterCompletionReturnsImmediately) {
        // The task finishes inline in Start(), so the signal is already set when Wait() is called.
        auto handle = MakeIntrusive<TaskHandle<int>>(SimpleIntTask(21));
        handle->Start();
        EXPECT_TRUE(handle->IsDone());

        handle->Wait();

        auto result = handle->GetResult();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, 42);
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Completed);
    }

    TEST(TaskSystemTest, WaitOnEmptyHandleReturns) {
        auto handle = MakeIntrusive<TaskHandle<void>>(Task<void>{});
        EXPECT_TRUE(handle->IsDone());

        AsyncWaiter waiter(handle);
        EXPECT_TRUE(WaitUntil([&waiter]() { return waiter.HasReturned(); })) << "Wait() on a handle without a task never returned";
    }

    TEST(TaskSystemTest, WaitReturnsForTaskCancelledBeforeStart) {
        std::atomic<bool> ran{false};
        auto handle = MakeIntrusive<TaskHandle<void>>(FlagTask(ran));

        handle->Cancel();
        handle->Start();  // a cancelled handle is no longer Pending, so the body must not run
        EXPECT_FALSE(ran.load(std::memory_order_acquire)) << "A task cancelled before Start() must not run";

        AsyncWaiter waiter(handle);
        EXPECT_TRUE(WaitUntil([&waiter]() { return waiter.HasReturned(); })) << "Wait() on a cancelled, never-started task blocks forever";

        auto result = handle->GetResult();
        ASSERT_FALSE(result.has_value());
        EXPECT_TRUE(result.error() == TaskError::Cancelled);
    }

    TEST(TaskSystemTest, CancelFromAnotherThreadWakesWait) {
        std::atomic<bool> ran{false};
        auto handle = MakeIntrusive<TaskHandle<void>>(FlagTask(ran));  // never started, so only Cancel() can end the wait

        AsyncWaiter waiter(handle);
        std::this_thread::sleep_for(kLetWaiterBlock);
        EXPECT_FALSE(waiter.HasReturned()) << "Wait() returned for a task that is neither finished nor cancelled";

        handle->Cancel();
        EXPECT_TRUE(WaitUntil([&waiter]() { return waiter.HasReturned(); })) << "Cancel() did not wake the blocked Wait()";
    }

    TEST(TaskSystemTest, LinkedTokenSeesParentCancellation) {
        auto parent = CancellationToken::Create();
        auto child = parent.CreateLinked();
        auto grandchild = child.CreateLinked();

        EXPECT_TRUE(child.IsValid());
        EXPECT_FALSE(child.IsCancelled());
        EXPECT_FALSE(grandchild.IsCancelled());

        parent.Cancel();

        EXPECT_TRUE(child.IsCancelled()) << "Cancelling the parent must cancel the child";
        EXPECT_TRUE(grandchild.IsCancelled()) << "Cancellation must propagate through more than one level";

        auto result = grandchild.CheckCancellation();
        ASSERT_FALSE(result.has_value());
        EXPECT_TRUE(result.error() == TaskError::Cancelled);
    }

    TEST(TaskSystemTest, LinkedTokenCancelDoesNotAffectParent) {
        auto parent = CancellationToken::Create();
        auto child = parent.CreateLinked();
        auto sibling = parent.CreateLinked();

        child.Cancel();

        EXPECT_TRUE(child.IsCancelled());
        EXPECT_FALSE(parent.IsCancelled()) << "Cancelling a child must not cancel its parent";
        EXPECT_FALSE(sibling.IsCancelled()) << "Cancelling a child must not cancel its sibling";
    }

    TEST(TaskSystemTest, LinkedTokenOutlivesParent) {
        CancellationToken child;
        {
            auto parent = CancellationToken::Create();
            child = parent.CreateLinked();
            parent.Cancel();
        }

        EXPECT_TRUE(child.IsCancelled()) << "The child must keep seeing the cancellation after the parent token is gone";
    }

    TEST(TaskSystemTest, LinkedTokenFromInvalidParentIsIndependent) {
        CancellationToken empty;
        EXPECT_FALSE(empty.IsValid());

        auto child = empty.CreateLinked();
        EXPECT_TRUE(child.IsValid());
        EXPECT_FALSE(child.IsCancelled());

        child.Cancel();
        EXPECT_TRUE(child.IsCancelled());
    }

    TEST(TaskSystemTest, TaskGroupCancelAllCancelsLinkedTokens) {
        TaskGroup group;
        auto linked = group.GetToken().CreateLinked();
        EXPECT_FALSE(linked.IsCancelled());

        group.CancelAll();

        EXPECT_TRUE(linked.IsCancelled()) << "A token linked to the group token must be cancelled by CancelAll()";
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
