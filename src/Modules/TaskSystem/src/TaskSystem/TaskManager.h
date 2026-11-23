#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Threading/Thread.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <BECore/Utils/PassKey.h>
#include <BECore/Utils/Singleton.h>
#include <TaskSystem/ParallelFor.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskLabel.h>
#include <TaskSystem/TaskPriority.h>
#include <TaskSystem/TaskScheduler.h>
#include <TaskSystem/ThreadPool.h>
#include <chrono>
#include <cstddef>
#include <utility>

namespace BECore {

    class CoreManager;

    /**
     * Entry point of the task system: starts tasks on a thread type and priority. Thread-safe; the lifecycle
     * calls are CoreManager's (PassKey).
     *
     * @example
     * auto handle = TaskManager::GetInstance().Run(MyAsyncTask());
     * TaskManager::GetInstance().RunDelayed(MyTask(), std::chrono::seconds(1));
     */
    class TaskManager : public SingletonAtomic<TaskManager> {
    public:
        using Duration = std::chrono::steady_clock::duration;

        /**
         * The most worker threads Initialize creates: min(hardware_concurrency, kMaxWorkerThreads), at least 2. The web build creates its
         * Web Workers before main() (BE_WEB_THREAD_POOL_SIZE in cmake/BagiWeb.cmake, one more than this), so raising this number means
         * raising that one: TaskManager.cpp checks it at compile time.
         */
        static constexpr size_t kMaxWorkerThreads = 4;

        /// Default time per frame that Update gives to the continuations of the background jobs (single-thread build).
        static constexpr Duration kDefaultFrameBudget = std::chrono::milliseconds(4);

        TaskManager() = default;
        ~TaskManager() = default;

        TaskManager(const TaskManager&) = delete;
        TaskManager& operator=(const TaskManager&) = delete;
        TaskManager(TaskManager&&) = delete;
        TaskManager& operator=(TaskManager&&) = delete;

        void Initialize(PassKey<CoreManager>);
        void Update(PassKey<CoreManager>);

        /**
         * Advances the game clock of the delayed tasks (DelayClock::GameTime) by one frame delta.
         * Called by CoreManager::OnGameCycle right before Update; does nothing before Initialize and after Shutdown.
         */
        void AdvanceGameTime(PassKey<CoreManager>, double seconds);
        void Shutdown(PassKey<CoreManager>);

        /**
         * Schedules the task. The scheduled job owns a reference to the handle, so dropping the returned one is safe.
         * @param label BE_TASK_LABEL("name"): names the first slice of the task in a profiler trace (default: none).
         *              A later slice after `co_await SwitchToBackground()` is named by the label of that call
         */
        template <typename T>
        IntrusivePtr<TaskHandle<T>> Run(Task<T> task, TaskPriority priority = TaskPriority::Normal, ThreadType threadType = ThreadType::Background, TaskLabel label = {}) {
            IntrusivePtr<TaskHandle<T>> handle = New<TaskHandle<T>>(std::move(task));

            _scheduler.Schedule(MakeStartTask(handle, label), priority, threadType);

            return handle;
        }

        /** Like Run, started after a wall-clock @p delay. */
        template <typename T>
        IntrusivePtr<TaskHandle<T>> RunDelayed(Task<T> task, Duration delay, TaskPriority priority = TaskPriority::Normal, ThreadType threadType = ThreadType::Background, TaskLabel label = {}) {
            IntrusivePtr<TaskHandle<T>> handle = New<TaskHandle<T>>(std::move(task));

            _scheduler.ScheduleDelayed(MakeStartTask(handle, label), delay, priority, threadType);

            return handle;
        }

