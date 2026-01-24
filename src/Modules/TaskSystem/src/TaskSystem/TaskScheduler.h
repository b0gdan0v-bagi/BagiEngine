#pragma once

#include <TaskSystem/TaskQueue.h>
#include <TaskSystem/TaskPriority.h>
#include <TaskSystem/ThreadPool.h>

#include <chrono>
#include <mutex>

#include <EASTL/vector.h>
#include <EASTL/priority_queue.h>

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
        void Schedule(TaskQueue::TaskFunc task, 
                      TaskPriority priority = TaskPriority::Normal,
                      ThreadType threadType = ThreadType::Background);

        /**
         * Добавляет отложенную задачу.
         * @param task Функция для выполнения
         * @param delay Задержка перед выполнением
         * @param priority Приоритет задачи
         * @param threadType Тип потока для выполнения
         */
        void ScheduleDelayed(TaskQueue::TaskFunc task,
                             Duration delay,
                             TaskPriority priority = TaskPriority::Normal,
                             ThreadType threadType = ThreadType::Background);

        /**
         * Обрабатывает задачи главного потока.
         * Должен вызываться из главного потока каждый кадр.
         */
        void ProcessMainThreadTasks();

        /**
         * Проверяет и запускает готовые отложенные задачи.
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
         * Возвращает количество отложенных задач.
         */
        [[nodiscard]] size_t GetDelayedTaskCount() const;

    private:
        struct DelayedTask {
            TaskQueue::TaskFunc task;
            TimePoint executeTime;
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

        // Отложенные задачи
        eastl::priority_queue<DelayedTask> _delayedTasks;
        mutable std::mutex _delayedMutex;

        bool _stopped = false;
    };

} // namespace BECore
