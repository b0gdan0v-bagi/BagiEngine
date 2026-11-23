#pragma once

#include <BECore/Memory/Pooled.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Types/Types.h>
#include <TaskSystem/TaskPriority.h>
#include <atomic>
#include <expected>
#include <utility>

namespace BECore {

    /**
     * Token of cooperative cancellation; copies share one state, and any thread may cancel or poll it.
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

        static CancellationToken Create() {
            return CancellationToken(New<State>());
        }

        /** True when this token or any of its parents was cancelled. */
        [[nodiscard]] bool IsCancelled() const {
            for (const State* state = _state.Get(); state != nullptr; state = state->parent.Get()) {
                if (state->cancelled.load(std::memory_order_acquire)) {
                    return true;
                }
            }
            return false;
        }

        /** Parent tokens are not affected. */
        void Cancel() {
            if (_state) {
                _state->cancelled.store(true, std::memory_order_release);
            }
        }

        /** The `Cancelled` error when IsCancelled(), otherwise a value. */
        [[nodiscard]] std::expected<void, TaskError> CheckCancellation() const {
            if (IsCancelled()) {
                return std::unexpected(TaskError::Cancelled);
            }
            return {};
        }

        /** False for a default-constructed token (it has no state and never reads as cancelled). */
        [[nodiscard]] bool IsValid() const {
            return _state != nullptr;
        }

        /**
         * Creates a child: cancelling the parent cancels it, cancelling it leaves the parent alone.
         * The child holds its own count of the parent state, so destruction order does not matter.
         */
        [[nodiscard]] CancellationToken CreateLinked() const {
            auto state = New<State>();
            state->parent = _state;
            return CancellationToken(std::move(state));
        }

    private:
        // Atomic: copies of a token (and the children's parent links) are dropped by whichever thread ends last; for the same reason the pool is the concurrent one.
        struct State : RefCountedAtomic {
            BE_POOLED(CancellationToken::State)
            std::atomic<bool> cancelled{false};
            IntrusivePtr<State> parent;
        };

        explicit CancellationToken(IntrusivePtr<State> state) : _state(std::move(state)) {}

        IntrusivePtr<State> _state;
    };

}  // namespace BECore
