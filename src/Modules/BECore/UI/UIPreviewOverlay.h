#pragma once

#include <BECore/Math/Rect.h>
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

        /// The bounds of the whole canvas in output pixels
        [[nodiscard]] static Rect CanvasBounds(const UICanvasResolved& resolved);

        /// The content bounds (where the layout puts the elements, inside the safe area when the canvas uses it) in output pixels
        [[nodiscard]] static Rect ContentBounds(const UICanvasResolved& resolved);
    };

}  // namespace BECore
