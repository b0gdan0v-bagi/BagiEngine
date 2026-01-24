#pragma once

#include <atomic>
#include <stdexcept>

namespace BECore {

    /**
     * Исключение, выбрасываемое при отмене задачи.
     */
    class TaskCancelledException : public std::exception {
    public:
        const char* what() const noexcept override {
            return "Task was cancelled";
        }
    };

    /**
     * CancellationToken - токен для кооперативной отмены задач.
     * 
     * @example
     * Task<void> MyTask(CancellationToken token) {
     *     while (!token.IsCancelled()) {
     *         DoWork();
     *         co_await Yield();
     *     }
     * }
     */
    class CancellationToken {
    public:
        CancellationToken() = default;

        /**
         * Создает новый токен отмены.
         */
        static CancellationToken Create() {
            return CancellationToken(eastl::make_shared<std::atomic<bool>>(false));
        }

        /**
         * Проверяет, была ли запрошена отмена.
         */
        [[nodiscard]] bool IsCancelled() const {
            return _cancelled && _cancelled->load(std::memory_order_acquire);
        }

        /**
         * Запрашивает отмену.
         */
        void Cancel() {
            if (_cancelled) {
                _cancelled->store(true, std::memory_order_release);
            }
        }

        /**
         * Выбрасывает исключение, если была запрошена отмена.
         */
        void ThrowIfCancelled() const {
            if (IsCancelled()) {
                throw TaskCancelledException();
            }
        }

        /**
         * Проверяет валидность токена.
         */
        [[nodiscard]] bool IsValid() const {
            return _cancelled != nullptr;
        }

        /**
         * Создает связанный токен (дочерний).
         * Отмена родителя автоматически отменяет дочерний.
         */
        [[nodiscard]] CancellationToken CreateLinked() const {
            auto child = Create();
            // TODO: реализовать связывание
            return child;
        }

    private:
        explicit CancellationToken(eastl::shared_ptr<std::atomic<bool>> cancelled)
            : _cancelled(std::move(cancelled)) {}

        eastl::shared_ptr<std::atomic<bool>> _cancelled;
    };

} // namespace BECore
