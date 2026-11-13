#pragma once

#include <BECore/UI/UIComponent.h>
#include <BECore/UI/UITransition.h>

namespace BECore {

    /**
     * @brief Shows and hides its element with a transition instead of a pop: `<component type="UITransitionComponent" duration="0.2" scaleFrom="0.9" slideY="24"/>`.
     *
     * The authored state stays the truth: `visible` of the element (after the variants) is the target and this component only delays the effect of a change, so the ways of changing it are the
     * old ones (`SetElementVisible`, a field edit and `Invalidate()`, a variant). The canvas layout `Sync`s the run on every layout; the first layout snaps (nothing animates on load). While the
     * element leaves, the layout keeps it visible, so it keeps its rect and is drawn (`UITransitionRun::IsLeaving`); the canvas lays out again when the run ends and the element goes for good.
     * The canvas draws a transitioning element and its subtree through `UITransition::Evaluate` / `Compose` (opacity, scale about the centre of its rect, slide).
     *
     * Input: with `inputDuringTransition` off the element and its subtree take no hover, press, click or tooltip while the run is on, but still consume the events that land on a hit-testable
     * element, so what lies below stays shielded (`UIElement::inputLocked`). With it on the element is as live as any other, in its laid-out rect: **the rect is not scaled or slid** for input.
     * The fields build a `UITransitionSpec` (`MakeSpec`); the component holds the run, which is runtime data and is not saved.
     */
    class UITransitionComponent : public UIComponent {
        BE_CLASS(UITransitionComponent)
    public:
        UITransitionComponent() = default;
        ~UITransitionComponent() override = default;

        BE_REFLECT_FIELD bool fade = true;  ///< the opacity follows the progress
        BE_REFLECT_FIELD float scaleFrom = 1.0f;
        BE_REFLECT_FIELD float slideX = 0.0f;
        BE_REFLECT_FIELD float slideY = 0.0f;
        BE_REFLECT_FIELD float duration = 0.0f;       ///< seconds of a show; 0: no animation (no animation: the change shows in the same frame)
        BE_REFLECT_FIELD float hideDuration = -1.0f;  ///< seconds of a hide; negative: `duration`
        BE_REFLECT_FIELD EaseType ease = EaseType::QuadOut;
        BE_REFLECT_FIELD bool inputDuringTransition = false;

        /// The fields as a spec (not bounded: the readers of the spec bound it)
        [[nodiscard]] UITransitionSpec MakeSpec() const;

        UITransitionRun run;  ///< runtime: where the transition is; synced by `UICanvasLayout`, read by the canvas when it draws
        bool held = false;    ///< runtime: the layout made the element visible again because it is leaving (it is `active.visible == false` by its authored state)
    };

}  // namespace BECore
