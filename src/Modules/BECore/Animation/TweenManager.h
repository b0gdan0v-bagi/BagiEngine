#pragma once

#include <BECore/Animation/Lerp.h>
#include <BECore/Animation/Timeline.h>
#include <BECore/Animation/Tween.h>
#include <BECore/Animation/TweenAwaiter.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <cstddef>
#include <utility>

namespace BECore {

    /**
     * @brief Runs tweens: changes a value over time from a start to a target through a setter callback.
     *
     * Main thread only, like the scene state; `CoreManager` owns one and ticks it once per frame. A test makes its own and calls
     * `Advance(dt)` with a fake frame time. The order of callbacks, the cancel rules and the waiters are in `Animation/CLAUDE.md`, "Rules"
     * (1 to 10; the tests pin them).
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
         * The first step comes from the next `Advance`, also when the tween is started inside a callback. A @p duration or a delay that is
         * not finite counts as zero. @p onValue is a callable taking `const T&` (`{}` for none) whose capture must fit a `TweenApply`.
         */
        template <Tweenable T, TweenValueCallback<T> TOnValue = std::nullptr_t>
        IntrusivePtr<TweenHandle> Start(T from, T to, TweenSettings settings, TOnValue&& onValue, MoveOnlyFunction<void()> onComplete = {}, MoveOnlyFunction<void()> onCancel = {}) {
            return StartErased(settings, MakeTweenApply<T>(std::move(from), std::move(to), std::forward<TOnValue>(onValue)), std::move(onComplete), std::move(onCancel));
        }

        /// A tween with no setter that only lasts @p seconds of game time (the frame time given to `Advance`): @p onComplete runs at the end, @p onCancel when it is cancelled
        IntrusivePtr<TweenHandle> Delay(float seconds, MoveOnlyFunction<void()> onComplete = {}, MoveOnlyFunction<void()> onCancel = {});

        /// `AwaitTween(Delay(seconds))`: a pause of @p seconds of game time, not wall-clock (`TweenAwaiter`)
        [[nodiscard]] TweenAwaiter Wait(float seconds);

        /**
         * @brief Runs @p timeline as one tween and returns its handle: cancel, progress, `ScopedTween` and `AwaitTween` work on it as on any tween.
         *
         * The tween lasts `timeline.GetDuration()` (repeats included), linear, and every step evaluates the timeline at `progress * duration`,
         * so the last step is exactly the duration. The timeline is moved into the tween. The rules 1 to 10 hold for the handle; the clips
         * have no cancel callbacks of their own, @p onCancel is the one of a cancelled run. A duration that is not finite counts as zero.
         * A clip may cancel its own run: the timeline then stops at that clip (`Animation/CLAUDE.md`, rule 15).
         */
        IntrusivePtr<TweenHandle> Play(Timeline timeline, float delay = 0.0f, MoveOnlyFunction<void()> onComplete = {}, MoveOnlyFunction<void()> onCancel = {});

        /// Advances every tween by @p dt seconds (the frame time). A @p dt of zero, negative or NaN does nothing, and so does a call from a callback
        /// that runs during `Advance` or `CancelAll` (the walk is on)
        void Advance(float dt);

        /// Cancels every tween, running the cancel callbacks in start order. A tween started by a callback during the call is not cancelled
        void CancelAll();

        /// Drops every tween without any callback (shutdown). The handles read Cancelled. From a callback, a tween started later in the same walk stays alive
        void Clear();

        /// Tweens that are not finished yet (waiting or running); walks the list
        [[nodiscard]] size_t GetActiveCount() const;

    private:
        friend class TweenHandle;

        IntrusivePtr<TweenHandle> StartErased(const TweenSettings& settings, TweenApply apply, MoveOnlyFunction<void()> onComplete, MoveOnlyFunction<void()> onCancel);
        void CancelTween(TweenHandle& tween);
        void Step(TweenHandle& tween, float dt);
        void ReleaseCallbacks(TweenHandle& tween);
        void ResumeWaiters(TweenHandle& tween);
        void CancelAllIn(const Vector<IntrusivePtr<TweenHandle>>& list);
        void EndWalk();

        Vector<IntrusivePtr<TweenHandle>> _tweens;   // in start order; a finished one stays until the walk ends
        Vector<IntrusivePtr<TweenHandle>> _pending;  // started while a walk is on: merged when it ends
        TweenHandle* _stepping = nullptr;            // the tween whose setter runs now: its callback must not be destroyed under it
        bool _walking = false;                       // Advance or CancelAll runs: the lists must not move
    };

}  // namespace BECore
