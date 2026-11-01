#pragma once

#include <BECore/UI/UICanvasScaler.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UILayoutValidator.h>
#include <BECore/UI/UIVariant.h>

namespace BECore {

    /**
     * @brief The layout of one canvas as pure code: scale and bounds, variants, the laid-out tree, a hit test and the validator. No scene, window or renderer.
     *
     * UICanvasComponent owns one and feeds it the context of the frame. It is separate so that the layout is built, run and validated in a test over a matrix of
     * sizes without a scene. The fields are public: change them and call Invalidate() (the settings in `params` are noticed by themselves).
     *
     * Update() works in this order: resolve the scale and bounds (UICanvasScaler), choose the variants of the bounds (UIVariants::Apply, only when
     * the bounds changed or after Invalidate()), lay the tree out in the content bounds (UITreeLayout). It does nothing when neither the context, the
     * params nor the tree changed.
     */
    class UICanvasLayout {
    public:
        UICanvasScaleParams params;
        IntrusivePtr<UIElement> root = New<UIElement>();  ///< the canvas itself: its children are the elements; its rect is the content bounds
        eastl::vector<UIVariant> variants;

        /// @return true when the layout was computed again
        bool Update(const UILayoutContext& context);

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
         * The rects of children are not clipped by their parent.
         */
        [[nodiscard]] UIElement* HitTest(float canvasX, float canvasY) const;

        [[nodiscard]] eastl::vector<UILayoutIssue> Validate(const UILayoutValidatorOptions& options) const;

    private:
        UICanvasScaleParams _params;
        UILayoutContext _context;
        UICanvasResolved _resolved;
        bool _valid = false;       // the layout in the tree is current
        bool _hasContext = false;  // an Update() has run
        Rect _variantBounds;
        bool _variantsChosen = false;
        uint32_t _variantRuns = 0;
        uint32_t _layoutRuns = 0;
    };

}  // namespace BECore
