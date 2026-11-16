#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/Math/Vec2.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UICanvasLayout.h>

namespace BECore {

    /// One interactive element of a canvas as the touch overlay of the viewport shows it
    struct TouchMark {
        Rect rect;  ///< in render output pixels
        PoolString name;
        bool tooSmall = false;  ///< under the minimum touch size, by the rule of UILayoutValidator (TouchTargetTooSmall)
    };

    /// One region a game published for a canvas as the regions overlay of the viewport shows it
    struct RegionMark {
        PoolString name;
        Rect rect;  ///< in render output pixels
    };

    /// One visible element of a canvas as the element rects overlay of the viewport shows it
    struct ElementMark {
        Rect rect;  ///< in render output pixels
        PoolString name;
        uint32_t depth = 0;                  ///< 0 for a child of the canvas root
        const UIElement* element = nullptr;  ///< a pointer into the tree: valid until the tree is edited; compare it, never keep it
    };

    /// The anchors an element hangs on, as the anchors overlay of the viewport shows them
    struct AnchorMark {
        Rect box;      ///< the box the anchors are fractions of, in render output pixels
        Rect anchors;  ///< the rect between `anchorMin` and `anchorMax` inside the box; zero-sized on an axis where they are equal (a point)
        Vec2 pivot;    ///< the pivot point of the element's rect, in render output pixels
    };

    /// One finding of UILayoutValidator as the findings overlay of the viewport shows it
    struct FindingMark {
        UILayoutIssueKind kind = UILayoutIssueKind::OutsideCanvas;
        PoolString element;
        PoolString other;                   ///< the sibling or the region name (UILayoutIssue::other)
        Rect rect;                          ///< the rect of `element`, in render output pixels; an OutsideCanvas rect is not clipped
        const UIElement* source = nullptr;  ///< UILayoutIssue::source: compare it, never keep it past an edit of the tree
    };

    /**
     * @brief The pure part of the editor viewport's overlays: the rects it draws over the image, in render output pixels of the target, from the top left of the output. No ImGui, no scene.
     */
    class UIPreviewOverlay {
    public:
        /// The scene's fitted view in the output (the fit that Scene::Draw() uses); the whole output when the view has no size (the identity fit)
        [[nodiscard]] static Rect ViewRect(float viewWidth, float viewHeight, float outputWidth, float outputHeight);

        /**
         * @brief Every visible interactive element with a rect of a canvas that has a layout, in output pixels.
         *
         * Walks like UILayoutValidator (a hidden element skips its subtree, the children of a closed popup are not walked). `tooSmall` is its touch rule: the smaller side as the user sees it (times
         * the canvas scale, divided by @p pixelDensity) under @p minTouchSize.
         */
        [[nodiscard]] static Vector<TouchMark> TouchTargets(const UICanvasLayout& canvas, float minTouchSize, float pixelDensity);

        /**
         * @brief Every region published for a canvas that has a layout (`UICanvasLayout::regions`), in output pixels, in the order of publishing.
         *
         * The rects follow the canvas transform like CanvasBounds: a region partly outside the canvas is not clipped, a zero-sized one gives a zero-sized rect.
         */
        [[nodiscard]] static Vector<RegionMark> RegionMarks(const UICanvasLayout& canvas);

        /**
         * @brief Every visible element with a non-empty rect of a canvas that has a layout, in draw order, in output pixels. The canvas root is not listed.
         *
         * The same walk as TouchTargets(): a hidden element skips its subtree, the children of a closed popup are not walked. @p marks is cleared first and keeps its capacity.
         */
        static void ElementMarks(const UICanvasLayout& canvas, Vector<ElementMark>& marks);
        [[nodiscard]] static Vector<ElementMark> ElementMarks(const UICanvasLayout& canvas);

        /**
         * @brief The anchors of @p element in output pixels, or none when it has none to show.
         *
         * An element has anchors when it is visible and its parent lays children out by anchors: not a stack, not an element with popup children (a popup places its children itself), and
         * @p element is in the tree of @p canvas. The box is what UITreeLayout lays the element out in: the region it names when the canvas published it, else the parent's rect (for a child of the
         * root with `ignoreSafeArea`, the whole canvas `bounds`). The pivot is `rect.x + pivot.x * rect.w`, with the pivot of UILayout::EffectiveAnchors.
         */
        [[nodiscard]] static Optional<AnchorMark> AnchorsOf(const UICanvasLayout& canvas, const UIElement& element);

        /**
         * @brief The design rectangle of the canvas scaler in output pixels: `referenceWidth` x `referenceHeight` centred in the canvas `bounds`. None unless the canvas has a layout, is a
         *        Screen canvas and uses `ScaleWithScreen`.
         *
         * With `match` 0 its width equals the canvas width, with `match` 1 its height equals the canvas height; between them it sticks out on one axis or the other.
         */
        [[nodiscard]] static Optional<Rect> DesignRect(const UICanvasLayout& canvas);

        /**
         * @brief One FindingMark per issue of @p issues (the result of validating @p canvas), in the order of the validator, with the rect in output pixels.
         *
         * @p marks is cleared first and keeps its capacity.
         */
        static void FindingMarks(const UICanvasLayout& canvas, Span<const UILayoutIssue> issues, Vector<FindingMark>& marks);
        [[nodiscard]] static Vector<FindingMark> FindingMarks(const UICanvasLayout& canvas, Span<const UILayoutIssue> issues);

        /**
         * @brief The one-line text of a finding for a list row: `<kind>: <element>[ / <other>] (<w> x <h>)`, the size in canvas units, e.g. `TouchTargetTooSmall: Button (48 x 24)`.
         *
         * @p text is cleared first and keeps its capacity.
         */
        static void FindingText(const UILayoutIssue& issue, String& text);

        /// The bounds of the whole canvas in output pixels
        [[nodiscard]] static Rect CanvasBounds(const UICanvasResolved& resolved);

        /// The content bounds (where the layout puts the elements, inside the safe area when the canvas uses it) in output pixels
        [[nodiscard]] static Rect ContentBounds(const UICanvasResolved& resolved);
    };

    /**
     * @brief The validator's issues of one canvas, kept until something that they depend on changes, so a steady frame validates nothing.
     *
     * Keyed by the canvas (its address), `GetLayoutRuns()` (every re-layout: a new context, an Invalidate() followed by an Update(), a change of regions) and the minimum touch size. Validates at
     * density 1, the density of the viewport's pass. A canvas that is destroyed is followed by Reset() (or a different canvas takes the address with the same count of layouts).
     */
    class UIFindingsCache {
    public:
        /// The issues of @p canvas for @p minTouchSize; empty for a canvas that has no layout yet. The reference is valid until the next call.
        [[nodiscard]] const Vector<UILayoutIssue>& Get(const UICanvasLayout& canvas, float minTouchSize);

        /// Forgets the issues, so the next Get() validates
        void Reset();

        /// How many times Get() validated: tests read it to show that a steady canvas pays nothing
        [[nodiscard]] uint32_t GetValidationRuns() const {
            return _validationRuns;
        }

    private:
        const UICanvasLayout* _canvas = nullptr;
        uint32_t _layoutRuns = 0;
        float _minTouchSize = 0.0f;
        bool _hasIssues = false;
        uint32_t _validationRuns = 0;
        Vector<UILayoutIssue> _issues;
    };

}  // namespace BECore
