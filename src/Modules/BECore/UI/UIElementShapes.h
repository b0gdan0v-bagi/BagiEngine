#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UILayoutContext.h>
#include <BECore/UI/UIPreview.h>
#include <BECore/UI/UIPreviewOverlay.h>
#include <BECore/UI/UITransition.h>
#include <BECore/UI/UITransitionComponent.h>
#include <cstdint>

namespace BECore {

    /// Where one element lies in each of the shapes asked for
    struct UIElementShapesResult {
        Vector<Optional<Rect>> rects;  ///< one per context, in that context's output pixels; none where the element is hidden, has no area or the canvas is not probed
        bool approximate = false;      ///< the element, or an ancestor, names a region the canvas published: the game published the regions for the live shape, not for these
    };

    /// What Collect() needs to put the live canvas back, kept by the caller so a probe that repeats allocates nothing
    struct UIElementShapesScratch {
        struct SavedTransition {
            UITransitionComponent* component = nullptr;  ///< into the tree of the canvas: valid for the one call that fills and empties the list
            UITransitionRun run;
            bool held = false;
        };

        Vector<SavedTransition> transitions;
        Vector<ElementMark> marks;
    };

    /**
     * @brief The same canvas element laid out for other devices: the pure part of the viewport's "other shapes". No ImGui, no scene.
     *
     * A canvas has one layout at a time, so the live canvas is laid out for each shape in turn and put back (Collect()). The probe is what `ValidateOnMatrix` of the tests does, with the
     * state of the live canvas kept: the layout context (`GetContext()`), the rects and `active` of every element (laid out again for it), and the run of each `UITransitionComponent`
     * (a shape that chooses another visibility must not start a transition of the live element).
     */
    class UIElementShapes {
    public:
        /**
         * @brief The layout context of @p device: its logical size in points (turned for @p landscape), `pixelDensity` 1, the safe area of `SafeAreaSimulation::Device`, and the
         *        `displayScale`, `userScale`, scene view and `worldBox` of @p live.
         *
         * Output pixels of the context are the logical points of the device, so a result of Collect() is in the units a phone's own layout works in.
         */
        [[nodiscard]] static UILayoutContext ContextFor(const UILayoutContext& live, const PreviewDevice& device, bool landscape);

        /**
         * @brief Lays @p canvas out for each of @p contexts, reads where @p element is in each and gives the canvas back as it was.
         *
         * Only a Screen canvas that has a layout is probed, and only for an element that is in its tree; for anything else every rect is none and the canvas is not touched. The
         * variants are chosen again for every shape (their conditions read the shape's bounds); a transition does not animate and no element of a probed shape is "leaving". The regions are
         * the ones of the live canvas, which is why an element under a region is `approximate`.
         *
         * Afterwards the layout is the one of the live context again (an Invalidate() and an Update() of it), so `GetLayoutRuns()` has moved by the shapes and one more.
         */
        static void Collect(UICanvasLayout& canvas, const UIElement& element, Span<const UILayoutContext> contexts, UIElementShapesResult& result, UIElementShapesScratch& scratch);

        /// The same with a scratch of its own
        static void Collect(UICanvasLayout& canvas, const UIElement& element, Span<const UILayoutContext> contexts, UIElementShapesResult& result);

        /// @p rect, in the output pixels of @p shape, as the same fractions of an image @p imageWidth x @p imageHeight: where the viewport draws it. None when the shape or the image has no area.
        [[nodiscard]] static Optional<Rect> FitToImage(const Rect& rect, const UILayoutContext& shape, float imageWidth, float imageHeight);
    };

    /**
     * @brief The result of Collect() for one element on one canvas, kept until something it depends on changes, so a steady frame probes nothing and allocates nothing.
     *
     * Keyed by the canvas and the element (their addresses), the shapes (the contexts, compared as values) and `GetLayoutRuns()` as the probe itself left it: an edit, a changed live context
     * or a change of regions lays the live canvas out and so asks for a new probe. The probe moves `GetLayoutRuns()`, so a cache keyed by it (`UIFindingsCache`) validates once after it.
     * A canvas or element that is destroyed is followed by Reset() (or another takes the address with the same count of layouts).
     */
    class UIElementShapesCache {
    public:
        /// The rects of @p element in @p contexts; empty (and nothing probed) for no contexts. The reference is valid until the next call.
        [[nodiscard]] const UIElementShapesResult& Get(UICanvasLayout& canvas, const UIElement& element, Span<const UILayoutContext> contexts);

        /// Forgets the result, so the next Get() probes
        void Reset();

        /// How many times Get() probed: tests read it to show that a steady canvas pays nothing
        [[nodiscard]] uint32_t GetProbeRuns() const {
            return _probeRuns;
        }

    private:
        const UICanvasLayout* _canvas = nullptr;
        const UIElement* _element = nullptr;
        uint32_t _layoutRuns = 0;
        bool _hasResult = false;
        uint32_t _probeRuns = 0;
        Vector<UILayoutContext> _contexts;
        UIElementShapesResult _result;
        UIElementShapesScratch _scratch;
    };

}  // namespace BECore
