#include "UIPreviewOverlay.h"

namespace BECore {

    namespace {

        // The tolerance and the lower limit of the density of UILayoutValidator's touch rule: the two must agree
        constexpr float kTouchTolerance = 0.01f;
        constexpr float kMinPixelDensity = 1e-3f;

        void CollectTouchTargets(const UIElement& element, const UICanvasResolved& resolved, float minTouchSize, float pixelDensity, eastl::vector<TouchMark>& marks) {
            if (!element.active.visible) {
                return;
            }
            if (element.IsInteractive() && !element.rect.IsEmpty()) {
                const float size = std::min(element.rect.w, element.rect.h) * resolved.scale / std::max(pixelDensity, kMinPixelDensity);
                marks.push_back({resolved.transform.ToOutput(element.rect.x, element.rect.y, element.rect.w, element.rect.h), element.name, size < minTouchSize - kTouchTolerance});
            }
            if (element.HasPopupChildren() && !element.IsPopupOpen()) {
                return;
            }
            for (const auto& child : element.children) {
                CollectTouchTargets(*child, resolved, minTouchSize, pixelDensity, marks);
            }
        }

    }  // namespace

    Rect UIPreviewOverlay::ViewRect(float viewWidth, float viewHeight, float outputWidth, float outputHeight) {
        if (viewWidth <= 0.0f || viewHeight <= 0.0f) {
            return {0.0f, 0.0f, outputWidth, outputHeight};
        }
        return ViewTransform::Fit(viewWidth, viewHeight, outputWidth, outputHeight).ToOutput(0.0f, 0.0f, viewWidth, viewHeight);
    }

    eastl::vector<TouchMark> UIPreviewOverlay::TouchTargets(const UICanvasLayout& canvas, float minTouchSize, float pixelDensity) {
        eastl::vector<TouchMark> marks;
        if (!canvas.HasLayout() || !canvas.root) {
            return marks;
        }
        for (const auto& child : canvas.root->children) {
            CollectTouchTargets(*child, canvas.GetResolved(), minTouchSize, pixelDensity, marks);
        }
        return marks;
    }

    eastl::vector<RegionMark> UIPreviewOverlay::RegionMarks(const UICanvasLayout& canvas) {
        eastl::vector<RegionMark> marks;
        if (!canvas.HasLayout()) {
            return marks;
        }
        const ViewTransform& transform = canvas.GetResolved().transform;
        marks.reserve(canvas.regions.Size());
        for (const auto& region : canvas.regions) {
            marks.push_back({region.name, transform.ToOutput(region.rect.x, region.rect.y, region.rect.w, region.rect.h)});
        }
        return marks;
    }

    Rect UIPreviewOverlay::CanvasBounds(const UICanvasResolved& resolved) {
        return resolved.transform.ToOutput(resolved.bounds.x, resolved.bounds.y, resolved.bounds.w, resolved.bounds.h);
    }

    Rect UIPreviewOverlay::ContentBounds(const UICanvasResolved& resolved) {
        return resolved.transform.ToOutput(resolved.contentBounds.x, resolved.contentBounds.y, resolved.contentBounds.w, resolved.contentBounds.h);
    }

}  // namespace BECore
