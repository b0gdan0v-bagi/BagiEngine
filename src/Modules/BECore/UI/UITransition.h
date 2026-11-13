#pragma once

#include <BECore/Animation/Easing.h>
#include <BECore/Math/Vec2.h>
#include <BECore/Renderer/ViewTransform.h>

namespace BECore {

    /// The longest a transition may take, in seconds; a duration of a file past it is cut (a number that long is a mistake, and a run that never ends would keep an element drawn)
    inline constexpr float kMaxTransitionSeconds = 60.0f;

    /**
     * @brief What a show / hide transition does and how long it takes: the data of a `UITransitionComponent` and of the transition fields of a canvas. Plain data.
     *
     * The numbers come from a file, so every reader goes through `Bounded()` (the run and `UITransition::Evaluate` do): NaN is the default, a negative duration is 0, a duration
     * is capped at `kMaxTransitionSeconds`, a slide is bounded like a position (`UIBounds`) and the scale stays inside `UIBounds`' scale range.
     */
    struct UITransitionSpec {
        bool fade = true;                    ///< the opacity follows the progress
        float scaleFrom = 1.0f;              ///< the scale of the hidden end, about the pivot; 1 is no scale
        float slideX = 0.0f;                 ///< canvas units the hidden end is moved by, to the right; 0 is none
        float slideY = 0.0f;                 ///< and down
        float duration = 0.0f;               ///< seconds of a show; 0 or less is no animation (no animation: the change shows in the same frame)
        float hideDuration = -1.0f;          ///< seconds of a hide; negative takes `duration`
        EaseType ease = EaseType::QuadOut;   ///< the curve of the progress
        bool inputDuringTransition = false;  ///< whether input reaches the element while it shows or hides

        /// A copy with every number bounded (see the struct)
        [[nodiscard]] UITransitionSpec Bounded() const;

        /// Seconds of a show / of a hide after bounding; `HideSeconds()` follows the show when `hideDuration` is negative or NaN
        [[nodiscard]] float ShowSeconds() const;
        [[nodiscard]] float HideSeconds() const;

        /// Some change takes time: a show or a hide has a duration. An inactive spec is the old pop, whatever else it says
        [[nodiscard]] bool IsActive() const {
            return ShowSeconds() > 0.0f || HideSeconds() > 0.0f;
        }
    };

    /**
     * @brief The rules of a transition. Pure: arguments in, numbers out.
     *
     * A progress is 0 hidden .. 1 shown (an overshooting ease leaves that range between, and `Evaluate` keeps what must not).
     */
    class UITransition {
    public:
        /// What a progress does to a subtree
        struct Evaluated {
            float opacity = 1.0f;  ///< 0..1, multiplies into the opacity of the subtree
            float scale = 1.0f;    ///< about the pivot, never negative
            float offsetX = 0.0f;  ///< canvas units
            float offsetY = 0.0f;
        };

        /// @p progress through @p spec (bounded here): the opacity is the progress when `fade` (clamped to 0..1), the scale goes from `scaleFrom` to 1, the slide from its full offset to 0.
        /// A NaN progress is a shown element
        [[nodiscard]] static Evaluated Evaluate(const UITransitionSpec& spec, float progress);

        /// @p view moved so that what is drawn with it is scaled by `evaluated.scale` about @p pivot (canvas units; the pivot stays where it is) and moved by the slide:
        /// `scale' = view.scale * s`, `offset' = view.offset + (pivot * (1 - s) + slide) * view.scale`
        [[nodiscard]] static ViewTransform Compose(const ViewTransform& view, Vec2 pivot, const Evaluated& evaluated);
    };

    /**
     * @brief The runtime of one element or canvas: where its transition is. Runtime data, not saved, no pointer into the scene.
     *
     * It keeps **timestamps** of game time (`CoreManager::GetTime().GetTotalTime()`), not ticks: the answer for a time is the same however often a frame is drawn, and a held frame
     * of a paused game stands still. `Sync` is the only call that changes anything but `TakeFinished`; the questions are `const`.
     *
     * A change of the target starts from the progress shown at that moment (a reversal does not jump) and takes the duration of the new direction in proportion to the distance
     * left, so reversing near the end is quick.
     */
    class UITransitionRun {
    public:
        /// Tells the run what is the target now. The first call snaps to it without an animation; a change of the target starts a run of the spec's duration (or snaps when that
        /// is 0); the same target changes nothing
        void Sync(bool visible, double now, const UITransitionSpec& spec);

        /// 0 hidden .. 1 shown, eased, at @p now. Exactly 0 or 1 once the run is over
        [[nodiscard]] float Progress(double now) const;

        /// A run is on at @p now (started and not over)
        [[nodiscard]] bool IsTransitioning(double now) const;

        /// The target is hidden and the element is not at 0 yet: it still has to be drawn
        [[nodiscard]] bool IsLeaving(double now) const;

        /// The target is shown and the element has not arrived
        [[nodiscard]] bool IsShowing(double now) const;

        /// True once for a run that ran its time out by @p now (whoever lays out again learns that a leaving element may go). A run cut by a `Sync` is not reported: that call
        /// is made by the layout itself
        [[nodiscard]] bool TakeFinished(double now);

        /// The target set by the last `Sync` (false before the first one)
        [[nodiscard]] bool TargetVisible() const {
            return _targetVisible;
        }

    private:
        bool _synced = false;
        bool _targetVisible = false;
        bool _running = false;
        float _from = 0.0f;    ///< the progress the run started from; where the element stands while no run is on
        float _length = 0.0f;  ///< seconds the run takes
        double _start = 0.0;
        EaseType _ease = EaseType::Linear;
    };

}  // namespace BECore
