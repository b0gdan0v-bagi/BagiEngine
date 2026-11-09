#pragma once

#include <EASTL/priority_queue.h>
#include <TaskSystem/TaskPriority.h>
#include <TaskSystem/TaskQueue.h>
#include <TaskSystem/ThreadPool.h>
#include <chrono>

namespace BECore {

    /**
     * TaskScheduler - планировщик задач с приоритетами и отложенным выполнением.
     *
     * Поддерживает:
     * - Приоритетные очереди
     * - Отложенные задачи (delayed tasks)
     * - Задачи для главного потока
     * - Интеграцию с ThreadPool
     * - Cooperative frame budget of a single-thread build (inline ThreadPool): a background coroutine that
     *   yields is parked in a queue of its own and resumed by ProcessBackgroundSlices for a set time per frame
     */
    class TaskScheduler {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        using Duration = Clock::duration;

        TaskScheduler() = default;
        ~TaskScheduler() = default;

        // Non-copyable, non-movable
        TaskScheduler(const TaskScheduler&) = delete;
        TaskScheduler& operator=(const TaskScheduler&) = delete;

        /**
         * Инициализирует планировщик.
         * @param threadPool Пул потоков для выполнения фоновых задач
         */
        void Initialize(ThreadPool* threadPool);

        /**
         * Добавляет задачу в очередь.
         * @param task Функция для выполнения
         * @param priority Приоритет задачи
         * @param threadType Тип потока для выполнения
         */
        void Schedule(TaskQueue::TaskFunc task, TaskPriority priority = TaskPriority::Normal, ThreadType threadType = ThreadType::Background);

        /**
         * Добавляет отложенную задачу.
         * @param task Функция для выполнения
         * @param delay Задержка перед выполнением
         * @param priority Приоритет задачи
         * @param threadType Тип потока для выполнения
         * @param clock Clock of the delay: real time (default) or game time (AdvanceGameTime).
         *              A game delay counts from the game time at the moment of the call; call it from the
         *              main thread for a deterministic release. A non-positive delay is ready at the
         *              next ProcessDelayedTasks.
         */
        void ScheduleDelayed(TaskQueue::TaskFunc task, Duration delay, TaskPriority priority = TaskPriority::Normal, ThreadType threadType = ThreadType::Background,
                             DelayClock clock = DelayClock::WallClock);

        /**
         * Advances the game clock. The only way to move DelayClock::GameTime.
         * NaN, infinity and a negative step are ignored; a step that is too large saturates.
         * The step is converted to Duration once (rounded to the nearest nanosecond), so the sums are
         * exact integers and equal on every platform.
         * @param seconds Step in seconds (in a frame: Time::GetDeltaTime())
         */
        void AdvanceGameTime(double seconds);

        /**
         * Returns the scheduler's game time (the sum of the accepted AdvanceGameTime steps).
         */
        [[nodiscard]] Duration GetGameTime() const;

        /**
         * Обрабатывает задачи главного потока.
         * Должен вызываться из главного потока каждый кадр.
         */
        void ProcessMainThreadTasks();

        /**
         * Checks and starts the ready delayed tasks:
         * real-time ones by Clock::now(), game-time ones by GetGameTime().
         * Должен вызываться регулярно (например, в Update).
         */
        void ProcessDelayedTasks();

        /**
         * Останавливает планировщик.
         */
        void Shutdown();

        /**
         * Возвращает количество задач в очереди главного потока.
         */
        [[nodiscard]] size_t GetMainThreadQueueSize() const;

        /**
         * Returns the number of delayed tasks (both queues).
         */
        [[nodiscard]] size_t GetDelayedTaskCount() const;

        /**
         * Schedules the continuation of a coroutine that yielded (YieldAwaiter, YieldIfNeededAwaiter).
         * With an inline pool, called from a background job: the continuation joins the cooperative queue and
         * is resumed by ProcessBackgroundSlices. Otherwise exactly Schedule(task, TaskPriority::Low, threadType),
         * so a main-thread yield resumes in the next main-thread drain and a yield on a worker pool goes back
         * to the pool.
         */
        void ScheduleYielded(TaskQueue::TaskFunc task, ThreadType threadType);

