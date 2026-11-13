#include "UICanvasScaler.h"

#include <BECore/UI/UIGeometryBounds.h>
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
            return UIBounds::Bound(Rect{left, top, outputRect.w / transform.scale, outputRect.h / transform.scale});
        }

        void Finish(UICanvasResolved& resolved, const UICanvasScaleParams& params) {
            resolved.contentBounds = params.useSafeArea ? resolved.safeBounds : resolved.bounds;
        }

        /// The numbers of the context bounded (a size of a window is not NaN or 1e38 in a real run; the editor's custom size and a test are what hand them in)
        UILayoutContext BoundContext(const UILayoutContext& given) {
            UILayoutContext context = given;
            context.outputWidth = std::max(UIBounds::Bound(given.outputWidth), 0.0f);
            context.outputHeight = std::max(UIBounds::Bound(given.outputHeight), 0.0f);
            context.safeArea = UIBounds::Bound(given.safeArea);
            context.sceneViewWidth = UIBounds::Bound(given.sceneViewWidth);
            context.sceneViewHeight = UIBounds::Bound(given.sceneViewHeight);
            context.worldBox = UIBounds::Bound(given.worldBox);
            return context;
        }

        UICanvasResolved ResolveInScene(const UICanvasScaleParams& params, const UILayoutContext& context) {
            ViewTransform fit = ViewTransform::Fit(context.sceneViewWidth, context.sceneViewHeight, context.outputWidth, context.outputHeight);
            // A view a million times smaller than its output (or the reverse) is no view: the fit of a real scene is within a few orders of 1
            if (!(fit.scale >= UIBounds::kMinScale && fit.scale <= UIBounds::kMaxScale)) {
                fit = ViewTransform{};
            }

            UICanvasResolved resolved;
            resolved.scale = fit.scale;
            if (params.space == CanvasSpace::World) {
                const Rect& box = context.worldBox;
                resolved.transform = {fit.scale, fit.offsetX + box.x * fit.scale, fit.offsetY + box.y * fit.scale};
                resolved.bounds = UIBounds::Bound(Rect{0.0f, 0.0f, std::max(box.w, 0.0f), std::max(box.h, 0.0f)});
                resolved.safeBounds = resolved.bounds;
                resolved.viewBounds = resolved.bounds;
            } else {
                resolved.transform = fit;
                const bool hasView = context.sceneViewWidth > 0.0f && context.sceneViewHeight > 0.0f;
                // Without a view the fit is the identity, so the output in units is the output itself at the origin
                const Rect outputUnits = ToUnits({0.0f, 0.0f, context.outputWidth, context.outputHeight}, fit);
                const Rect view = hasView ? Rect{0.0f, 0.0f, context.sceneViewWidth, context.sceneViewHeight} : outputUnits;
                resolved.bounds = params.extendToOutput ? outputUnits : view;
                resolved.viewBounds = params.extendToOutput ? view : resolved.bounds;
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

    UICanvasResolved UICanvasScaler::Resolve(const UICanvasScaleParams& params, const UILayoutContext& givenContext) {
        const UILayoutContext context = BoundContext(givenContext);
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
        scale = std::clamp(scale, UIBounds::kMinScale, UIBounds::kMaxScale);

        UICanvasResolved resolved;
        resolved.scale = scale;
        resolved.transform = {scale, 0.0f, 0.0f};
        resolved.bounds = UIBounds::Bound(Rect{0.0f, 0.0f, context.outputWidth / scale, context.outputHeight / scale});
        resolved.viewBounds = resolved.bounds;
        resolved.safeBounds = Intersect(ToUnits(SafeAreaInOutput(context), resolved.transform), resolved.bounds);
        Finish(resolved, params);
        return resolved;
    }

}  // namespace BECore
