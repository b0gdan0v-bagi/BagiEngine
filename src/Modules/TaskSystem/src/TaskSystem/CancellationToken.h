#pragma once

#include <TaskSystem/TaskPriority.h>

namespace BECore {

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
            return CancellationToken(MakeShared<State>());
        }

        /**
         * Checks whether cancellation was requested on this token or on any of its parents.
         */
        [[nodiscard]] bool IsCancelled() const {
            for (const State* state = _state.get(); state != nullptr; state = state->parent.get()) {
                if (state->cancelled.load(std::memory_order_acquire)) {
                    return true;
                }
            }
            return false;
        }

        /**
         * Requests cancellation. Parent tokens are not affected.
         */
        void Cancel() {
            if (_state) {
                _state->cancelled.store(true, std::memory_order_release);
            }
        }

        /**
         * Проверяет отмену и возвращает std::expected.
         * @return std::expected<void, TaskError> с ошибкой Cancelled, если отменено
         */
        [[nodiscard]] std::expected<void, TaskError> CheckCancellation() const {
            if (IsCancelled()) {
                return std::unexpected(TaskError::Cancelled);
            }
            return {};
        }

        /**
         * Проверяет валидность токена.
         */
        [[nodiscard]] bool IsValid() const {
            return _state != nullptr;
        }

        /**
         * Creates a linked (child) token.
         * Cancelling the parent cancels the child; cancelling the child leaves the parent alone.
         * The child keeps its parent alive through shared_ptr, so destruction order does not matter.
         */
        [[nodiscard]] CancellationToken CreateLinked() const {
            auto state = MakeShared<State>();
            state->parent = _state;
            return CancellationToken(std::move(state));
        }

    private:
        struct State {
            std::atomic<bool> cancelled{false};
            SharedPtr<State> parent;
        };

        explicit CancellationToken(SharedPtr<State> state) : _state(std::move(state)) {}

        SharedPtr<State> _state;
    };

}  // namespace BECore
