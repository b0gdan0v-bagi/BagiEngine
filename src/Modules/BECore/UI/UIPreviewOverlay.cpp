#include "UIPreviewOverlay.h"

#include <BECore/UI/UILayout.h>

namespace BECore {

    namespace {

        // The tolerance and the lower limit of the density of UILayoutValidator's touch rule: the two must agree
        constexpr float kTouchTolerance = 0.01f;
        constexpr float kMinPixelDensity = 1e-3f;

        void CollectTouchTargets(const UIElement& element, const UICanvasResolved& resolved, float minTouchSize, float pixelDensity, Vector<TouchMark>& marks) {
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

        void CollectElementMarks(const UIElement& element, const UICanvasResolved& resolved, uint32_t depth, Vector<ElementMark>& marks) {
            if (!element.active.visible) {
                return;
            }
            if (!element.rect.IsEmpty()) {
                marks.push_back({resolved.transform.ToOutput(element.rect.x, element.rect.y, element.rect.w, element.rect.h), element.name, depth, &element});
            }
            if (element.HasPopupChildren() && !element.IsPopupOpen()) {
                return;
            }
            for (const auto& child : element.children) {
                CollectElementMarks(*child, resolved, depth + 1, marks);
            }
        }

        // The parent of @p target under @p node, or null when it is not there
        const UIElement* FindParent(const UIElement& node, const UIElement& target) {
            for (const auto& child : node.children) {
                if (child.Get() == &target) {
                    return &node;
                }
                if (const UIElement* found = FindParent(*child, target)) {
                    return found;
                }
            }
            return nullptr;
        }

    }  // namespace

    Rect UIPreviewOverlay::ViewRect(float viewWidth, float viewHeight, float outputWidth, float outputHeight) {
        if (viewWidth <= 0.0f || viewHeight <= 0.0f) {
            return {0.0f, 0.0f, outputWidth, outputHeight};
        }
        return ViewTransform::Fit(viewWidth, viewHeight, outputWidth, outputHeight).ToOutput(0.0f, 0.0f, viewWidth, viewHeight);
    }

    void UIPreviewOverlay::TouchTargets(const UICanvasLayout& canvas, float minTouchSize, float pixelDensity, Vector<TouchMark>& marks) {
        marks.clear();
        if (!canvas.HasLayout() || !canvas.root) {
            return;
        }
        for (const auto& child : canvas.root->children) {
            CollectTouchTargets(*child, canvas.GetResolved(), minTouchSize, pixelDensity, marks);
        }
    }

    Vector<TouchMark> UIPreviewOverlay::TouchTargets(const UICanvasLayout& canvas, float minTouchSize, float pixelDensity) {
        Vector<TouchMark> marks;
        TouchTargets(canvas, minTouchSize, pixelDensity, marks);
        return marks;
    }

    void UIPreviewOverlay::RegionMarks(const UICanvasLayout& canvas, Vector<RegionMark>& marks) {
        marks.clear();
        if (!canvas.HasLayout()) {
            return;
        }
        const ViewTransform& transform = canvas.GetResolved().transform;
        marks.reserve(canvas.regions.Size());
        for (const auto& region : canvas.regions) {
            marks.push_back({region.name, transform.ToOutput(region.rect.x, region.rect.y, region.rect.w, region.rect.h)});
        }
    }

    Vector<RegionMark> UIPreviewOverlay::RegionMarks(const UICanvasLayout& canvas) {
        Vector<RegionMark> marks;
        RegionMarks(canvas, marks);
        return marks;
    }

    void UIPreviewOverlay::ElementMarks(const UICanvasLayout& canvas, Vector<ElementMark>& marks) {
        marks.clear();
        if (!canvas.HasLayout() || !canvas.root) {
            return;
        }
        for (const auto& child : canvas.root->children) {
            CollectElementMarks(*child, canvas.GetResolved(), 0, marks);
        }
    }

    Vector<ElementMark> UIPreviewOverlay::ElementMarks(const UICanvasLayout& canvas) {
        Vector<ElementMark> marks;
        ElementMarks(canvas, marks);
        return marks;
    }

