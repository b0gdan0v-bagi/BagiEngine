#pragma once

#include <TaskSystem/Task.h>
#include <TaskSystem/TaskPriority.h>

#include <atomic>
#include <functional>
#include <thread>

namespace BECore {

    /**
     * TaskHandleBase - базовый класс для TaskHandle без шаблонного параметра.
     * Используется для хранения задач в контейнерах.
     */
    class TaskHandleBase : public RefCountedAtomic {
    public:
        virtual ~TaskHandleBase() = default;

        /**
         * Проверяет, завершена ли задача.
         */
        [[nodiscard]] virtual bool IsDone() const = 0;

        /**
         * Отменяет задачу (если поддерживается).
         */
        virtual void Cancel() = 0;

        /**
         * Ожидает завершения задачи (блокирующе).
         */
        virtual void Wait() = 0;

        /**
         * Получает статус задачи.
         */
        [[nodiscard]] TaskStatus GetStatus() const { return _status.load(std::memory_order_acquire); }

        /**
         * Получает ошибку задачи.
         */
        [[nodiscard]] TaskError GetError() const { return _error.load(std::memory_order_acquire); }

        // Awaitable interface для co_await TaskHandleBase
        bool await_ready() const noexcept { return IsDone(); }
        void await_suspend(std::coroutine_handle<> continuation) noexcept { _continuation = continuation; }
        void await_resume() noexcept {}

    protected:
        std::atomic<TaskStatus> _status{TaskStatus::Pending};
        std::atomic<TaskError> _error{TaskError::None};
        std::coroutine_handle<> _continuation;

        void SetStatus(TaskStatus status) {
            _status.store(status, std::memory_order_release);
        }

        void ResumeContinuation() {
            if (_continuation) {
                _continuation.resume();
            }
        }
    };

    /**
     * TaskHandle<T> - handle для отслеживания и управления задачей.
     * 
     * Позволяет:
     * - Проверять статус задачи
     * - Ожидать завершения
     * - Получать результат
     * - Отменять выполнение
     * 
     * @tparam T Тип результата задачи
     * 
     * @example
     * TaskHandle<int> handle = TaskManager::Run(MyTask());
     * // ... делаем что-то еще ...
     * int result = handle.GetResult();
     */
    template <typename T = void>
    class TaskHandle : public TaskHandleBase {
    public:
        TaskHandle() = default;

        explicit TaskHandle(Task<T>&& task) : _task(std::move(task)) {
            SetStatus(TaskStatus::Pending);
        }

        ~TaskHandle() override = default;

        // =================================================================
        // TaskHandleBase interface
        // =================================================================

        [[nodiscard]] bool IsDone() const override {
            return _task.IsDone();
        }

        void Cancel() override {
            if (GetStatus() == TaskStatus::Pending || GetStatus() == TaskStatus::Running) {
                SetStatus(TaskStatus::Cancelled);
                _cancelled.store(true, std::memory_order_release);
            }
        }

        void Wait() override {
            while (!IsDone() && !_cancelled.load(std::memory_order_acquire)) {
                // Spin wait (TODO: replace with proper synchronization)
                std::this_thread::yield();
            }
        }

        // =================================================================
        // Result access
        // =================================================================

        /**
         * Получает результат задачи.
         * Блокирует до завершения, если задача еще выполняется.
         */
        std::expected<T, TaskError> GetResult() requires(!std::is_void_v<T>) {
            Wait();
            if (IsCancelled()) {
                return std::unexpected(TaskError::Cancelled);
            }
            return _task.GetResult();
        }

        std::expected<void, TaskError> GetResult() requires(std::is_void_v<T>) {
            Wait();
            if (IsCancelled()) {
                return std::unexpected(TaskError::Cancelled);
            }
            return _task.GetResult();
        }

        /**
         * Проверяет, была ли задача отменена.
         */
        [[nodiscard]] bool IsCancelled() const {
            return _cancelled.load(std::memory_order_acquire);
        }

        // =================================================================
        // Task access (для внутреннего использования)
        // =================================================================

        Task<T>& GetTask() { return _task; }
        const Task<T>& GetTask() const { return _task; }

        /**
         * Запускает выполнение задачи.
         */
        void Start() {
            if (GetStatus() == TaskStatus::Pending) {
                SetStatus(TaskStatus::Running);
                _task.Resume();
            }
        }

        /**
         * Отмечает задачу как завершенную.
         */
        void MarkCompleted() {
            if (!_cancelled.load(std::memory_order_acquire)) {
                SetStatus(TaskStatus::Completed);
            }
            ResumeContinuation();
        }

        /**
         * Отмечает задачу как завершенную с ошибкой.
         */
        void MarkFailed(TaskError error = TaskError::Exception) {
            _error.store(error, std::memory_order_release);
            SetStatus(TaskStatus::Failed);
            ResumeContinuation();
        }

    private:
        Task<T> _task;
        std::atomic<bool> _cancelled{false};
    };


} // namespace BECore
