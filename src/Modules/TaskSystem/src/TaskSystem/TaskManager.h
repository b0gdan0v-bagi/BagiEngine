#pragma once

#include <TaskSystem/Task.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskLabel.h>
#include <TaskSystem/TaskPriority.h>
#include <TaskSystem/TaskScheduler.h>
#include <TaskSystem/ThreadPool.h>
#include <chrono>
#include <thread>

namespace BECore {

    // Forward declarations
    class CoreManager;

    /**
     * TaskManager - главный интерфейс для работы с асинхронными задачами.
     *
     * Предоставляет API для запуска задач, управления потоками выполнения
     * и интеграции с системой событий движка.
     *
     * @example
     * // Запуск простой задачи
     * auto handle = TaskManager::GetInstance().Run(MyAsyncTask());
     *
     * // Отложенная задача
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

        // Non-copyable, non-movable
        TaskManager(const TaskManager&) = delete;
        TaskManager& operator=(const TaskManager&) = delete;
        TaskManager(TaskManager&&) = delete;
        TaskManager& operator=(TaskManager&&) = delete;

        // =================================================================
        // Lifecycle (protected by PassKey<CoreManager>)
        // =================================================================

        void Initialize(PassKey<CoreManager>);
        void Update(PassKey<CoreManager>);

        /**
         * Advances the game clock of the delayed tasks (DelayClock::GameTime) by one frame delta.
         * Called by CoreManager::OnGameCycle right before Update; does nothing before Initialize and after Shutdown.
         */
        void AdvanceGameTime(PassKey<CoreManager>, double seconds);
        void Shutdown(PassKey<CoreManager>);

        // =================================================================
        // Public API - Run tasks
        // =================================================================

        /**
         * Запускает задачу на выполнение.
         * @param task Корутина для выполнения
         * @param priority Приоритет задачи
         * @param threadType Тип потока для выполнения
         * @param label BE_TASK_LABEL("name"): the zone that names the first slice of the task in a profiler trace (default: none).
         *              A later slice after `co_await SwitchToBackground()` is named by the label of that call
         * @return Handle для отслеживания задачи
         */
        template <typename T>
        IntrusivePtr<TaskHandle<T>> Run(Task<T> task, TaskPriority priority = TaskPriority::Normal, ThreadType threadType = ThreadType::Background, TaskLabel label = {}) {
            auto handle = MakeIntrusive<TaskHandle<T>>(std::move(task));

            _scheduler.Schedule(MakeStartTask(handle, label), priority, threadType);

            return handle;
        }

        /**
         * Запускает отложенную задачу.
         */
        template <typename T>
        IntrusivePtr<TaskHandle<T>> RunDelayed(Task<T> task, Duration delay, TaskPriority priority = TaskPriority::Normal, ThreadType threadType = ThreadType::Background, TaskLabel label = {}) {
            auto handle = MakeIntrusive<TaskHandle<T>>(std::move(task));

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
            auto handle = MakeIntrusive<TaskHandle<T>>(std::move(task));

            _scheduler.ScheduleDelayed(MakeStartTask(handle, label), delay, priority, threadType, DelayClock::GameTime);

            return handle;
        }

        /**
         * Запускает задачу в главном потоке.
         */
        template <typename T>
        IntrusivePtr<TaskHandle<T>> RunOnMainThread(Task<T> task, TaskPriority priority = TaskPriority::Normal, TaskLabel label = {}) {
            return Run(std::move(task), priority, ThreadType::MainThread, label);
        }

        // =================================================================
        // Public API - Utilities
        // =================================================================

        /**
         * Проверяет, инициализирована ли система задач.
         */
        [[nodiscard]] bool IsInitialized() const {
            return _isInitialized;
        }

        /**
         * Проверяет, выполняется ли код в главном потоке.
         */
        [[nodiscard]] bool IsMainThread() const;

        /**
         * Возвращает количество рабочих потоков в пуле.
         */
        [[nodiscard]] size_t GetWorkerCount() const;

        /**
         * Возвращает количество ожидающих задач.
         */
        [[nodiscard]] size_t GetPendingTaskCount() const;

        /**
         * Sends the queues of the task system to the profiler as counters, one graph each (a Chrome trace `C` event, a Tracy plot):
         * `Pool: pending jobs`, `Pool: steals` (since the last sample), `Main thread queue: pending`, `Delayed tasks: pending` and, in a
         * single-thread build, `Cooperative queue: pending`. The busy graphs (`Pool: busy workers`, `Worker n: busy`, 0 or 1) are not
         * sampled: the pool's workers send them on every change. Update samples once per frame, before it drains the queues; a test may
         * call it. Does nothing without a profiler backend or with BE_PROFILER=0.
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

        /**
         * Returns the time per frame of the background jobs (kDefaultFrameBudget unless SetFrameBudget was called).
         */
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

        /**
         * Получает ThreadPool (для низкоуровневого доступа).
         */
        [[nodiscard]] ThreadPool* GetThreadPool() {
            return _threadPool.get();
        }

        /**
         * Получает TaskScheduler (для низкоуровневого доступа).
         */
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
            return [handle, label]() mutable {
                auto owned = std::move(handle);
                RunLabeled(label, [&owned] { owned->Start(); });  // Start() сам вызывает Resume()
            };
        }

        bool _isInitialized = false;
        std::thread::id _mainThreadId;
        Duration _frameBudget = kDefaultFrameBudget;

        eastl::unique_ptr<ThreadPool> _threadPool;
        TaskScheduler _scheduler;
    };

}  // namespace BECore
