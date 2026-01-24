#pragma once

#include <coroutine>
#include <exception>
#include <variant>

namespace BECore {

    // Forward declaration
    template <typename T>
    class Task;

    namespace Detail {

        /**
         * Базовый promise_type для Task<T>.
         * Содержит общую логику для void и non-void типов.
         */
        template <typename T>
        struct TaskPromiseBase {
            std::exception_ptr _exception;
            std::coroutine_handle<> _continuation;

            auto initial_suspend() noexcept { return std::suspend_always{}; }

            auto final_suspend() noexcept {
                struct FinalAwaiter {
                    bool await_ready() noexcept { return false; }

                    std::coroutine_handle<> await_suspend(std::coroutine_handle<TaskPromiseBase> h) noexcept {
                        if (h.promise()._continuation) {
                            return h.promise()._continuation;
                        }
                        return std::noop_coroutine();
                    }

                    void await_resume() noexcept {}
                };
                return FinalAwaiter{};
            }

            void unhandled_exception() noexcept {
                _exception = std::current_exception();
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

            T& GetResult() & {
                if (this->_exception) {
                    std::rethrow_exception(this->_exception);
                }
                return std::get<T>(_result);
            }

            T GetResult() && {
                if (this->_exception) {
                    std::rethrow_exception(this->_exception);
                }
                return std::move(std::get<T>(_result));
            }
        };

        /**
         * Promise для Task<void>.
         * Не хранит значение, только исключение.
         */
        template <>
        struct TaskPromise<void> : TaskPromiseBase<void> {
            Task<void> get_return_object() noexcept;

            void return_void() noexcept {}

            void GetResult() {
                if (this->_exception) {
                    std::rethrow_exception(this->_exception);
                }
            }
        };

    } // namespace Detail

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

    } // namespace Detail

} // namespace BECore
