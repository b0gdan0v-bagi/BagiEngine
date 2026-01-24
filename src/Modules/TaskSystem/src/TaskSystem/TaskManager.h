#pragma once

#include <TaskSystem/TaskPriority.h>

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
     * Использует SingletonAtomic для потокобезопасной инициализации.
     * Lifecycle методы (Initialize, Update, Shutdown) защищены PassKey<CoreManager>.
     * 
     * @example
     * // Запуск простой задачи
     * TaskManager::GetInstance().Run(MyAsyncTask());
     * 
     * // Переключение на главный поток
     * co_await TaskManager::SwitchToMainThread();
     */
    class TaskManager : public SingletonAtomic<TaskManager> {
    public:
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

        /**
         * Инициализация системы задач.
         * Создает пул потоков и подготавливает очереди.
         * Вызывается из CoreManager::OnApplicationInit.
         */
        void Initialize(PassKey<CoreManager>);

        /**
         * Обновление системы задач.
         * Обрабатывает задачи из очереди главного потока.
         * Вызывается из CoreManager::OnGameCycle.
         */
        void Update(PassKey<CoreManager>);

        /**
         * Завершение работы системы задач.
         * Ожидает завершения всех задач и останавливает потоки.
         * Вызывается из CoreManager::OnApplicationDeinit.
         */
        void Shutdown(PassKey<CoreManager>);

        // =================================================================
        // Public API (будет расширено в следующих этапах)
        // =================================================================

        /**
         * Проверяет, инициализирована ли система задач.
         * @return true если система готова к работе
         */
        [[nodiscard]] bool IsInitialized() const { return _isInitialized; }

        /**
         * Проверяет, выполняется ли код в главном потоке.
         * @return true если текущий поток - главный
         */
        [[nodiscard]] bool IsMainThread() const;

        /**
         * Возвращает количество рабочих потоков в пуле.
         * @return количество потоков
         */
        [[nodiscard]] size_t GetWorkerCount() const { return _workerCount; }

    private:
        bool _isInitialized = false;
        size_t _workerCount = 0;
        std::thread::id _mainThreadId;
    };

} // namespace BECore
