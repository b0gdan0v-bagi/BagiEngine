#pragma once

#include <TaskSystem/TaskPriority.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/ThreadPool.h>
#include <TaskSystem/TaskScheduler.h>

#include <thread>
#include <chrono>

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
        void Shutdown(PassKey<CoreManager>);

        // =================================================================
        // Public API - Run tasks
        // =================================================================

        /**
         * Запускает задачу на выполнение.
         * @param task Корутина для выполнения
         * @param priority Приоритет задачи
         * @param threadType Тип потока для выполнения
         * @return Handle для отслеживания задачи
         */
        template <typename T>
        IntrusivePtr<TaskHandle<T>> Run(Task<T> task,
                                         TaskPriority priority = TaskPriority::Normal,
                                         ThreadType threadType = ThreadType::Background) {
            auto handle = MakeIntrusive<TaskHandle<T>>(std::move(task));
            
            auto* handlePtr = handle.Get();
            _scheduler.Schedule([handlePtr]() {
                handlePtr->Start();  // Start() сам вызывает Resume()
            }, priority, threadType);

            return handle;
        }

        /**
         * Запускает отложенную задачу.
         */
        template <typename T>
        IntrusivePtr<TaskHandle<T>> RunDelayed(Task<T> task,
                                                Duration delay,
                                                TaskPriority priority = TaskPriority::Normal,
                                                ThreadType threadType = ThreadType::Background) {
            auto handle = MakeIntrusive<TaskHandle<T>>(std::move(task));
            
            auto* handlePtr = handle.Get();
            _scheduler.ScheduleDelayed([handlePtr]() {
                handlePtr->Start();  // Start() сам вызывает Resume()
            }, delay, priority, threadType);

            return handle;
        }

        /**
         * Запускает задачу в главном потоке.
         */
        template <typename T>
        IntrusivePtr<TaskHandle<T>> RunOnMainThread(Task<T> task,
                                                     TaskPriority priority = TaskPriority::Normal) {
            return Run(std::move(task), priority, ThreadType::MainThread);
        }

        // =================================================================
        // Public API - Utilities
        // =================================================================

        /**
         * Проверяет, инициализирована ли система задач.
         */
        [[nodiscard]] bool IsInitialized() const { return _isInitialized; }

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
         * Получает ThreadPool (для низкоуровневого доступа).
         */
        [[nodiscard]] ThreadPool* GetThreadPool() { return _threadPool.get(); }

        /**
         * Получает TaskScheduler (для низкоуровневого доступа).
         */
        [[nodiscard]] TaskScheduler* GetScheduler() { return &_scheduler; }

    private:
        bool _isInitialized = false;
        std::thread::id _mainThreadId;

        eastl::unique_ptr<ThreadPool> _threadPool;
        TaskScheduler _scheduler;
    };

} // namespace BECore
