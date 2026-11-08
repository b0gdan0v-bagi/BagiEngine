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
         * @param clock Часы задержки: реальное время (по умолчанию) или игровое время (AdvanceGameTime).
         *              Игровая задержка отсчитывается от игрового времени на момент вызова; для
         *              детерминированного запуска вызывать из главного потока. Неположительная
         *              задержка готова на ближайшем ProcessDelayedTasks.
         */
        void ScheduleDelayed(TaskQueue::TaskFunc task, Duration delay, TaskPriority priority = TaskPriority::Normal, ThreadType threadType = ThreadType::Background,
                             DelayClock clock = DelayClock::WallClock);

        /**
         * Продвигает игровые часы. Единственный способ сдвинуть DelayClock::GameTime.
         * NaN, бесконечность и отрицательный шаг игнорируются; слишком большой шаг насыщается.
         * Шаг один раз переводится в Duration, поэтому суммы точны и одинаковы на всех платформах.
         * @param seconds Шаг в секундах (в кадре - Time::GetDeltaTime())
         */
        void AdvanceGameTime(double seconds);

        /**
         * Возвращает игровое время планировщика (сумма принятых шагов AdvanceGameTime).
         */
        [[nodiscard]] Duration GetGameTime() const;

        /**
         * Обрабатывает задачи главного потока.
         * Должен вызываться из главного потока каждый кадр.
         */
        void ProcessMainThreadTasks();

        /**
         * Проверяет и запускает готовые отложенные задачи:
         * по реальному времени - по Clock::now(), по игровому - по GetGameTime().
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
         * Возвращает количество отложенных задач (обеих очередей).
         */
        [[nodiscard]] size_t GetDelayedTaskCount() const;

    private:
        struct DelayedTask {
            TaskQueue::TaskFunc task;
            TimePoint executeTime;  // для игровой очереди: TimePoint{} + игровое время
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

        // Отложенные задачи по реальному времени
        eastl::priority_queue<DelayedTask> _delayedTasks;
        // Отложенные задачи по игровому времени (_gameTime), под тем же мьютексом
        eastl::priority_queue<DelayedTask> _gameDelayedTasks;
        Duration _gameTime{};
        mutable std::mutex _delayedMutex;

        bool _stopped = false;
    };

}  // namespace BECore
