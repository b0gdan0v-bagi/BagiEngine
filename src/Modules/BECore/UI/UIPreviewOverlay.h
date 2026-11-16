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

        /// The bounds of the whole canvas in output pixels
        [[nodiscard]] static Rect CanvasBounds(const UICanvasResolved& resolved);

        /// The content bounds (where the layout puts the elements, inside the safe area when the canvas uses it) in output pixels
        [[nodiscard]] static Rect ContentBounds(const UICanvasResolved& resolved);
    };

}  // namespace BECore
