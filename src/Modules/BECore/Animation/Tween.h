#pragma once

#include <BECore/Animation/Easing.h>
#include <BECore/Animation/Lerp.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/FixedFunction.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace BECore {

    class TweenManager;

    /// How a tween runs. The value it moves is not here: it is the arguments of `TweenManager::Start`
    struct TweenSettings {
        float duration = 0.25f;            ///< seconds from the end of the delay to the target; zero or less (or not finite) completes on the first `Advance` after the delay
        float delay = 0.0f;                ///< seconds before the first setter call (not finite counts as zero); the setter is not called during it
        EaseType ease = EaseType::Linear;  ///< the curve of the progress, see `Ease`
    };

    /// The inline buffer of a tween setter, which holds `from`, `to` and the user's callable: one cache line with its two pointers, enough for a
    /// `Vec4` with a pointer and for the biggest setter of the engine's own tools (the entities of `BagiStress`)
    inline constexpr size_t kTweenApplyBytes = 48;

    /// The eased progress in, the value out: the setter of one tween clip, type-erased so the manager has no code per value type. It never
    /// allocates: a callable that does not fit `kTweenApplyBytes` is a compile error (`MakeTweenApply` says it in words)
    using TweenApply = FixedFunction<kTweenApplyBytes, void(float)>;

    /// What `onValue` of a typed tween can be: a callable taking `const T&`, or `std::nullptr_t` (`{}` and `nullptr`) for a tween with no setter.
    /// An empty `MoveOnlyFunction` is not a "no setter": calling it asserts
    template <typename TOnValue, typename T>
    concept TweenValueCallback = std::is_same_v<std::decay_t<TOnValue>, std::nullptr_t> || std::is_invocable_v<std::decay_t<TOnValue>&, const T&>;

    /// The setter of a typed tween from @p from to @p to: `onValue(Lerp(from, to, easedT))`. The one place that builds it, for `TweenManager::Start`
    /// and the clips of `Timeline`; @p onValue is moved into the capture (a move-only one is fine)
    template <Tweenable T, TweenValueCallback<T> TOnValue>
    [[nodiscard]] TweenApply MakeTweenApply(T from, T to, TOnValue&& onValue) {
        if constexpr (std::is_same_v<std::decay_t<TOnValue>, std::nullptr_t>) {
            return TweenApply([](float) {});
        } else {
            auto apply = [from = std::move(from), to = std::move(to), onValue = std::forward<TOnValue>(onValue)](float easedT) mutable { onValue(Lerp(from, to, easedT)); };
            static_assert(kFunctionFitsInline<decltype(apply), kTweenApplyBytes>, "onValue does not fit a tween setter (kTweenApplyBytes with `from` and `to`; its move must be noexcept, which a "
                                                                                  "captured const object is not): capture a pointer to an object that holds the state");
            return TweenApply(std::move(apply));
        }
    }

    /// Waiting (the delay has not passed), Running, then Completed or Cancelled (never both)
    CORE_ENUM(TweenState, uint8_t, Waiting, Running, Completed, Cancelled)

    /**
     * @brief How much the frame loop has to be awake for a tween (the contract of the render-on-demand entry; the names and the meaning of
     * `ParticleFrameClass`).
     *
     * Interactive needs a frame all the time, FiniteEffect needs frames until the tween ends, DecorativeLoop is an endless loop that only
     * decorates (it may run at a lower rate or not at all while the rest of the picture sleeps). `TweenManager::GetActiveCount(class)` counts them.
     */
    CORE_ENUM(AnimationFrameClass, uint8_t, Interactive, FiniteEffect, DecorativeLoop)

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
     * **Waiters.** A coroutine suspended on the handle (`AwaitTween`) is resumed inline after the `onComplete` or `onCancel` callback;
     * `Clear()` and the destructor of the manager drop it without resuming it (`Animation/CLAUDE.md`, rule 9 and 10).
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

        /// 0..1 of the raw time (not eased) since the end of the delay: 0 while waiting, 1 once completed, where it stopped when cancelled.
        /// Always 0 for the handle of an endless timeline (`TweenManager::Play`): it has no end to measure against
        [[nodiscard]] float GetProgress() const {
            return _progress;
        }

        /// The frame class of the tween: `FiniteEffect` for `Start` and `Delay`, the class of the timeline for `Play`
        [[nodiscard]] AnimationFrameClass GetFrameClass() const {
            return _frameClass;
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
        AnimationFrameClass _frameClass = AnimationFrameClass::FiniteEffect;
        bool _endless = false;  // set by `Play` of an endless timeline: no completion, the setter gets seconds instead of a progress
        float _progress = 0.0f;
        float _elapsed = 0.0f;  // raw time since Start, the delay included
        TweenSettings _settings;
        TweenApply _apply;  // the setter of the typed Start
        MoveOnlyFunction<void()> _onComplete;
        MoveOnlyFunction<void()> _onCancel;
        Vector<Waiter> _waiters;  // in registration order; resumed (and removed one by one) by the manager when the tween finishes
    };

}  // namespace BECore
