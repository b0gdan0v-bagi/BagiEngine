#pragma once

#include <BECore/Animation/Lerp.h>
#include <BECore/Animation/Tween.h>

namespace BECore {

    /**
     * @brief Runs tweens: changes a value over time from a start to a target through a setter callback.
     *
     * Main thread only, like the scene state; `CoreManager` owns one and ticks it once per frame. A test makes its own and calls
     * `Advance(dt)` with a fake frame time. The rules (the tests pin them):
     * 1. Tweens advance in the order they were started; the callbacks of one `Advance` fire in that order.
     * 2. A tween started inside a callback is first advanced by the NEXT `Advance`, never in the same one. The time left over after a
     *    tween finishes is dropped, not carried.
     * 3. The setter is not called during the delay. The `Advance` that passes the delay applies the time left after it. `from` is not
     *    applied before the first setter call.
     * 4. The last setter call of a tween that finishes is exactly `to`, then the completion callback runs once. Never both completion
     *    and cancel.
     * 5. A duration of zero or less completes on the first `Advance` after the delay. A `dt` that is zero, negative or NaN advances nothing.
     * 6. `TweenHandle::Cancel` runs the cancel callback once, at once, and the tween gets no more setter calls; it does not set the value.
     *    On a finished tween it does nothing. Cancelling from inside a callback (this tween or another) is safe.
     * 7. `CancelAll` cancels with callbacks, in start order. `Clear` (shutdown) drops every tween WITHOUT callbacks, because what the
     *    callbacks point at may be gone.
     * 8. A setter that captures a pointer to an object must be cancelled before that object dies (hold a `ScopedTween` in the owner).
     *    Dropping a handle does not stop its tween.
     *
     * The destructor drops the tweens like `Clear`: no callbacks, the surviving handles read Cancelled. Destroying the manager from
     * inside one of its callbacks is not supported.
     */
    class TweenManager {
    public:
        TweenManager() = default;
        ~TweenManager();

        TweenManager(const TweenManager&) = delete;
        TweenManager& operator=(const TweenManager&) = delete;

        /**
         * @brief Starts a tween of a value of type @p T from @p from to @p to; @p onValue gets the interpolated value on every step.
         *
         * @p onComplete runs once after the last setter call (which is exactly @p to), @p onCancel once when the tween is cancelled.
         * The first step comes from the next `Advance`, also when the tween is started inside a callback.
         */
        template <Tweenable T>
        IntrusivePtr<TweenHandle> Start(T from, T to, TweenSettings settings, eastl::function<void(const std::type_identity_t<T>&)> onValue, eastl::function<void()> onComplete = {},
                                        eastl::function<void()> onCancel = {}) {
            auto apply = [from = std::move(from), to = std::move(to), onValue = std::move(onValue)](float easedT) {
                if (onValue) {
                    onValue(Lerp(from, to, easedT));
                }
            };
            return StartErased(settings, std::move(apply), std::move(onComplete), std::move(onCancel));
        }

        /// Advances every tween by @p dt seconds (the frame time). A @p dt of zero, negative or NaN does nothing, and so does a call from a callback that runs during `Advance` or `CancelAll` (the
        /// walk is on). A cancel callback run by a direct `TweenHandle::Cancel()` outside a walk may call it, and it advances.
        void Advance(float dt);

        /// Cancels every tween, running the cancel callbacks in start order. A tween that a callback starts after the call (a cancel callback of this very call, or a callback of the walk in progress
        /// when called from one) is not cancelled.
        void CancelAll();

        /// Drops every tween without any callback (shutdown). The handles read Cancelled. Called from a callback, a tween started later in the same walk stays alive.
        void Clear();

        /// Tweens that are not finished yet (waiting or running); walks the list
        [[nodiscard]] size_t GetActiveCount() const;

    private:
        friend class TweenHandle;

        IntrusivePtr<TweenHandle> StartErased(const TweenSettings& settings, eastl::function<void(float)> apply, eastl::function<void()> onComplete, eastl::function<void()> onCancel);
        void CancelTween(TweenHandle& tween);
        void Step(TweenHandle& tween, float dt);
        void ReleaseCallbacks(TweenHandle& tween);
        void EndWalk();

        eastl::vector<IntrusivePtr<TweenHandle>> _tweens;   // in start order; a finished one stays until the walk ends
        eastl::vector<IntrusivePtr<TweenHandle>> _pending;  // started while a walk is on: merged when it ends
        TweenHandle* _stepping = nullptr;                   // the tween whose setter runs now: its callback must not be destroyed under it
        bool _walking = false;                              // Advance or CancelAll runs: the lists must not move
    };

}  // namespace BECore
