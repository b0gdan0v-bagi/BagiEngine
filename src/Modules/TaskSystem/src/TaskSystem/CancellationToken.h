#pragma once

#include <BECore/RefCounted/New.h>
#include <BECore/Types/Types.h>
#include <TaskSystem/TaskPriority.h>
#include <atomic>
#include <expected>
#include <utility>

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
            return CancellationToken(New<State>());
        }

        /**
         * Checks whether cancellation was requested on this token or on any of its parents.
         */
        [[nodiscard]] bool IsCancelled() const {
            for (const State* state = _state.Get(); state != nullptr; state = state->parent.Get()) {
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
         * The child keeps its parent alive through its own count of the parent state, so destruction order does not matter.
         */
        [[nodiscard]] CancellationToken CreateLinked() const {
            auto state = New<State>();
            state->parent = _state;
            return CancellationToken(std::move(state));
        }

    private:
        // Atomic: copies of a token (and the children's parent links) are dropped by whichever thread ends last.
        struct State : RefCountedAtomic {
            std::atomic<bool> cancelled{false};
            IntrusivePtrAtomic<State> parent;
        };

        explicit CancellationToken(IntrusivePtrAtomic<State> state) : _state(std::move(state)) {}

        IntrusivePtrAtomic<State> _state;
    };

}  // namespace BECore