        /**
         * Starts a delayed task on game time: the delay counts the frame deltas fed by CoreManager::OnGameCycle
         * (the frame clamp and the manual clock apply). Released in the Update of the frame in which the game time
         * reaches the delay; by default it runs on the main thread in that same frame. Call it from the main thread
         * when the run must be deterministic.
         */
        template <typename T>
        IntrusivePtr<TaskHandle<T>> RunDelayedGameTime(Task<T> task, Duration delay, TaskPriority priority = TaskPriority::Normal, ThreadType threadType = ThreadType::MainThread,
                                                       TaskLabel label = {}) {
            IntrusivePtr<TaskHandle<T>> handle = New<TaskHandle<T>>(std::move(task));

            _scheduler.ScheduleDelayed(MakeStartTask(handle, label), delay, priority, threadType, DelayClock::GameTime);

            return handle;
        }

        /** Starts the task in the main-thread queue, drained in Update. */
        template <typename T>
        IntrusivePtr<TaskHandle<T>> RunOnMainThread(Task<T> task, TaskPriority priority = TaskPriority::Normal, TaskLabel label = {}) {
            return Run(std::move(task), priority, ThreadType::MainThread, label);
        }

        [[nodiscard]] bool IsInitialized() const {
            return _isInitialized;
        }

        [[nodiscard]] bool IsMainThread() const;

        [[nodiscard]] size_t GetWorkerCount() const;

        [[nodiscard]] size_t GetPendingTaskCount() const;

        /**
         * Runs `body(begin, end)` over chunks of `[0, count)` on the pool's workers and on the calling thread, and returns when all are done
         * (the free `ParallelFor` of `TaskSystem/ParallelFor.h` over this manager's pool: its contract, the body's rules and `minChunk` are there).
         * Before Initialize, after Shutdown, in a single-thread build (the inline pool has no worker) and below `2 * minChunk` indices it calls
         * `body(0, count)` once on the calling thread, so the result is the same everywhere. Allocates nothing in a steady call.
         */
        void ParallelFor(size_t count, size_t minChunk, FunctionRef<void(size_t begin, size_t end)> body);

        /**
         * Sends the sizes of the queues of the task system to the profiler as counters. Update samples once per
         * frame, before it drains the queues; a test may call it. Does nothing without a profiler backend. The pool's
         * busy graphs are sent by the workers themselves on every change (Profiler/CLAUDE.md).
         */
        void SampleProfilerCounters();

        /**
         * Sets the time per frame that Update gives to the background jobs that yielded (single-thread build: the
         * inline pool; a threaded build never reads it). A negative value is stored as zero, which still resumes one
         * slice per frame. A setting: Shutdown keeps it.
         */
        void SetFrameBudget(Duration budget) {
            _frameBudget = budget < Duration::zero() ? Duration::zero() : budget;
        }

        [[nodiscard]] Duration GetFrameBudget() const {
            return _frameBudget;
        }

        /**
         * True when the running background job should suspend (single-thread build: its slice of the frame is spent).
         * Always false on a threaded build, in a main-thread job and outside any job. For a loop that checks by hand:
         * if (TaskManager::GetInstance().ShouldYield()) { co_await Yield(); }  (co_await YieldIfNeeded() does the same.)
         */
        [[nodiscard]] bool ShouldYield() const {
            return _scheduler.ShouldYield();
        }

        [[nodiscard]] ThreadPool* GetThreadPool() {
            return _threadPool.get();
        }

        [[nodiscard]] TaskScheduler* GetScheduler() {
            return &_scheduler;
        }

    private:
        /**
         * Builds the scheduler job that starts @p handle. The job owns a reference, so the task survives even if
         * the caller drops the returned handle before the job runs (Start() then keeps it alive until completion).
         * The reference is moved into a local so a pool worker does not hold on to it after the job has run.
         */
        template <typename T>
        static TaskQueue::TaskFunc MakeStartTask(IntrusivePtr<TaskHandle<T>> handle, TaskLabel label) {
            return [handle = std::move(handle), label]() mutable {
                auto owned = std::move(handle);
                RunLabeled(label, [&owned] { owned->Start(); });
            };
        }

        bool _isInitialized = false;
        Thread::Id _mainThreadId;
        Duration _frameBudget = kDefaultFrameBudget;

        UniquePtr<ThreadPool> _threadPool;
        TaskScheduler _scheduler;
    };

}  // namespace BECore
