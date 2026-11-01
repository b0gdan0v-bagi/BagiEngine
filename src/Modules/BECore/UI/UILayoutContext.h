#pragma once

namespace BECore {

    /**
     * @brief Everything the layout of a canvas depends on besides its own tree and settings.
     *
     * A canvas keeps its layout until this changes (operator==) or it is invalidated. Sizes are in render output
     * pixels unless a field says otherwise.
     */
    struct UILayoutContext {
        float outputWidth = 0.0f;  ///< the size of what is drawn into: the back buffer, or the editor viewport's target
        float outputHeight = 0.0f;
        float pixelDensity = 1.0f;    ///< output pixels per window unit (IMainWindow::GetPixelDensity)
        float displayScale = 1.0f;    ///< the user's display scale of the monitor (IMainWindow::GetDisplayScale)
        Rect safeArea;                ///< the part of the output that notches and system bars leave free; empty means the whole output
        float userScale = 1.0f;       ///< the accessibility UI scale (UISettings::userScale)
        float sceneViewWidth = 0.0f;  ///< the scene's view size in scene units (0: the scene has none), for SceneView and World canvases
        float sceneViewHeight = 0.0f;
        Rect worldBox;  ///< World canvases: the box of the node's TransformComponent in scene units

        [[nodiscard]] bool operator==(const UILayoutContext& other) const {
            const auto same = [](const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; };
            return outputWidth == other.outputWidth && outputHeight == other.outputHeight && pixelDensity == other.pixelDensity && displayScale == other.displayScale &&
                   same(safeArea, other.safeArea) && userScale == other.userScale && sceneViewWidth == other.sceneViewWidth && sceneViewHeight == other.sceneViewHeight &&
                   same(worldBox, other.worldBox);
        }
    };

}  // namespace BECore
