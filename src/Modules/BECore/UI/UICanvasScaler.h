#pragma once

#include <BECore/Renderer/ViewTransform.h>
#include <BECore/UI/UILayoutContext.h>

namespace BECore {

    /// What a canvas fills. Screen: the render output, so its edges are the window's edges (HUD, menus). SceneView: the scene's fitted view, so it
    /// scales and moves with the playfield. World: the box of the node's TransformComponent, in scene units, so it follows the node.
    CORE_ENUM(CanvasSpace, uint8_t, Screen, SceneView, World)

    /// How a Screen canvas turns the output size into a scale. ConstantPixelSize: a UI unit is a window unit (a CSS pixel), so the scale is the pixel
    /// density. ConstantPhysicalSize: the same, times the display scale the user set. ScaleWithScreen: the scale follows the output size against the reference size.
    CORE_ENUM(CanvasScaleMode, uint8_t, ConstantPixelSize, ScaleWithScreen, ConstantPhysicalSize)

    struct UICanvasScaleParams {
        CanvasSpace space = CanvasSpace::Screen;
        CanvasScaleMode scaleMode = CanvasScaleMode::ScaleWithScreen;
        float referenceWidth = 1280.0f;
        float referenceHeight = 720.0f;
        float match = 0.5f;      ///< ScaleWithScreen: 0 follows the width, 1 the height, between them the geometric blend (Unity's)
        float minScale = 0.25f;  ///< 0 or less: no lower limit
        float maxScale = 8.0f;   ///< 0 or less: no upper limit
        bool useSafeArea = false;

        [[nodiscard]] bool operator==(const UICanvasScaleParams&) const = default;
    };

    /// Where a canvas is, in the units it works in
    struct UICanvasResolved {
        float scale = 1.0f;       ///< output pixels per UI unit
        Rect bounds;              ///< the whole canvas, in UI units, origin at 0, 0
        Rect safeBounds;          ///< the safe area in UI units, inside bounds
        Rect contentBounds;       ///< safeBounds when the canvas uses the safe area, else bounds: where the layout puts its elements
        ViewTransform transform;  ///< UI units to output pixels; the canvas sets it around its draw
    };

    /**
     * @brief The one pure function that turns a canvas's settings and a layout context into its scale, bounds and transform.
     *
     * Screen: scale = mode scale x user scale, kept between minScale and maxScale. bounds = output / scale.
     * SceneView and World: UI units are scene units and the scale is the fit of the scene's view to the output (the same
     * ViewTransform::Fit that Scene::Draw() uses); scaleMode, the user scale and the limits do not apply, because the board the UI belongs to
     * is scaled by that fit. A scene without a view size has the identity fit, so a SceneView canvas is the output then. SceneView bounds
     * are the view; World bounds are the node's box and its origin is the node's position. The safe area applies to Screen and SceneView only.
     */
    class UICanvasScaler {
    public:
        [[nodiscard]] static UICanvasResolved Resolve(const UICanvasScaleParams& params, const UILayoutContext& context);

        /// The scale of a Screen canvas before the user scale and the limits
        [[nodiscard]] static float ModeScale(const UICanvasScaleParams& params, const UILayoutContext& context);
    };

}  // namespace BECore
