#pragma once

#include <BECore/UI/UICanvasScaler.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UILayoutValidator.h>
#include <BECore/UI/UIRegions.h>
#include <BECore/UI/UITransitionComponent.h>
#include <BECore/UI/UIVariant.h>

namespace BECore {

    /**
     * @brief The layout of one canvas as pure code: scale and bounds, variants, the laid-out tree, a hit test and the validator. No scene, window or renderer.
     *
     * UICanvasComponent owns one and feeds it the context of the frame. It is separate so that the layout is built, run and validated in a test over a matrix of
     * sizes without a scene. The fields are public: change them and call Invalidate() (the settings in `params` are noticed by themselves).
     *
     * Update() works in this order: resolve the scale and bounds (UICanvasScaler), choose the variants of the bounds (UIVariants::Apply, only when
     * the bounds changed or after Invalidate()), lay the tree out in the content bounds (UITreeLayout), an element that names a published region in that
     * region. It does nothing when neither the context, the params, the `regions` nor the tree changed. A change of `regions` lays the tree out again and
     * does not choose the variants again: they depend on the bounds only.
     *
     * **Transitions.** After the variants (and on every layout) a pass `Sync`s the run of each `UITransitionComponent` with the element's authored `active.visible`, makes a leaving element visible
     * again so it keeps its rect and is drawn, and sets `UIElement::inputLocked` on the subtree of a transitioning element whose input is off. The authored state is the target; nothing else
     * moves it.
     */
    class UICanvasLayout {
    public:
        UICanvasScaleParams params;
        IntrusivePtr<UIElement> root = New<UIElement>();  ///< the canvas itself: its children are the elements; its rect is the content bounds
        Vector<UIVariant> variants;
        UIRegionSet regions;  ///< the rectangles the game published, in canvas units; noticed by Update() by themselves, like `params`
        double time = 0.0;    ///< game time of this frame in seconds, for the transitions of the elements; the owner sets it before Update() (it is no part of what makes Update() lay out again)

        /// @return true when the layout was computed again
        bool Update(const UILayoutContext& context);

        /// Asks the transition runs of the elements (`UITransitionComponent`) whether one ended at @p now; then the layout is invalidated, so the next Update() lets a hidden element go (its rect
        /// is cleared) and unlocks the input of a shown one. A steady canvas pays a loop over the elements that have the component. @return true when a run ended
        bool AdvanceTransitions(double now);

        /// The tree or the variants were edited: the next Update() chooses the variants and lays out again
        void Invalidate() {
            _valid = false;
        }

        /// False until the first Update()
        [[nodiscard]] bool HasLayout() const {
            return _hasContext;
        }

        [[nodiscard]] const UICanvasResolved& GetResolved() const {
            return _resolved;
        }

        [[nodiscard]] const UILayoutContext& GetContext() const {
            return _context;
        }

        /// How many times the variants were chosen and how many times the tree was laid out: tests read them to show that a stable canvas does neither
        [[nodiscard]] uint32_t GetVariantRuns() const {
            return _variantRuns;
        }
        [[nodiscard]] uint32_t GetLayoutRuns() const {
            return _layoutRuns;
        }

        /// A point in render output pixels as a point of the canvas, using the layout of the last Update()
        [[nodiscard]] std::pair<float, float> OutputToCanvas(float outputX, float outputY) const;

        /**
         * @brief The topmost hit-testable element at a point of the canvas, or null.
         *
         * Topmost is the one drawn last: a child before its parent, a later sibling before an earlier one. Hidden elements are not found.
         * The rects of children are not clipped by their parent. The children of an open popup (`UIElement::HasPopupChildren`) are tested first, as they are drawn last; the normal pass
         * never goes into the children of a popup element.
         */
        [[nodiscard]] UIElement* HitTest(float canvasX, float canvasY) const;

        /**
         * @brief The topmost visible element at a point of the canvas that an editor can pick, or null: the root is never one.
         *
         * The same walk and order as HitTest(), but an element counts when it is hit-testable **or draws something** (a `color` that is not transparent, or a non-empty `components`),
         * so a plain label or a colored panel is found although it takes no input. HitTest() and the game's input are not changed by it.
         */
        [[nodiscard]] UIElement* PickElement(float canvasX, float canvasY) const;

        /// The elements with `HasPopupChildren()` that are open, in tree order; only visible ones, and a popup inside the children of another is not looked for
        [[nodiscard]] Vector<UIElement*> OpenPopups() const;

        [[nodiscard]] Vector<UILayoutIssue> Validate(const UILayoutValidatorOptions& options) const;

    private:
        UICanvasScaleParams _params;
        UIRegionSet _regions;
        UILayoutContext _context;
        UICanvasResolved _resolved;
        bool _valid = false;       // the layout in the tree is current
        bool _hasContext = false;  // an Update() has run
        Rect _variantBounds;
        bool _variantsChosen = false;
        uint32_t _variantRuns = 0;
        uint32_t _layoutRuns = 0;
        Vector<IntrusivePtr<UITransitionComponent>> _transitions;  ///< the components found by the last layout, in tree order; kept so a steady frame allocates nothing
    };

}  // namespace BECore
