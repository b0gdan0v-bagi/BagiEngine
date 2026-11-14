#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <TaskSystem/Awaitables.h>
#include <TaskSystem/CancellationToken.h>
#include <TaskSystem/LiveTasks.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskGroup.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/TaskQueue.h>
#include <TaskSystem/TaskStatistics.h>
#include <TaskSystem/ThreadPool.h>
#include <chrono>
#include <thread>

namespace BECore::Tests {

    // A handle is not awaitable: nothing would resume a coroutine suspended on it. Wait() blocks, a Task<T> is awaited directly.
    template <typename THandle>
    concept AwaitableHandle = requires(THandle & handle) {
        handle.await_ready();
    };

    static_assert(!AwaitableHandle<TaskHandleBase>);
    static_assert(!AwaitableHandle<TaskHandle<int>>);
    static_assert(AwaitableHandle<Task<int>>);  // guards the concept itself: a Task is awaited directly

    TEST(TaskSystemTest, CompileTimeChecks) {}

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
        /// either flaky on a loaded machine or needlessly slow. The limit only costs time when a test fails, so it is generous
        /// (ctest -j runs many test processes on the cores of one machine).
        template <typename Predicate>
        bool WaitUntil(Predicate&& predicate, std::chrono::milliseconds timeout = std::chrono::milliseconds(20000)) {
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

            SharedPtr<std::atomic<bool>> returned = MakeShared<std::atomic<bool>>(false);
            std::thread thread;
        };

        constexpr auto kLetWaiterBlock = std::chrono::milliseconds(30);

        /// State shared between a test and a coroutine; the test owns it and outlives every task that uses it.
        struct LifetimeProbe {
            std::atomic<bool> gate{false};
            std::atomic<bool> parked{false};
            std::atomic<bool> finished{false};
            std::atomic<bool> frameDestroyed{false};
            std::atomic<bool> destroyedBeforeFinish{false};
        };

        /// Passed to a coroutine by value, so it lives in the coroutine frame and is destroyed with it.
        /// Records whether the frame went away before the task body completed.
        class FrameGuard {
        public:
            explicit FrameGuard(LifetimeProbe& probe) : _probe(&probe) {}
            FrameGuard(FrameGuard&& other) noexcept : _probe(std::exchange(other._probe, nullptr)) {}

            FrameGuard(const FrameGuard&) = delete;
            FrameGuard& operator=(const FrameGuard&) = delete;
            FrameGuard& operator=(FrameGuard&&) = delete;

            ~FrameGuard() {
                if (_probe != nullptr) {
                    _probe->destroyedBeforeFinish.store(!_probe->finished.load(std::memory_order_acquire), std::memory_order_release);
                    _probe->frameDestroyed.store(true, std::memory_order_release);
                }
            }

        private:
            LifetimeProbe* _probe;
        };

        /// Waits for probe.gate on whatever thread runs it, optionally after hopping to the pool first.
        Task<void> ProbeTask([[maybe_unused]] FrameGuard guard, LifetimeProbe& probe, bool hopToBackground) {
            if (hopToBackground) {
                co_await SwitchToBackground();
            }
            while (!probe.gate.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            probe.finished.store(true, std::memory_order_release);
            co_return;
        }

        /// Parks on a delay that never expires within a test run, standing in for a task that Shutdown strands.
        /// The delayed job still holds the (now dangling) coroutine_handle until the engine environment shuts down,
        /// where it is dropped unrun; nothing in the test process pumps TaskManager::Update.
        Task<void> ParkedTask([[maybe_unused]] FrameGuard guard, LifetimeProbe& probe) {
            probe.parked.store(true, std::memory_order_release);
            co_await Delay(std::chrono::hours(1));
            probe.finished.store(true, std::memory_order_release);
            co_return;
        }

    }  // namespace

    /// Tests that go through the real TaskManager (and so need the engine core services).
    class TaskManagerLifetimeTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

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

    TEST(TaskSystemTest, StatusIsCompletedWithoutCallingWait) {
        auto handle = MakeIntrusive<TaskHandle<int>>(SimpleIntTask(21));
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Pending);

        handle->Start();  // finishes inline

