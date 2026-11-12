#include "ViewportCamera.h"

namespace BECore {

    namespace {

        constexpr float kMiddle = 0.5f;

        /// A zero or negative base zoom would divide by it, so it falls back to 1
        float SafeBase(float baseZoom) {
            return baseZoom > 0.0f ? baseZoom : 1.0f;
        }

        Vec2 PanelMiddle(const Rect& panel) {
            return {panel.x + panel.w * kMiddle, panel.y + panel.h * kMiddle};
        }

        /// The centre of the target in target pixels: the panel is the unzoomed image, so the target is the panel over the base zoom
        Vec2 TargetMiddle(const Rect& panel, float baseZoom) {
            return {panel.w / baseZoom * kMiddle, panel.h / baseZoom * kMiddle};
        }

    }  // namespace

    float ViewportCamera::Scale(float baseZoom) const {
        return SafeBase(baseZoom) * zoom;
    }

    Vec2 ViewportCamera::ToScreen(Vec2 target, const Rect& panel, float baseZoom) const {
        const float base = SafeBase(baseZoom);
        const Vec2 middle = PanelMiddle(panel);
        const Vec2 targetMiddle = TargetMiddle(panel, base);
        const float scale = Scale(base);
        return {middle.x + (target.x - targetMiddle.x - centerX) * scale, middle.y + (target.y - targetMiddle.y - centerY) * scale};
    }

    Vec2 ViewportCamera::ToTarget(Vec2 screen, const Rect& panel, float baseZoom) const {
        const float base = SafeBase(baseZoom);
        const Vec2 middle = PanelMiddle(panel);
        const Vec2 targetMiddle = TargetMiddle(panel, base);
        const float scale = Scale(base);
        return {targetMiddle.x + centerX + (screen.x - middle.x) / scale, targetMiddle.y + centerY + (screen.y - middle.y) / scale};
    }

    void ViewportCamera::ZoomAt(float factor, Vec2 screen, const Rect& panel, float baseZoom) {
        const float base = SafeBase(baseZoom);
        const Vec2 anchor = ToTarget(screen, panel, base);
        zoom = std::clamp(zoom * factor, kMinZoom, kMaxZoom);
        const Vec2 middle = PanelMiddle(panel);
        const Vec2 targetMiddle = TargetMiddle(panel, base);
        const float scale = Scale(base);
        centerX = anchor.x - targetMiddle.x - (screen.x - middle.x) / scale;
        centerY = anchor.y - targetMiddle.y - (screen.y - middle.y) / scale;
    }

    void ViewportCamera::PanBy(Vec2 screenDelta, float baseZoom) {
        const float scale = Scale(baseZoom);
        centerX -= screenDelta.x / scale;
        centerY -= screenDelta.y / scale;
    }

    void ViewportCamera::FrameRect(const Rect& rect, Vec2 panelSize, float baseZoom, float margin) {
        const float base = SafeBase(baseZoom);
        const float targetWidth = panelSize.x / base;
        const float targetHeight = panelSize.y / base;
        centerX = rect.x + rect.w * kMiddle - targetWidth * kMiddle;
        centerY = rect.y + rect.h * kMiddle - targetHeight * kMiddle;

        const float roomX = panelSize.x - 2.0f * margin;
        const float roomY = panelSize.y - 2.0f * margin;
        if (!(rect.w > 0.0f) || !(rect.h > 0.0f) || !(roomX > 0.0f) || !(roomY > 0.0f)) {
            zoom = 1.0f;
            return;
        }
        const float scale = std::min(roomX / rect.w, roomY / rect.h);
        zoom = std::clamp(scale / base, kMinZoom, kMaxZoom);
    }

    void ViewportCamera::Reset() {
        *this = ViewportCamera{};
    }

    bool ViewportCamera::IsIdentity() const {
        return zoom == 1.0f && centerX == 0.0f && centerY == 0.0f;
    }

}  // namespace BECore
