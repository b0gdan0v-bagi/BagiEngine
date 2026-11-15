#pragma once

#include <BECore/Animation/Lerp.h>
#include <BECore/Animation/Tween.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <cstddef>
#include <utility>

namespace BECore {

    /**
     * @brief A set of clips (tweens, callbacks, sub-timelines) on one local time line, run in sequence and in parallel.
     *
     * Pure maths, no manager and no clock: the owner calls `Evaluate(time)` with the time since the start (or `Advance(dt)` with the
     * time since the last call, for a run of any length); `TweenManager::Play` runs
     * one as a tween. What `Evaluate`, `Advance`, `Repeat`, `Yoyo` and the stop check guarantee is in `Animation/CLAUDE.md`, "Rules" (11 to 15).
     *
     * **Building.** Every clip has a start and a length on the local time line; the duration of one iteration is the latest end.
     * `Append` starts a clip when the timeline so far ends, `Join` with the last clip added (by any call), `Insert` at an absolute
     * time. The `delay` of the `TweenSettings` of a clip lengthens its slot. A start before zero, or not finite, counts as zero; a duration
     * or a delay that is negative or not finite counts as zero. Build the timeline before the first `Evaluate`; a callback must not add
     * clips to the timeline that calls it.
     *
     * Clips are visited in order of start, equal starts in insertion order; a sub-timeline is evaluated with its own local time (the
     * time minus its start).
     *
     * **Yoyo.** With `Yoyo()` every second iteration (index 1, 3, ...) is the exact time reversal of a forward one: the clips are entered
     * latest end first (equal ends in reverse insertion order), a tween writes `Lerp(from, to, Ease(type, 1 - p))` so the same curve is
     * walked back and the last call of a clip is exactly its `from`, the `delay` of a clip sits at the end of its backward slot, a callback
     * fires once per iteration at the mirrored time (`duration - at`), and a sub-timeline is played reversed as a whole (its own repeats
     * and yoyo too).
     *
     * Move-only: a copy of the callbacks and of a sub-timeline could not share state.
     */
    class Timeline {
    public:
        Timeline() = default;
        Timeline(Timeline&&) noexcept = default;
        Timeline& operator=(Timeline&&) noexcept = default;
        ~Timeline() = default;

        Timeline(const Timeline&) = delete;
        Timeline& operator=(const Timeline&) = delete;

        /// A tween of a value of type @p T from @p from to @p to, starting when the timeline so far ends; @p onValue gets the interpolated value
        /// (a callable taking `const T&`, moved or copied in; `{}` for none). The capture must fit a `TweenApply`, which never allocates
        template <Tweenable T, TweenValueCallback<T> TOnValue = std::nullptr_t>
        Timeline& Append(T from, T to, TweenSettings settings, TOnValue&& onValue) {
            return AddTween(_end, settings, MakeTweenApply<T>(std::move(from), std::move(to), std::forward<TOnValue>(onValue)));
        }

        /// Like `Append`, but starting with the last clip added
        template <Tweenable T, TweenValueCallback<T> TOnValue = std::nullptr_t>
        Timeline& Join(T from, T to, TweenSettings settings, TOnValue&& onValue) {
            return AddTween(_lastStart, settings, MakeTweenApply<T>(std::move(from), std::move(to), std::forward<TOnValue>(onValue)));
        }

        /// Like `Append`, but starting at the absolute time @p at of the timeline (the duration grows when the clip ends later)
        template <Tweenable T, TweenValueCallback<T> TOnValue = std::nullptr_t>
        Timeline& Insert(float at, T from, T to, TweenSettings settings, TOnValue&& onValue) {
            return AddTween(at, settings, MakeTweenApply<T>(std::move(from), std::move(to), std::forward<TOnValue>(onValue)));
        }

        /// A sub-timeline (moved in) starting when the timeline so far ends. Its duration, repeats included, is the length of the clip.
        /// A parent cannot contain an endless one: it plays ONE iteration of it (the endless flag is dropped), and the clip is that long
        Timeline& Append(Timeline timeline);