        /**
         * Resumes the parked continuations of background jobs (FIFO) until the queue is empty or the budget is
         * spent. A budget of zero or less still resumes one slice, so a parked task is never starved. A slice
         * that yields again is resumed again in the same call while time remains (round robin). The call
         * overruns the budget by at most one slice: a coroutine cannot be preempted, only the time between
         * two of its own checks counts. While it runs, ShouldYield() compares the budget clock with the
         * deadline of this call; after it the window counts as spent until the next call.
         * A no-op (returns 0) unless the pool is inline and the scheduler is not stopped. Call it from the
         * single thread, once per frame, after the main-thread tasks (TaskManager::Update does).
         * @param budget Time for this call, by the budget clock
         * @return Number of slices run
         */
        size_t ProcessBackgroundSlices(Duration budget);

        /**
         * True when the running background job should suspend (co_await YieldIfNeeded()): the pool is inline,
         * the caller is a background job and the budget window is spent. False on a worker pool (only the stop flag and the kind of the
         * pool are read, so it is safe from any thread), in a main-thread job, outside any job, before the
         * first frame (the window is open until the first ProcessBackgroundSlices), and once the scheduler is stopped
         * (its pool may be gone).
         */
        [[nodiscard]] bool ShouldYield() const;

        /**
         * Replaces the clock of the budget (ProcessBackgroundSlices and ShouldYield only; delayed tasks keep
         * Clock::now()). nullptr restores steady_clock::now. For tests: a fake clock makes them independent
         * of the wall time.
         */
        void SetBudgetClock(TimePoint (*now)());

        /**
         * Returns the number of parked continuations in the cooperative queue.
         */
        [[nodiscard]] size_t GetBackgroundSliceCount() const;

    private:
        struct DelayedTask {
            TaskQueue::TaskFunc task;
            TimePoint executeTime;  // game queue: TimePoint{} + game time
            TaskPriority priority;
            ThreadType threadType;

            bool operator<(const DelayedTask& other) const {
                // Min-heap: меньшее время = выше приоритет
                return executeTime > other.executeTime;
            }
        };

        struct PrioritizedTask {
            TaskQueue::TaskFunc task;
            TaskPriority priority;

            bool operator<(const PrioritizedTask& other) const {
                // Max-heap: больший приоритет = выше
                return static_cast<uint8_t>(priority) < static_cast<uint8_t>(other.priority);
            }
        };

        void DispatchToThreadPool(TaskQueue::TaskFunc task, TaskPriority priority);

        /// The clock of the budget: the one set by SetBudgetClock, else steady_clock::now.
        [[nodiscard]] TimePoint BudgetNow() const;

        /// Submits @p task to the inline pool wrapped so that it counts as a background job while it runs.
        void SubmitInlineBackgroundJob(TaskQueue::TaskFunc task);

        ThreadPool* _threadPool = nullptr;

        // Очередь главного потока (с приоритетами)
        eastl::priority_queue<PrioritizedTask> _mainThreadQueue;
        mutable std::mutex _mainThreadMutex;

        // Delayed tasks by real time
        eastl::priority_queue<DelayedTask> _delayedTasks;
        // Delayed tasks by game time (_gameTime), under the same mutex
        eastl::priority_queue<DelayedTask> _gameDelayedTasks;
        Duration _gameTime{};
        mutable std::mutex _delayedMutex;

        // Cooperative queue (inline pool only): continuations of background jobs that yielded
        eastl::deque<TaskQueue::TaskFunc> _backgroundSlices;
        mutable std::mutex _backgroundMutex;

        // Inline pool only, one thread: true while a background job runs on the stack
        bool _inBackgroundJob = false;
        // TimePoint max: no frame yet (never yield); now + budget: a drain runs; TimePoint min: spent
        TimePoint _sliceDeadline = (TimePoint::max)();
        TimePoint (*_budgetNow)() = nullptr;  // nullptr: steady_clock::now

        // Written by Initialize / Shutdown on the main thread, read by any thread (Schedule, ShouldYield)
        std::atomic<bool> _stopped{false};
    };

}  // namespace BECore
