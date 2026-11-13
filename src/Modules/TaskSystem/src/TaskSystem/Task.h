#pragma once

#include <TaskSystem/TaskPriority.h>
#include <coroutine>

namespace BECore {

    // Forward declaration
    template <typename T>
    class Task;

    namespace Detail {

        /**
         * CompletionEvent - one-shot signal "the coroutine reached final_suspend".
         *
         * Owned by TaskHandle; the promise only keeps a pointer to it. A waiting thread blocks on a condition
         * variable instead of spinning, and reads an atomic flag instead of the coroutine frame state
         * (which the worker thread is writing at the same time).
         */
        class CompletionEvent {
        public:
            CompletionEvent() = default;

            CompletionEvent(const CompletionEvent&) = delete;
            CompletionEvent& operator=(const CompletionEvent&) = delete;

            /**
             * Makes the owner of this event stay alive until the task finishes.
             * Signal() hands the reference back, so the owner may be destroyed right inside Signal().
             */
            void SetKeepAlive(IntrusivePtr<RefCountedAtomic> owner) {
                LockGuard lock(_mutex);
                _keepAlive = std::move(owner);
            }

            /**
             * Marks the task as finished, wakes all waiters and drops the keep-alive reference.
             * The notification is sent under the mutex: a waiter cannot return (and destroy the event)
             * until the lock is released. Nothing may touch the event after this call returns: if the
             * keep-alive reference was the last one, the event is gone.
             */
            void Signal() {
                IntrusivePtr<RefCountedAtomic> keepAlive;
                {
                    LockGuard lock(_mutex);
                    _signaled.store(true, std::memory_order_release);
                    keepAlive = std::move(_keepAlive);
                    _condition.notify_all();
                }
            }

            /**
             * Takes the keep-alive reference away from a task that will never finish (see LiveTasks::AbandonAll).
             * The caller releases it outside its own locks. Empty if the task never started or has already finished,
             * because Signal() hands the reference back itself.
             */
            [[nodiscard]] IntrusivePtr<RefCountedAtomic> ReleaseKeepAlive() {
                LockGuard lock(_mutex);
                return std::move(_keepAlive);
            }

            /**
             * Wakes waiters without finishing the task, so they re-check their own predicate (used by Cancel).
             */
            void Wake() {
                LockGuard lock(_mutex);
                _condition.notify_all();
            }

            [[nodiscard]] bool IsSignaled() const noexcept {
                return _signaled.load(std::memory_order_acquire);
            }

            /**
             * Blocks until the task has finished or interrupted() returns true.
             */
            template <typename Predicate>
            void WaitUntil(Predicate interrupted) {
                UniqueLock lock(_mutex);
                _condition.wait(lock, [&] { return _signaled.load(std::memory_order_acquire) || interrupted(); });
            }

        private:
            Mutex _mutex{BE_LOCK_SITE("Task completion")};
            ConditionVariable _condition;
            std::atomic<bool> _signaled{false};
            IntrusivePtr<RefCountedAtomic> _keepAlive;  // guarded by _mutex
        };

        /**
         * Базовый promise_type для Task<T>.
         * Содержит общую логику для void и non-void типов.
         */
        /**
         * FinalAwaiter - awaiter для final_suspend.
         * Определён вне локального контекста, чтобы поддерживать шаблонный await_suspend.
         */
        struct FinalAwaiter {
            bool await_ready() noexcept {
                return false;
            }