        /// A sub-timeline starting with the last clip added
        Timeline& Join(Timeline timeline);

        /// A sub-timeline starting at the absolute time @p at
        Timeline& Insert(float at, Timeline timeline);

        /// A pause of @p seconds after the timeline so far: it calls nothing, it only moves the next `Append` (and the end) later
        Timeline& AppendDelay(float seconds);

        /// A callback that fires once when the time reaches the end of the timeline so far
        Timeline& AppendCallback(MoveOnlyFunction<void()> callback);

        /// A callback that fires once when the time reaches @p at
        Timeline& InsertCallback(float at, MoveOnlyFunction<void()> callback);

        /// Runs the whole timeline @p count times in a row (forward, unless `Yoyo()` turns every second one back). A @p count below 1 is 1.
        /// The last of `Repeat` and `RepeatForever` wins
        Timeline& Repeat(int count);

        /// Repeats without end (every second iteration backward with `Yoyo()`). `GetDuration()` is infinite, the timeline is never finished
        /// by itself (only a `Reset()` starts it over), and a parent plays one iteration of it. A timeline with no length is done by its first
        /// call and does not loop
        Timeline& RepeatForever() {
            _endless = true;
            return *this;
        }

        /// With a repeat, every second iteration (index 1, 3, ...) runs backwards: an exact time reversal of the forward pass (see the class
        /// comment). A single iteration is not changed by it
        Timeline& Yoyo(bool enabled = true) {
            _yoyo = enabled;
            return *this;
        }

        /**
         * @brief Applies the state of the timeline at @p time seconds since its start (the rules are in the class comment).
         *
         * @p time must never go back between two calls; `Reset()` starts over. A negative @p time counts as zero, NaN is ignored (so is a
         * time that is not finite, for an endless timeline), and a call after the timeline is finished does nothing. A @p time that spans
         * several iterations crosses every one of them: each ends with its exact final values and its callbacks.
         */
        void Evaluate(float time);

        /**
         * @brief Like `Evaluate(elapsed + dt)` for a timeline that keeps its own elapsed time: the iteration and the time inside it, so it
         * stays exact however long the run (no growing float time).
         *
         * A @p dt that is negative, NaN or infinite counts as zero. A @p dt that crosses several iterations crosses all of them. `Evaluate`
         * and `Advance` are not mixed on one run; `Reset()` clears both.
         */
        void Advance(float dt);

        /**
         * @brief Gives this timeline a predicate that says "stop": `Evaluate` asks it after every clip it visits, and when it answers true it
         * returns at once, with no further setter or callback call in that call (nor in the later ones while it stays true). `TweenManager::Play`
         * binds it to the handle, so a callback clip that cancels its own run ends the step right there.
         *
         * Only the timeline `Evaluate` is called on uses its check: it governs its sub-timelines for that call, and a check set on a timeline
         * before it was moved in as a sub is not asked. Keep the predicate cheap and let it capture nothing that dies before the timeline does.
         * An empty function (the default) never stops.
         */
        void SetStopCheck(MoveOnlyFunction<bool()> shouldStop) {
            _shouldStop = std::move(shouldStop);
        }

        /// The duration of one iteration times the repeats; zero for an empty timeline; for an endless one `+infinity`, or zero when it has
        /// no length
        [[nodiscard]] float GetDuration() const;

        /// The frame class `TweenManager::Play` gives the tween (see `AnimationFrameClass`). Not set, it is `DecorativeLoop` for an endless
        /// timeline and `FiniteEffect` otherwise; a class set here is kept whatever `RepeatForever` or `Repeat` say
        Timeline& SetFrameClass(AnimationFrameClass frameClass) {
            _frameClass = frameClass;
            return *this;
        }

        [[nodiscard]] AnimationFrameClass GetFrameClass() const {
            return _frameClass.value_or(_endless ? AnimationFrameClass::DecorativeLoop : AnimationFrameClass::FiniteEffect);
        }

        /// True after `RepeatForever()` (until a later `Repeat`)
        [[nodiscard]] bool IsEndless() const {
            return _endless;
        }

