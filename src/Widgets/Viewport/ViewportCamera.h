#pragma once

#include <BECore/Math/Vec2.h>

namespace BECore {

    /**
     * @brief The editor camera of the viewport: a zoom and a pan over the rendered image.
     *
     * The viewport draws its render target at a fit of its own (the "base zoom": screen units per target pixel, see PreviewTarget) inside the "panel", the screen rect of that
     * unzoomed image. The camera sits on top: the target is shown at baseZoom * zoom, and (centerX, centerY) is the point of the target at the middle of the panel, in target
     * pixels counted from the target's own centre, so the default camera shows the image exactly as the fit does and needs no target size to say so. The panel is also the clip rect.
     */
    struct ViewportCamera {
        static constexpr float kMinZoom = 0.1f;
        static constexpr float kMaxZoom = 32.0f;
        static constexpr float kWheelZoomStep = 1.1f;  ///< the factor of one notch of the wheel

        float zoom = 1.0f;     ///< a factor on the base zoom, kMinZoom to kMaxZoom
        float centerX = 0.0f;  ///< the point of the target at the middle of the panel, target pixels from the target's centre
        float centerY = 0.0f;

        /// Where @p target (target pixels) lands on the screen
        [[nodiscard]] Vec2 ToScreen(Vec2 target, const Rect& panel, float baseZoom) const;

        /// The point of the target (target pixels) under @p screen
        [[nodiscard]] Vec2 ToTarget(Vec2 screen, const Rect& panel, float baseZoom) const;

        /// The factor of a wheel turn of @p notches (positive zooms in, fractions of a notch from a trackpad scale smoothly)
        [[nodiscard]] static float WheelFactor(float notches);

        /// The size of one target pixel on the screen
        [[nodiscard]] float Scale(float baseZoom) const;

        /// Multiplies the zoom by @p factor (clamped) and keeps the point of the target under @p screen under it
        void ZoomAt(float factor, Vec2 screen, const Rect& panel, float baseZoom);

        /// Moves the image with the pointer by @p screenDelta (screen units)
        void PanBy(Vec2 screenDelta, float baseZoom);

        /**
         * @brief Centres the camera on @p rect (target pixels) and takes the largest zoom that shows it whole with @p margin screen units around it, clamped.
         * A rect with no area gives zoom 1 centred on it.
         * @param panelSize the size of the panel on the screen
         */
        void FrameRect(const Rect& rect, Vec2 panelSize, float baseZoom, float margin);

        void Reset();

        [[nodiscard]] bool IsIdentity() const;
    };

}  // namespace BECore