    Optional<AnchorMark> UIPreviewOverlay::AnchorsOf(const UICanvasLayout& canvas, const UIElement& element) {
        if (!canvas.HasLayout() || !canvas.root || !element.active.visible) {
            return {};
        }
        const UIElement* parent = FindParent(*canvas.root, element);
        if (!parent || parent->layout == UILayoutKind::Stack || parent->HasPopupChildren()) {
            return {};
        }
        const UICanvasResolved& resolved = canvas.GetResolved();
        const Rect* region = canvas.regions.Find(element.active.rect.region);
        const Rect& box = region ? *region : parent == canvas.root.Get() && element.ignoreSafeArea ? resolved.bounds : parent->rect;
        const UIEffectiveAnchors anchors = UILayout::EffectiveAnchors(element.active.rect);

        const Rect anchorRect = {box.x + anchors.min.x * box.w, box.y + anchors.min.y * box.h, (anchors.max.x - anchors.min.x) * box.w, (anchors.max.y - anchors.min.y) * box.h};
        const ViewTransform& transform = resolved.transform;
        AnchorMark mark;
        mark.box = transform.ToOutput(box.x, box.y, box.w, box.h);
        mark.anchors = transform.ToOutput(anchorRect.x, anchorRect.y, anchorRect.w, anchorRect.h);
        mark.pivot = {(element.rect.x + anchors.pivot.x * element.rect.w) * transform.scale + transform.offsetX,
                      (element.rect.y + anchors.pivot.y * element.rect.h) * transform.scale + transform.offsetY};
        return mark;
    }

    Optional<Rect> UIPreviewOverlay::DesignRect(const UICanvasLayout& canvas) {
        const UICanvasScaleParams& params = canvas.params;
        if (!canvas.HasLayout() || params.space != CanvasSpace::Screen || params.scaleMode != CanvasScaleMode::ScaleWithScreen) {
            return {};
        }
        const Rect& bounds = canvas.GetResolved().bounds;
        const float x = bounds.x + (bounds.w - params.referenceWidth) * 0.5f;
        const float y = bounds.y + (bounds.h - params.referenceHeight) * 0.5f;
        return canvas.GetResolved().transform.ToOutput(x, y, params.referenceWidth, params.referenceHeight);
    }

    void UIPreviewOverlay::FindingMarks(const UICanvasLayout& canvas, Span<const UILayoutIssue> issues, Vector<FindingMark>& marks) {
        marks.clear();
        const ViewTransform& transform = canvas.GetResolved().transform;
        for (const UILayoutIssue& issue : issues) {
            marks.push_back({issue.kind, issue.element, issue.other, transform.ToOutput(issue.rect.x, issue.rect.y, issue.rect.w, issue.rect.h), issue.source});
        }
    }

    Vector<FindingMark> UIPreviewOverlay::FindingMarks(const UICanvasLayout& canvas, Span<const UILayoutIssue> issues) {
        Vector<FindingMark> marks;
        FindingMarks(canvas, issues, marks);
        return marks;
    }

    void UIPreviewOverlay::FindingText(const UILayoutIssue& issue, String& text) {
        text.clear();
        auto out = eastl::back_inserter(text);
        FormatTo(out, "{}: {}", EnumUtils<UILayoutIssueKind>::ToString(issue.kind), issue.element.ToStringView());
        if (!issue.other.Empty()) {
            FormatTo(out, " / {}", issue.other.ToStringView());
        }
        FormatTo(out, " ({:g} x {:g})", issue.rect.w, issue.rect.h);
    }

    Rect UIPreviewOverlay::CanvasBounds(const UICanvasResolved& resolved) {
        return resolved.transform.ToOutput(resolved.bounds.x, resolved.bounds.y, resolved.bounds.w, resolved.bounds.h);
    }

    Rect UIPreviewOverlay::ContentBounds(const UICanvasResolved& resolved) {
        return resolved.transform.ToOutput(resolved.contentBounds.x, resolved.contentBounds.y, resolved.contentBounds.w, resolved.contentBounds.h);
    }

    const Vector<UILayoutIssue>& UIFindingsCache::Get(const UICanvasLayout& canvas, float minTouchSize) {
        if (!canvas.HasLayout()) {
            Reset();
            return _issues;
        }
        if (!_hasIssues || _canvas != &canvas || _layoutRuns != canvas.GetLayoutRuns() || _minTouchSize != minTouchSize) {
            UILayoutValidatorOptions options;
            options.minTouchSize = minTouchSize;
            options.pixelDensity = 1.0f;
            _issues = canvas.Validate(options);
            _canvas = &canvas;
            _layoutRuns = canvas.GetLayoutRuns();
            _minTouchSize = minTouchSize;
            _hasIssues = true;
            ++_validationRuns;
        }
        return _issues;
    }

    void UIFindingsCache::Reset() {
        _issues.clear();
        _canvas = nullptr;
        _hasIssues = false;
    }

}  // namespace BECore