        /// True once `Evaluate` has been called with a time at or after `GetDuration()` (or the first time, for a timeline with no length);
        /// `Advance` finishes it when the time inside the last iteration reaches its end. An endless timeline is never finished
        [[nodiscard]] bool IsFinished() const {
            return _finished;
        }

        /// Makes the timeline run again from its start: the next `Evaluate` begins with every clip untouched (sub-timelines too). The clips stay
        void Reset();

    private:
        struct Clip {
            bool isCallback = false;  // a callback clip; with `sub` set it is a sub-timeline, else a tween
            float start = 0.0f;       // the start of the slot on the local time line
            float delay = 0.0f;       // the clip begins at start + delay
            float length = 0.0f;      // after the delay (a sub-timeline: its duration)
            EaseType ease = EaseType::Linear;
            TweenApply apply;                   // a tween clip: the eased progress in, the value out
            MoveOnlyFunction<void()> callback;  // a callback clip
            UniquePtr<Timeline> sub;            // a sub-timeline clip
            size_t order = 0;                   // the insertion number: equal ends are entered in reverse of it in a backward iteration
            bool done = false;                  // the final call has been made in this iteration
        };

        Timeline& AddTween(float at, const TweenSettings& settings, TweenApply apply);
        Timeline& AddSub(float at, Timeline&& timeline);
        Timeline& AddClip(float at, Clip&& clip);
        // `stop` is the check of the timeline `Evaluate` was called on, handed down to the sub-timelines for the call and never stored
        // `reversed`: the parent plays this timeline backwards as a whole (its iteration that holds it runs backward)
        void EvaluateWith(float time, const MoveOnlyFunction<bool()>& stop, bool reversed);
        // The iteration loop both entry points share: @p local is the time inside the current iteration, @p finishing says the call
        // reaches the end of the last one (decided by the caller from the duration, so rounding cannot leave a clip short)
        void Run(double local, bool finishing, const MoveOnlyFunction<bool()>& stop);
        void VisitClips(float time, const MoveOnlyFunction<bool()>& stop);
        void VisitClip(Clip& clip, float time, const MoveOnlyFunction<bool()>& stop, bool backward);
        void RestartClips();
        // Whether the current iteration runs backward: a yoyo iteration of odd index, flipped when the whole timeline is played reversed
        [[nodiscard]] bool IsBackward() const;
        [[nodiscard]] static float EndOf(const Clip& clip) {
            return (clip.start + clip.delay) + clip.length;
        }
        // The time in an iteration at which a clip is entered: the order of `_clips` forward, of `_backOrder` backward
        [[nodiscard]] float EnterTime(const Clip& clip, bool backward) const {
            return backward ? _end - EndOf(clip) : clip.start;
        }
        void BuildBackwardOrder();
        [[nodiscard]] static bool IsStopped(const MoveOnlyFunction<bool()>& stop) {
            return stop && stop();
        }

        MoveOnlyFunction<bool()> _shouldStop;  // asked after every clip; true ends the `Evaluate` in progress
        Vector<Clip> _clips;                   // in order of start, equal starts in insertion order
        Vector<size_t> _backOrder;             // indices into `_clips` in the order of a backward iteration; built at the first one
        size_t _cursor = 0;                    // positions before it (in the order in use) are done in this iteration
        size_t _inserted = 0;                  // clips added so far: the next insertion number
        float _end = 0.0f;                     // the end of the latest clip: the duration of one iteration
        float _lastStart = 0.0f;               // the start of the last clip added: where `Join` puts the next one
        // The time inside the current iteration, in double: a run of hours of small `Advance` steps adds no float rounding error to it
        double _clock = 0.0;
        int _repeat = 1;
        int _iteration = 0;
        Optional<AnimationFrameClass> _frameClass;  // empty: derived from `_endless`
        bool _endless = false;
        bool _yoyo = false;
        bool _reversed = false;  // set by the parent on every call: this timeline is played backwards as a whole
        bool _backOrderBuilt = false;
        bool _finished = false;
    };

}  // namespace BECore
