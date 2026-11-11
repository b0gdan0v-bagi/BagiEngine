#pragma once

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

    /**
     * @brief The pure part of the editor viewport's overlays: the rects it draws over the image, in render output pixels of the target. No ImGui, no scene.
     *
     * The viewport walks the canvases of the scene and hands each laid-out UICanvasLayout here; the origin of every rect is the top left of the output.
     */
    class UIPreviewOverlay {
    public:
        /// The scene's fitted view in the output (the fit that Scene::Draw() uses); the whole output when the view has no size (the identity fit)
        [[nodiscard]] static Rect ViewRect(float viewWidth, float viewHeight, float outputWidth, float outputHeight);

        /**
         * @brief Every visible interactive element with a rect of a canvas that has a layout, with the rect in output pixels.
         *
         * An element hidden by its style is skipped with its subtree, and the children of a closed popup are not walked: the walk of UILayoutValidator.
         * `tooSmall` is its touch rule: the smaller side, as the user sees it (times the canvas scale, divided by @p pixelDensity), under @p minTouchSize.
         * Empty for a canvas with no layout. The viewport pass runs at density 1, so there @p pixelDensity is 1.
         */
        [[nodiscard]] static eastl::vector<TouchMark> TouchTargets(const UICanvasLayout& canvas, float minTouchSize, float pixelDensity);

        /**
         * @brief Every region published for a canvas that has a layout (`UICanvasLayout::regions`), with the rect in output pixels, in the order of publishing.
         *
         * The rects follow the canvas transform exactly like CanvasBounds: a region partly outside the canvas is not clipped, a zero-sized one gives a zero-sized rect.
         * Empty for a canvas with no layout or no regions.
         */
        [[nodiscard]] static eastl::vector<RegionMark> RegionMarks(const UICanvasLayout& canvas);

        /// The bounds of the whole canvas in output pixels
        [[nodiscard]] static Rect CanvasBounds(const UICanvasResolved& resolved);

        /// The content bounds (where the layout puts the elements, inside the safe area when the canvas uses it) in output pixels
        [[nodiscard]] static Rect ContentBounds(const UICanvasResolved& resolved);
    };

}  // namespace BECore
