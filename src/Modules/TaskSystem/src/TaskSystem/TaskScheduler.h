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

        bool _stopped = false;
    };

}  // namespace BECore
