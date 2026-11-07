#pragma once

#include <BECore/Animation/Easing.h>
#include <coroutine>

namespace BECore {

    class TweenManager;

    /// How a tween runs. The value it moves is not here: it is the arguments of `TweenManager::Start`
    struct TweenSettings {
        float duration = 0.25f;            ///< seconds from the end of the delay to the target; zero or less (or not finite) completes on the first `Advance` after the delay
        float delay = 0.0f;                ///< seconds before the first setter call (not finite counts as zero); the setter is not called during it
        EaseType ease = EaseType::Linear;  ///< the curve of the progress, see `Ease`
    };

    /// Waiting (the delay has not passed), Running, then Completed or Cancelled (never both)
    CORE_ENUM(TweenState, uint8_t, Waiting, Running, Completed, Cancelled)

    /// How a tween that was awaited ended (`TweenAwaiter`)
    CORE_ENUM(TweenResult, uint8_t, Completed, Cancelled)

    /**
     * @brief The handle of one tween: its state and its progress, and a way to cancel it.
     *
     * Made by `TweenManager::Start`, main thread only (like the manager and the scene). It stays valid after the tween ends, so the
     * state and the progress can be read later. Dropping a handle does not stop the tween (fire and forget, like `TaskHandle`): the
     * manager keeps it running. A handle that outlives its manager reads Cancelled and does nothing on `Cancel()`.
     *
     * **Lifetime of the setter.** The setter of a tween often captures a pointer to an object (a component, a node). The tween does not
     * know when that object dies: cancel it first, from the owner (a `ScopedTween` member does it in its destructor).
     *
     * **Waiters.** A coroutine suspended on the handle (`AwaitTween`) is a waiter. The manager resumes the waiters inline, in the order
     * they registered, right after the `onComplete` or `onCancel` callback of the tween (a waiter is a callback and has all the rules of
     * one). `Clear()` and the destructor of the manager drop them without resuming them.
     */
    class TweenHandle : public RefCountedNonAtomic {
    public:
        [[nodiscard]] TweenState GetState() const {
            return _state;
        }

        /// Completed or Cancelled: no more callbacks will run
        [[nodiscard]] bool IsFinished() const {
            return _state == TweenState::Completed || _state == TweenState::Cancelled;
        }

        [[nodiscard]] bool IsCancelled() const {
            return _state == TweenState::Cancelled;
        }

        /// 0..1 of the raw time (not eased) since the end of the delay: 0 while waiting, 1 once completed, where it stopped when cancelled
        [[nodiscard]] float GetProgress() const {
            return _progress;
        }

        /**
         * @brief Cancels the tween: the cancel callback runs once, at once, and the setter is not called again. The value is not set.
         *
         * Does nothing on a tween that is finished already (completed, cancelled, or whose manager is gone). Safe from inside any
         * callback of this manager, also the callbacks of this tween.
         */
        void Cancel();

        /**
         * @brief Registers @p continuation to be resumed when the tween finishes. @p owner identifies the waiter for `RemoveWaiter`
         * (the awaiter passes `this`). Does nothing on a tween that is finished already: the caller checks `IsFinished()` first.
         */
        void AddWaiter(const void* owner, std::coroutine_handle<> continuation) {
            if (!IsFinished()) {
                _waiters.push_back({owner, continuation});
            }
        }

        /// Unregisters the waiter @p owner (nothing when it is not registered, also on a finished tween whose waiters are being resumed)
        void RemoveWaiter(const void* owner) {
            const auto found = eastl::find_if(_waiters.begin(), _waiters.end(), [owner](const Waiter& waiter) { return waiter.owner == owner; });
            if (found != _waiters.end()) {
                _waiters.erase(found);
            }
        }

    private:
        struct Waiter {
            const void* owner = nullptr;
            std::coroutine_handle<> continuation;
        };

        friend class TweenManager;

        TweenManager* _manager = nullptr;  // null once the tween is finished
        TweenState _state = TweenState::Waiting;
        float _progress = 0.0f;
        float _elapsed = 0.0f;  // raw time since Start, the delay included
        TweenSettings _settings;
        eastl::function<void(float)> _apply;  // the eased progress in, the value out (the setter of the typed Start)
        eastl::function<void()> _onComplete;
        eastl::function<void()> _onCancel;
        eastl::vector<Waiter> _waiters;  // in registration order; resumed (and removed one by one) by the manager when the tween finishes
    };

}  // namespace BECore
