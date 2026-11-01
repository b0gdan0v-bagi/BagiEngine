#include "UICanvasScaler.h"

#include <cmath>

namespace BECore {

    namespace {

        Rect Intersect(const Rect& a, const Rect& b) {
            const float left = std::max(a.x, b.x);
            const float top = std::max(a.y, b.y);
            const float right = std::min(a.x + a.w, b.x + b.w);
            const float bottom = std::min(a.y + a.h, b.y + b.h);
            return {left, top, std::max(right - left, 0.0f), std::max(bottom - top, 0.0f)};
        }

        // The safe area of the context in output pixels: empty means the whole output
        Rect SafeAreaInOutput(const UILayoutContext& context) {
            const Rect output{0.0f, 0.0f, context.outputWidth, context.outputHeight};
            return context.safeArea.IsEmpty() ? output : Intersect(context.safeArea, output);
        }

        Rect ToUnits(const Rect& outputRect, const ViewTransform& transform) {
            const float left = transform.ToViewX(outputRect.x);
            const float top = transform.ToViewY(outputRect.y);
            return {left, top, outputRect.w / transform.scale, outputRect.h / transform.scale};
        }

        void Finish(UICanvasResolved& resolved, const UICanvasScaleParams& params) {
            resolved.contentBounds = params.useSafeArea ? resolved.safeBounds : resolved.bounds;
        }

        UICanvasResolved ResolveInScene(const UICanvasScaleParams& params, const UILayoutContext& context) {
            const ViewTransform fit = ViewTransform::Fit(context.sceneViewWidth, context.sceneViewHeight, context.outputWidth, context.outputHeight);

            UICanvasResolved resolved;
            resolved.scale = fit.scale;
            if (params.space == CanvasSpace::World) {
                const Rect& box = context.worldBox;
                resolved.transform = {fit.scale, fit.offsetX + box.x * fit.scale, fit.offsetY + box.y * fit.scale};
                resolved.bounds = {0.0f, 0.0f, std::max(box.w, 0.0f), std::max(box.h, 0.0f)};
                resolved.safeBounds = resolved.bounds;
            } else {
                resolved.transform = fit;
                const bool hasView = context.sceneViewWidth > 0.0f && context.sceneViewHeight > 0.0f;
                resolved.bounds = hasView ? Rect{0.0f, 0.0f, context.sceneViewWidth, context.sceneViewHeight} : Rect{0.0f, 0.0f, context.outputWidth, context.outputHeight};
                resolved.safeBounds = Intersect(ToUnits(SafeAreaInOutput(context), fit), resolved.bounds);
            }
            Finish(resolved, params);
            return resolved;
        }

    }  // namespace

    float UICanvasScaler::ModeScale(const UICanvasScaleParams& params, const UILayoutContext& context) {
        switch (params.scaleMode) {
            case CanvasScaleMode::ConstantPixelSize:
                return context.pixelDensity;
            case CanvasScaleMode::ConstantPhysicalSize:
                return context.pixelDensity * context.displayScale;
            case CanvasScaleMode::ScaleWithScreen: {
                if (params.referenceWidth <= 0.0f || params.referenceHeight <= 0.0f || context.outputWidth <= 0.0f || context.outputHeight <= 0.0f) {
                    return 1.0f;
                }
                const float logWidth = std::log2(context.outputWidth / params.referenceWidth);
                const float logHeight = std::log2(context.outputHeight / params.referenceHeight);
                const float match = std::clamp(params.match, 0.0f, 1.0f);
                return std::exp2(logWidth + (logHeight - logWidth) * match);
            }
        }
        return 1.0f;
    }

    UICanvasResolved UICanvasScaler::Resolve(const UICanvasScaleParams& params, const UILayoutContext& context) {
        if (params.space != CanvasSpace::Screen) {
            return ResolveInScene(params, context);
        }

        float scale = ModeScale(params, context) * context.userScale;
        if (params.maxScale > 0.0f) {
            scale = std::min(scale, params.maxScale);
        }
        if (params.minScale > 0.0f) {
            scale = std::max(scale, params.minScale);
        }
        if (!(scale > 0.0f)) {  // also catches NaN
            scale = 1.0f;
        }

        UICanvasResolved resolved;
        resolved.scale = scale;
        resolved.transform = {scale, 0.0f, 0.0f};
        resolved.bounds = {0.0f, 0.0f, context.outputWidth / scale, context.outputHeight / scale};
        resolved.safeBounds = Intersect(ToUnits(SafeAreaInOutput(context), resolved.transform), resolved.bounds);
        Finish(resolved, params);
        return resolved;
    }

}  // namespace BECore
