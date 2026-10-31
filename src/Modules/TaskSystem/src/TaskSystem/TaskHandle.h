#pragma once

#include <TaskSystem/Task.h>
#include <TaskSystem/TaskPriority.h>

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
         * The coroutine finishes on its own thread without touching _status, so a stored Running that is already
         * done is reported as Completed here instead of waiting for someone to update it.
         */
        [[nodiscard]] TaskStatus GetStatus() const {
            const TaskStatus status = _status.load(std::memory_order_acquire);
            if (status == TaskStatus::Running && IsDone()) {
                return TaskStatus::Completed;
            }
            return status;
        }

        /**
         * Получает ошибку задачи.
         */
        [[nodiscard]] TaskError GetError() const {
            return _error.load(std::memory_order_acquire);
        }

        // Awaitable interface для co_await TaskHandleBase
        bool await_ready() const noexcept {
            return IsDone();
        }
        void await_suspend(std::coroutine_handle<> continuation) noexcept {
            _continuation = continuation;
        }
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
            // The handle lives on the heap and never moves, so the promise can keep a pointer to _completion.
            if (_task.IsValid()) {
                _task.GetHandle().promise()._completion = &_completion;
            }
            SetStatus(TaskStatus::Pending);
        }

        ~TaskHandle() override = default;

        // =================================================================
        // TaskHandleBase interface
        // =================================================================

        [[nodiscard]] bool IsDone() const override {
            return !_task.IsValid() || _completion.IsSignaled();
        }

        /**
         * Cancels the handle. A task that has already finished stays Completed and keeps its result.
         */
        void Cancel() override {
            const TaskStatus status = GetStatus();
            if (status == TaskStatus::Pending || status == TaskStatus::Running) {
                SetStatus(TaskStatus::Cancelled);
                _cancelled.store(true, std::memory_order_release);
                _completion.Wake();
            }
        }

        /**
         * Blocks until the task has finished or the handle has been cancelled.
         * Never call it on the main thread for a task scheduled with ThreadType::MainThread: that queue is drained
         * in TaskManager::Update, so the wait would block itself.
         */
        void Wait() override {
            if (!_task.IsValid()) {
                return;
            }

            _completion.WaitUntil([this] { return IsCancelled(); });
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

        Task<T>& GetTask() {
            return _task;
        }
        const Task<T>& GetTask() const {
            return _task;
        }

        /**
         * Запускает выполнение задачи.
         * From here until the coroutine finishes the handle keeps itself alive, so the caller may drop its
         * reference (fire and forget); the coroutine is resumed from pool threads that own nothing.
         * The caller of Start() must hold a reference of its own: the task may finish, and release the
         * last one, before Resume() returns.
         */
        void Start() {
            if (GetStatus() == TaskStatus::Pending) {
                SetStatus(TaskStatus::Running);
                _completion.SetKeepAlive(IntrusivePtr<RefCountedAtomic>(this));
                _task.Resume();
            }
        }

        /**
         * Отмечает задачу как завершенную с ошибкой.
         * Примечание: ResumeContinuation не вызывается здесь,
         * так как FinalAwaiter делает симметричный transfer к continuation.
         */
        void MarkFailed(TaskError error = TaskError::Exception) {
            _error.store(error, std::memory_order_release);
            SetStatus(TaskStatus::Failed);
        }

    private:
        // Declared before _task: the coroutine frame is destroyed first, the event it signals last.
        Detail::CompletionEvent _completion;
        Task<T> _task;
        std::atomic<bool> _cancelled{false};
    };

}  // namespace BECore
