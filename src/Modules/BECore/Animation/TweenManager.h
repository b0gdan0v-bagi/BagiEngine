#pragma once

#include <BECore/Animation/Lerp.h>
#include <BECore/Animation/Timeline.h>
#include <BECore/Animation/Tween.h>
#include <BECore/Animation/TweenAwaiter.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace BECore {

    /// How a slot of `TweenManager` uses its time: `Once` ends at its duration (`Start`, `Delay`, a finite `Play`), `Loop` wraps there and goes on
    /// (`TweenManager::Loop`), `Endless` hands its setter the seconds of the step (`Play` of an endless timeline)
    CORE_ENUM(TweenRunMode, uint8_t, Once, Loop, Endless)

    /**
     * @brief Runs tweens: changes a value over time from a start to a target through a setter callback.
     *
     * Main thread only, like the scene state; `CoreManager` owns one and ticks it once per frame. A test makes its own and calls
     * `Advance(dt)` with a fake frame time. The order of callbacks, the cancel rules and the waiters are in `Animation/CLAUDE.md`, "Rules"
     * (1 to 10; the tests pin them; rule 16 for an endless `Play`, 17 for `Loop`).
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

        /**
         * @brief A tween that runs turn after turn in the slot it started in: it keeps its place in the walk, its handle and its setter, and no
         * turn allocates (`Animation/CLAUDE.md`, rule 17). A repeat uses it instead of a restart from `onComplete`.
         *
         * The delay of @p settings is applied once. Every `Advance` after it calls @p onValue once with the value of the current turn; the time past
         * the end of a turn goes into the next one, so @p to is not necessarily written at the end of a turn. It never completes: only a cancel ends
         * it, and @p onCancel runs then. A duration of zero or less, or not finite, makes an ordinary tween of no length instead (@p to once,
         * `Completed`).
         */
        template <Tweenable T, TweenValueCallback<T> TOnValue>
        IntrusivePtr<TweenHandle> Loop(T from, T to, TweenSettings settings, TOnValue&& onValue, MoveOnlyFunction<void()> onCancel = {}) {
            return LoopErased(settings, MakeTweenApply<T>(std::move(from), std::move(to), std::forward<TOnValue>(onValue)), std::move(onCancel));
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
         *
         * An endless timeline (`RepeatForever()`) makes a handle that runs until it is cancelled (`Cancel()`, `CancelAll`, a `ScopedTween`):
         * it never completes, `onComplete` never runs, `GetProgress()` stays 0 and an await on it ends only by cancel. Every step hands the
         * timeline the seconds since the last one (`Timeline::Advance`), so a run of hours keeps its precision. The handle takes the
         * frame class of the timeline (`TweenHandle::GetFrameClass`).
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

        /// Tweens of the frame class @p frameClass that are not finished yet: what the frame loop asks to know whether an endless decorative
        /// loop is the only thing that runs. The counts of the three classes add up to `GetActiveCount()`
        [[nodiscard]] size_t GetActiveCount(AnimationFrameClass frameClass) const;

    private:
        friend class TweenHandle;

        /// What every frame reads and writes of one tween, by value: the walk touches no handle and no reference count. A finished tween
        /// leaves a tombstone (`state` Completed or Cancelled) until a compaction
        struct Slot {
            float elapsed = 0.0f;   // raw time since Start, the delay included; of a `Loop` past its delay, the time into the current turn
            float duration = 0.0f;  // finite, see `StartErased`
            float delay = 0.0f;     // finite
            float progress = 0.0f;
            uint32_t id = 0;
            EaseType ease = EaseType::Linear;
            TweenState state = TweenState::Waiting;
            AnimationFrameClass frameClass = AnimationFrameClass::FiniteEffect;
            TweenRunMode mode = TweenRunMode::Once;
            TweenApply apply;
        };

        static constexpr size_t kCacheLineBytes = 64;
        static_assert(sizeof(Slot) <= 2 * kCacheLineBytes, "a slot is walked every frame: keep it within two cache lines");

        /// A finished tween taken out of the lists: the owner keeps its handle alive across the callbacks, the setter is destroyed by the caller
        /// after the bookkeeping (its capture may call back into the manager)
        struct Retired {
            IntrusivePtr<TweenHandle> owner;
            TweenApply apply;
        };

        [[nodiscard]] static bool IsLive(TweenState state) {
            return state == TweenState::Waiting || state == TweenState::Running;
        }

        [[nodiscard]] Slot& SlotOf(uint32_t id);
        [[nodiscard]] const Slot& SlotOf(uint32_t id) const;
        [[nodiscard]] uint32_t AcquireId();
        IntrusivePtr<TweenHandle> StartErased(const TweenSettings& settings, TweenApply apply, MoveOnlyFunction<void()> onComplete, MoveOnlyFunction<void()> onCancel,
                                              TweenRunMode mode = TweenRunMode::Once, AnimationFrameClass frameClass = AnimationFrameClass::FiniteEffect);
        IntrusivePtr<TweenHandle> LoopErased(const TweenSettings& settings, TweenApply apply, MoveOnlyFunction<void()> onCancel);
        void CancelTween(uint32_t id);
        void Step(Slot& slot, float dt);
        void StepLoop(Slot& slot, float dt);
        void StepEndless(Slot& slot, float dt);
        void CallSetter(Slot& slot, float value);
        [[nodiscard]] Retired Retire(Slot& slot, TweenState terminal);
        void Complete(Slot& slot);
        void ResumeWaiters(TweenHandle& tween, uint32_t clearCount);
        void EndWalk();
        void Compact();
        void MergePending();

        // `Advance` walks `_slots` in start order and a setter or callback runs with a reference into it. So `_slots` is never resized while
        // `_walking` (a finish leaves a tombstone, a start goes to `_pending`, merged by `EndWalk`), and nothing keeps a slot reference
        // across a point where `_walking` is off
        Vector<Slot> _slots;
        Vector<Slot> _pending;
        // Indexed by id, not by position, so a compaction rewrites `_indexOfId` and never touches a handle. An id is reserved from its start
        // until the slot that held it is compacted away, so a tombstone and a new tween never share one
        Vector<IntrusivePtr<TweenHandle>> _owners;  // null for a finished tween and a free id
        Vector<uint32_t> _indexOfId;                // a position in `_slots`, or in `_pending` with `kPendingBit`
        Vector<uint32_t> _freeIds;
        size_t _tombstones = 0;                    // in `_slots`
        uint32_t _stepping = kNoId;                // the tween whose setter runs now: its setter must not be destroyed under it
        IntrusivePtr<TweenHandle> _steppingOwner;  // a tween finished by its own setter: the handle lives until the setter returns
        uint32_t _clearCount = 0;                  // counts `Clear()` calls: a tween's waiters are dropped, not resumed, when one ran since it finished
        bool _walking = false;                     // Advance or CancelAll runs: the lists must not move

        static constexpr uint32_t kNoId = UINT32_MAX;
        static constexpr uint32_t kPendingBit = 1u << 31;
        static constexpr size_t kMinTombstones = 64;  // fewer than this and a walk never compacts
        static constexpr size_t kTombstoneShare = 8;  // a walk compacts once a share of 1 / this of the slots are tombstones
    };

}  // namespace BECore