        EXPECT_TRUE(handle->IsDone());
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Completed) << "A finished task reported a stale status";
    }

    TEST(TaskSystemTest, StatusFollowsATaskRunningOnThePool) {
        std::atomic<bool> gate{false};
        std::atomic<bool> finished{false};
        IntrusivePtr<TaskHandle<void>> handle;
        ThreadPool pool(1);
        handle = StartOnPool(pool, GatedTask(gate, finished));

        EXPECT_TRUE(WaitUntil([&handle]() { return handle->GetStatus() == TaskStatus::Running; })) << "The task never reported Running";
        EXPECT_FALSE(handle->IsDone());

        gate.store(true, std::memory_order_release);
        EXPECT_TRUE(WaitUntil([&handle]() { return handle->IsDone(); })) << "The task never finished";
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Completed) << "A finished task reported a stale status";
    }

    TEST(TaskSystemTest, CancelAfterCompletionIsIgnored) {
        auto handle = MakeIntrusive<TaskHandle<int>>(SimpleIntTask(21));
        handle->Start();
        ASSERT_TRUE(handle->IsDone());

        handle->Cancel();

        EXPECT_FALSE(handle->IsCancelled()) << "Cancelling a finished task must not mark it cancelled";
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Completed);
        auto result = handle->GetResult();
        ASSERT_TRUE(result.has_value()) << "The result of a finished task was discarded by a late Cancel()";
        EXPECT_EQ(*result, 42);
    }

    TEST(TaskSystemTest, ConcurrentStartAndCancelKeepStatusConsistent) {
        // Start() and Cancel() race on a Pending handle. Whoever wins, the status and the cancelled flag must agree:
        // a Cancel() that lands between Start()'s check and its store must not be overwritten by Running.
        constexpr int kRounds = 20000;
        int inconsistent = 0;

        for (int round = 0; round < kRounds; ++round) {
            auto handle = MakeIntrusive<TaskHandle<void>>(SimpleVoidTask());
            std::atomic<int> arrived{0};
            auto rendezvous = [&arrived] {
                arrived.fetch_add(1);
                while (arrived.load() < 2) {
                    std::this_thread::yield();
                }
            };

            std::thread canceller([&] {
                rendezvous();
                handle->Cancel();
            });
            rendezvous();
            handle->Start();
            canceller.join();

            if (handle->IsCancelled() != (handle->GetStatus() == TaskStatus::Cancelled)) {
                ++inconsistent;
            }
        }

        EXPECT_EQ(inconsistent, 0) << "Start() overwrote a concurrent Cancel(): cancelled flag and status disagree";
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

    TEST_F(TaskManagerLifetimeTest, DroppedHandleKeepsTaskAliveUntilItFinishes) {
#if BE_SINGLE_THREAD
        GTEST_SKIP() << "needs worker threads (BE_SINGLE_THREAD)";
#endif
        LifetimeProbe probe;

        CoreManager::GetTaskManager().Run(ProbeTask(FrameGuard(probe), probe, false));  // handle dropped on the spot

        std::this_thread::sleep_for(kLetWaiterBlock);  // the body is now running on a worker, held at the gate
        EXPECT_FALSE(probe.frameDestroyed.load()) << "The coroutine frame was destroyed while the task was still running";

        probe.gate.store(true, std::memory_order_release);
        EXPECT_TRUE(WaitUntil([&probe]() { return probe.finished.load(); })) << "The task never ran to completion";
        EXPECT_TRUE(WaitUntil([&probe]() { return probe.frameDestroyed.load(); })) << "The frame leaked after the task finished";
        EXPECT_FALSE(probe.destroyedBeforeFinish.load()) << "The frame was destroyed before the task body completed";
    }

    TEST_F(TaskManagerLifetimeTest, DroppedHandleKeepsTaskAliveAcrossThreadHop) {
#if BE_SINGLE_THREAD
        GTEST_SKIP() << "needs worker threads (BE_SINGLE_THREAD)";
#endif
        LifetimeProbe probe;

        // Parks on SwitchToBackground first: the awaiter only keeps a coroutine_handle, nothing else owns the frame.
        CoreManager::GetTaskManager().Run(ProbeTask(FrameGuard(probe), probe, true));

        std::this_thread::sleep_for(kLetWaiterBlock);
        EXPECT_FALSE(probe.frameDestroyed.load()) << "The coroutine frame was destroyed while the task was suspended or running";

        probe.gate.store(true, std::memory_order_release);
        EXPECT_TRUE(WaitUntil([&probe]() { return probe.finished.load(); })) << "The task never ran to completion";
        EXPECT_TRUE(WaitUntil([&probe]() { return probe.frameDestroyed.load(); })) << "The frame leaked after the task finished";
        EXPECT_FALSE(probe.destroyedBeforeFinish.load());
    }

    TEST_F(TaskManagerLifetimeTest, KeptHandleOwnsTaskAfterItFinishes) {
#if BE_SINGLE_THREAD
        GTEST_SKIP() << "needs worker threads (BE_SINGLE_THREAD)";
#endif
        LifetimeProbe probe;

        auto handle = CoreManager::GetTaskManager().Run(ProbeTask(FrameGuard(probe), probe, false));
        probe.gate.store(true, std::memory_order_release);
        handle->Wait();

        EXPECT_TRUE(probe.finished.load());
        EXPECT_FALSE(probe.frameDestroyed.load()) << "The frame must live as long as the caller holds the handle";

        handle.Reset();
        EXPECT_TRUE(WaitUntil([&probe]() { return probe.frameDestroyed.load(); })) << "The frame was not released after the last reference went away";
        EXPECT_FALSE(probe.destroyedBeforeFinish.load());
    }

    TEST_F(TaskManagerLifetimeTest, TaskCancelledBeforeStartIsReleasedWithItsHandle) {
        LifetimeProbe probe;
        auto handle = MakeIntrusive<TaskHandle<void>>(ProbeTask(FrameGuard(probe), probe, false));

        handle->Cancel();
        handle->Start();  // a cancelled handle is no longer Pending, so it must not keep itself alive
        handle.Reset();

        EXPECT_TRUE(probe.frameDestroyed.load()) << "A task that never started must not outlive its last handle";
        EXPECT_TRUE(probe.destroyedBeforeFinish.load());
    }

    TEST_F(TaskManagerLifetimeTest, AbandonCancelsParkedTaskAndWakesWaiter) {
        LifetimeProbe probe;
        auto handle = CoreManager::GetTaskManager().Run(ParkedTask(FrameGuard(probe), probe));
        ASSERT_TRUE(WaitUntil([&probe]() { return probe.parked.load(); })) << "The task never reached its delay";

        AsyncWaiter waiter(handle);
        std::this_thread::sleep_for(kLetWaiterBlock);
        ASSERT_FALSE(waiter.HasReturned()) << "Wait() returned for a task that is still parked";

        Detail::LiveTasks::GetInstance().AbandonAll();

        ASSERT_TRUE(WaitUntil([&waiter]() { return waiter.HasReturned(); })) << "Wait() on an abandoned task never returned";
        EXPECT_TRUE(handle->IsCancelled());
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Cancelled);
        auto result = handle->GetResult();
        ASSERT_FALSE(result.has_value());
        EXPECT_TRUE(result.error() == TaskError::Cancelled);
        EXPECT_FALSE(probe.finished.load());
    }

    TEST_F(TaskManagerLifetimeTest, AbandonFreesParkedTaskWhoseHandleWasDropped) {
        LifetimeProbe probe;
        CoreManager::GetTaskManager().Run(ParkedTask(FrameGuard(probe), probe));  // fire and forget
        ASSERT_TRUE(WaitUntil([&probe]() { return probe.parked.load(); })) << "The task never reached its delay";
        EXPECT_FALSE(probe.frameDestroyed.load()) << "The frame must live while the task is parked";

        Detail::LiveTasks::GetInstance().AbandonAll();

        EXPECT_TRUE(probe.frameDestroyed.load()) << "The self-reference of a stranded task leaked its frame";
        EXPECT_TRUE(probe.destroyedBeforeFinish.load());
    }

    TEST_F(TaskManagerLifetimeTest, AbandonKeepsFrameWhileCallerHoldsTheHandle) {
        LifetimeProbe probe;
        auto handle = CoreManager::GetTaskManager().Run(ParkedTask(FrameGuard(probe), probe));
        ASSERT_TRUE(WaitUntil([&probe]() { return probe.parked.load(); })) << "The task never reached its delay";

        Detail::LiveTasks::GetInstance().AbandonAll();
        EXPECT_FALSE(probe.frameDestroyed.load()) << "The frame is owned by the caller's handle and must outlive Abandon";

        handle.Reset();
        EXPECT_TRUE(probe.frameDestroyed.load()) << "The frame was not released with the last handle";
    }

    TEST_F(TaskManagerLifetimeTest, AbandonCancelsTaskThatWasNeverStarted) {
        std::atomic<bool> ran{false};
        auto handle = MakeIntrusive<TaskHandle<void>>(FlagTask(ran));  // Pending: its Start() job was lost with a queue
        AsyncWaiter waiter(handle);
        std::this_thread::sleep_for(kLetWaiterBlock);
        ASSERT_FALSE(waiter.HasReturned());

        Detail::LiveTasks::GetInstance().AbandonAll();

        ASSERT_TRUE(WaitUntil([&waiter]() { return waiter.HasReturned(); })) << "Wait() on a task that can never start never returned";
        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Cancelled);

        handle->Start();  // a late Start() must not revive it
        EXPECT_FALSE(ran.load());
    }

    TEST_F(TaskManagerLifetimeTest, AbandonLeavesFinishedTaskAlone) {
        auto handle = CoreManager::GetTaskManager().Run(SimpleIntTask(21));
        handle->Wait();

        Detail::LiveTasks::GetInstance().AbandonAll();

        EXPECT_TRUE(handle->GetStatus() == TaskStatus::Completed);
        EXPECT_FALSE(handle->IsCancelled());
        auto result = handle->GetResult();
        ASSERT_TRUE(result.has_value()) << "A finished task lost its result to Abandon";
        EXPECT_EQ(*result, 42);
    }

    TEST_F(TaskManagerLifetimeTest, HandlesComingAndGoingWhileAbandonAllRuns) {
        // Registration and AbandonAll share one lock: a handle that is being destroyed must never be seen half gone.
        constexpr int kThreads = 4;
        constexpr int kHandlesPerThread = 5000;
        std::atomic<int> running{kThreads};

        Vector<std::thread> threads;
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&running] {
                for (int n = 0; n < kHandlesPerThread; ++n) {
                    auto handle = MakeIntrusive<TaskHandle<void>>(SimpleVoidTask());  // never started: stays Pending
                }
                running.fetch_sub(1);
            });
        }
        while (running.load() > 0) {
            Detail::LiveTasks::GetInstance().AbandonAll();
        }
        for (auto& thread : threads) {
            thread.join();
        }
        SUCCEED();
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
