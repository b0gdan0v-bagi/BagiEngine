#pragma once

#include <BECore/Animation/Lerp.h>
#include <BECore/Animation/Tween.h>

namespace BECore {

    /**
     * @brief A set of clips (tweens, callbacks, sub-timelines) on one local time line, run in sequence and in parallel.
     *
     * Pure maths, no manager and no clock: the owner calls `Evaluate(time)` with the time since the start. The timeline is
     * evaluated by TIME, not started clip by clip, so the time between clips is exact (a chain of tweens started from `onComplete`
     * loses the left-over time of every link, a timeline does not). `TweenManager::Play` runs one as a tween.
     *
     * **Building.** Every clip has a start and a length on the local time line; the duration of one iteration is the latest end.
     * `Append` starts a clip when the timeline so far ends, `Join` with the last clip added (by any call), `Insert` at an absolute
     * time. The `delay` of the `TweenSettings` of a clip lengthens its slot: the clip starts after it, and the setter is not called
     * during it. A start before zero, or not finite, counts as zero; a duration or a delay that is negative or not finite counts as zero.
     * Build the timeline before the first `Evaluate`; a callback must not add clips to the timeline that calls it.
     *
     * **Evaluating.** `Evaluate(time)` takes a time that never goes back (`Reset()` starts over). The clips are visited in order of
     * start, equal starts in insertion order:
     * - a tween the time is inside gets `setter(Lerp(from, to, Ease(ease, local progress)))` on every call;
     * - a tween the time has passed (or whose length is zero) gets exactly one final `setter(to)`, never skipped, so a big step
     *   leaves every value right; with two clips on one property the later one in the order wins;
     * - a callback fires once, when the time reaches it, also when one call jumps over several;
     * - a sub-timeline is evaluated with its own local time (the time minus its start) and finishes when its own duration, repeats
     *   included, has passed;
     * - the call with `time >= GetDuration()` finishes every clip and callback; a call after that changes nothing.
     * A repeat finishes the iteration, then starts the next one (every clip runs from its `from` again) and goes on with the left-over
     * time. A timeline whose iteration has no length is done by the first call and does not loop.
     *
     * With a stop check (`SetStopCheck`) `Evaluate` ends at the first clip boundary after the check answers true: the clip that was running
     * finishes its call, no later clip (nor a later iteration, nor the rest of a sub-timeline) is visited.
     *
     * There are no cancel callbacks per clip: cancelling a timeline stops it where it is, like a tween (the cancel callback of the whole
     * run is the one of the handle). Move-only: a copy of the callbacks and of a sub-timeline could not share state.
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
        template <Tweenable T>
        Timeline& Append(T from, T to, TweenSettings settings, eastl::function<void(const std::type_identity_t<T>&)> onValue) {
            return AddTween(_end, settings, EraseSetter(std::move(from), std::move(to), std::move(onValue)));
        }

        /// Like `Append`, but starting with the last clip added
        template <Tweenable T>
        Timeline& Join(T from, T to, TweenSettings settings, eastl::function<void(const std::type_identity_t<T>&)> onValue) {
            return AddTween(_lastStart, settings, EraseSetter(std::move(from), std::move(to), std::move(onValue)));
        }

        /// Like `Append`, but starting at the absolute time @p at of the timeline (the duration grows when the clip ends later)
        template <Tweenable T>
        Timeline& Insert(float at, T from, T to, TweenSettings settings, eastl::function<void(const std::type_identity_t<T>&)> onValue) {
            return AddTween(at, settings, EraseSetter(std::move(from), std::move(to), std::move(onValue)));
        }

        /// A sub-timeline (moved in) starting when the timeline so far ends. Its duration, repeats included, is the length of the clip
        Timeline& Append(Timeline timeline);

        /// A sub-timeline starting with the last clip added
        Timeline& Join(Timeline timeline);

        /// A sub-timeline starting at the absolute time @p at
        Timeline& Insert(float at, Timeline timeline);

        /// A pause of @p seconds after the timeline so far: it calls nothing, it only moves the next `Append` (and the end) later
        Timeline& AppendDelay(float seconds);

        /// A callback that fires once when the time reaches the end of the timeline so far
        Timeline& AppendCallback(eastl::function<void()> callback);

        /// A callback that fires once when the time reaches @p at
        Timeline& InsertCallback(float at, eastl::function<void()> callback);

        /// Runs the whole timeline @p count times in a row (forward, finite). A @p count below 1 is 1. `GetDuration()` includes the repeats
        Timeline& Repeat(int count);

        /**
         * @brief Applies the state of the timeline at @p time seconds since its start (the rules are in the class comment).
         *
         * @p time must never go back between two calls; `Reset()` starts over. A negative @p time counts as zero, NaN is ignored, and a call
         * after the timeline is finished does nothing.
         */
        void Evaluate(float time);

        /**
         * @brief Gives the timeline (and every sub-timeline it holds now) a predicate that says "stop": `Evaluate` asks it after every clip
         * it visits, and when it answers true it returns at once, with no further setter or callback call in that call (nor in the later ones
         * while it stays true). `TweenManager::Play` binds it to the handle, so a callback clip that cancels its own run ends the step right there.
         *
         * Set it after the last clip is added (a sub-timeline added later does not get it). The predicate is copied into every sub-timeline once,
         * so keep it cheap and let it capture nothing that dies before the timeline does. An empty function (the default) never stops.
         */
        void SetStopCheck(eastl::function<bool()> shouldStop);

        /// The duration of one iteration times the repeats; zero for an empty timeline
        [[nodiscard]] float GetDuration() const;

        /// True once `Evaluate` has been called with a time at or after `GetDuration()` (or the first time, for a timeline with no length)
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
            eastl::function<void(float)> apply;  // a tween clip: the eased progress in, the value out
            eastl::function<void()> callback;    // a callback clip
            UniquePtr<Timeline> sub;             // a sub-timeline clip
            bool done = false;                   // the final call has been made in this iteration
        };

        // The setter of a typed clip, erased exactly as `TweenManager::Start` does it: no code per value type outside this wrapper
        template <Tweenable T>
        static eastl::function<void(float)> EraseSetter(T from, T to, eastl::function<void(const std::type_identity_t<T>&)> onValue) {
            return [from = std::move(from), to = std::move(to), onValue = std::move(onValue)](float easedT) {
                if (onValue) {
                    onValue(Lerp(from, to, easedT));
                }
            };
        }

        Timeline& AddTween(float at, const TweenSettings& settings, eastl::function<void(float)> apply);
        Timeline& AddSub(float at, Timeline&& timeline);
        Timeline& AddClip(float at, Clip&& clip);
        void VisitClips(float time);
        void VisitClip(Clip& clip, float time);
        void RestartClips();
        [[nodiscard]] bool IsStopped() const {
            return _shouldStop && _shouldStop();
        }

        eastl::function<bool()> _shouldStop;  // asked after every clip; true ends the `Evaluate` in progress
        Vector<Clip> _clips;                  // in order of start, equal starts in insertion order
        size_t _cursor = 0;                   // clips before it are done in this iteration
        float _end = 0.0f;                    // the end of the latest clip: the duration of one iteration
        float _lastStart = 0.0f;              // the start of the last clip added: where `Join` puts the next one
        int _repeat = 1;
        int _iteration = 0;
        bool _finished = false;
    };

}  // namespace BECore