            template <typename PromiseType>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<PromiseType> h) noexcept {
                auto& promise = h.promise();

                // After Signal() a waiting thread may destroy the frame, so nothing is read from the promise past it.
                const std::coroutine_handle<> continuation = promise._continuation;
                if (promise._completion) {
                    promise._completion->Signal();
                }

                if (continuation) {
                    return continuation;
                }
                return std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        template <typename T>
        struct TaskPromiseBase {
            TaskError _error = TaskError::None;
            std::coroutine_handle<> _continuation;
            CompletionEvent* _completion = nullptr;  // set by TaskHandle; null for tasks awaited via co_await

            auto initial_suspend() noexcept {
                return std::suspend_always{};
            }

            FinalAwaiter final_suspend() noexcept {
                return FinalAwaiter{};
            }

            void unhandled_exception() noexcept {
                _error = TaskError::Exception;
                // В debug можно логировать через ASSERT
                ASSERT(false, "Unhandled exception in coroutine");
            }

            void SetContinuation(std::coroutine_handle<> continuation) noexcept {
                _continuation = continuation;
            }
        };

        /**
         * Promise для Task<T> где T != void.
         * Хранит возвращаемое значение.
         */
        template <typename T>
        struct TaskPromise : TaskPromiseBase<T> {
            std::variant<std::monostate, T> _result;

            Task<T> get_return_object() noexcept;

            void return_value(T value) noexcept {
                _result.template emplace<T>(std::move(value));
            }

            std::expected<T, TaskError> GetResult() & {
                if (this->_error != TaskError::None) {
                    return std::unexpected(this->_error);
                }
                return std::get<T>(_result);
            }

            std::expected<T, TaskError> GetResult() && {
                if (this->_error != TaskError::None) {
                    return std::unexpected(this->_error);
                }
                return std::move(std::get<T>(_result));
            }
        };

        /**
         * Promise для Task<void>.
         * Не хранит значение, только ошибку.
         */
        template <>
        struct TaskPromise<void> : TaskPromiseBase<void> {
            Task<void> get_return_object() noexcept;

            void return_void() noexcept {}

            std::expected<void, TaskError> GetResult() {
                if (this->_error != TaskError::None) {
                    return std::unexpected(this->_error);
                }
                return {};
            }
        };

    }  // namespace Detail

    /**
     * Task<T> - основной тип корутины для асинхронных операций.
     *
     * Представляет отложенное вычисление, которое может быть запущено
     * и ожидаемо через co_await.
     *
     * @tparam T Тип возвращаемого значения (void для задач без результата)
     *
     * @example
     * Task<int> ComputeAsync() {
     *     co_return 42;
     * }
     *
     * Task<void> DoWorkAsync() {
     *     int result = co_await ComputeAsync();
     *     LOG_INFO("Result: {}"_format(result));
     * }
     */
    template <typename T = void>
    class [[nodiscard]] Task {
    public:
        using promise_type = Detail::TaskPromise<T>;
        using handle_type = std::coroutine_handle<promise_type>;

        Task() noexcept = default;

        explicit Task(handle_type handle) noexcept : _handle(handle) {}

        ~Task() {
            if (_handle) {
                _handle.destroy();
            }
        }

        // Move-only
        Task(Task&& other) noexcept : _handle(other._handle) {
            other._handle = nullptr;
        }

        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (_handle) {
                    _handle.destroy();
                }
                _handle = other._handle;
                other._handle = nullptr;
            }
            return *this;
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        // =================================================================
        // Awaitable interface
        // =================================================================

        bool await_ready() const noexcept {
            return _handle.done();
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
            _handle.promise().SetContinuation(continuation);
            return _handle;
        }

        decltype(auto) await_resume() {
            return std::move(_handle.promise()).GetResult();
        }

        // =================================================================
        // Task control
        // =================================================================

        /**
         * Проверяет, завершена ли задача.
         */
        [[nodiscard]] bool IsDone() const noexcept {
            return !_handle || _handle.done();
        }

        /**
         * Проверяет валидность задачи.
         */
        [[nodiscard]] bool IsValid() const noexcept {
            return static_cast<bool>(_handle);
        }

        /**
         * Явный запуск задачи (если не запущена автоматически).
         */
        void Resume() {
            if (_handle && !_handle.done()) {
                _handle.resume();
            }
        }

        /**
         * Получение результата (блокирующее).
         * Запускает корутину до завершения.
         */
        decltype(auto) GetResult() {
            while (!IsDone()) {
                _handle.resume();
            }
            return std::move(_handle.promise()).GetResult();
        }

        /**
         * Получение handle для низкоуровневого доступа.
         */
        [[nodiscard]] handle_type GetHandle() const noexcept {
            return _handle;
        }

    private:
        handle_type _handle = nullptr;
    };

    // =================================================================
    // get_return_object implementations
    // =================================================================

    namespace Detail {

        template <typename T>
        Task<T> TaskPromise<T>::get_return_object() noexcept {
            return Task<T>{std::coroutine_handle<TaskPromise<T>>::from_promise(*this)};
        }

        inline Task<void> TaskPromise<void>::get_return_object() noexcept {
            return Task<void>{std::coroutine_handle<TaskPromise<void>>::from_promise(*this)};
        }

    }  // namespace Detail

}  // namespace BECore
