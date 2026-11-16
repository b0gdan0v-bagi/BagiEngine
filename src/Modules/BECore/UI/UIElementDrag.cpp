#include "UIElementDrag.h"

#include <cmath>

namespace BECore {

    namespace {

        constexpr float kHalf = 0.5f;
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

        bool Contains(const Rect& rect, const Vec2& point) {
            return point.x >= rect.x && point.x <= rect.x + rect.w && point.y >= rect.y && point.y <= rect.y + rect.h;
        }

        Rect Square(float centerX, float centerY, float size) {
            return {centerX - size * kHalf, centerY - size * kHalf, size, size};
        }

        bool MovesLeft(UIDragHandle handle) {
            return handle == UIDragHandle::TopLeft || handle == UIDragHandle::Left || handle == UIDragHandle::BottomLeft;
        }

        bool MovesRight(UIDragHandle handle) {
            return handle == UIDragHandle::TopRight || handle == UIDragHandle::Right || handle == UIDragHandle::BottomRight;
        }

        bool MovesTop(UIDragHandle handle) {
            return handle == UIDragHandle::TopLeft || handle == UIDragHandle::Top || handle == UIDragHandle::TopRight;
        }

        bool MovesBottom(UIDragHandle handle) {
            return handle == UIDragHandle::BottomLeft || handle == UIDragHandle::Bottom || handle == UIDragHandle::BottomRight;
        }

        bool IsCorner(UIDragHandle handle) {
            return (MovesLeft(handle) || MovesRight(handle)) && (MovesTop(handle) || MovesBottom(handle));
        }

        /// The line nearest to @p value within @p threshold, and how far @p value must move to sit on it
        struct LineHit {
            float line = 0.0f;
            float shift = 0.0f;
        };

        Optional<LineHit> NearestLine(float value, const Array<float, 3>& lines, float threshold) {
            Optional<LineHit> best;
            for (const float line : lines) {
                const float distance = std::abs(line - value);
                if (distance <= threshold && (!best || distance < std::abs(best->shift))) {
                    best = LineHit{line, line - value};
                }
            }
            return best;
        }

        /// The nearest of the three edges of a body to any line: which line, and how far the body must move
        Optional<LineHit> NearestBodyLine(float start, float size, const Array<float, 3>& lines, float threshold) {
            Optional<LineHit> best;
            for (const float edge : {start, start + size * kHalf, start + size}) {
                const Optional<LineHit> hit = NearestLine(edge, lines, threshold);
                if (hit && (!best || std::abs(hit->shift) < std::abs(best->shift))) {
                    best = hit;
                }
            }
            return best;
        }

    }  // namespace

    Optional<Rect> UIElementDrag::BoxOf(const UICanvasLayout& canvas, const UIElement& element) {
        if (!canvas.HasLayout() || !canvas.root) {
            return {};
        }
        const UIElement* parent = FindParent(*canvas.root, element);
        if (!parent) {
            return {};
        }
        return BoxOf(canvas, *parent, element, element.active.rect.region);
    }

    Rect UIElementDrag::BoxOf(const UICanvasLayout& canvas, const UIElement& parent, const UIElement& element, PoolString region) {
        if (const Rect* published = canvas.regions.Find(region)) {
            return *published;
        }
        if (&parent == canvas.root.Get() && element.ignoreSafeArea) {
            return canvas.GetResolved().bounds;
        }
        return parent.rect;
    }

    CanDragReason UIElementDrag::CanDrag(const UICanvasLayout& canvas, const UIElement& element) {
        if (!canvas.HasLayout() || !canvas.root) {
            return CanDragReason::NoLayout;
        }
        if (!element.active.visible || element.rect.IsEmpty()) {
            return CanDragReason::Hidden;
        }
        if (!UIPreviewOverlay::AnchorsOf(canvas, element)) {
            return CanDragReason::NoBox;
        }
        if (!(element.active.rect == element.style.rect)) {
            return CanDragReason::VariantOverrides;
        }
        return CanDragReason::Ok;
    }

    const char* UIElementDrag::ReasonText(CanDragReason reason) {
        switch (reason) {
            case CanDragReason::Ok:
                return "";
            case CanDragReason::NoLayout:
                return "The canvas has not been laid out yet";
            case CanDragReason::Hidden:
                return "The element is hidden or has no area";
            case CanDragReason::NoBox:
                return "The parent places this element itself (a stack or a popup): nothing to drag";
            case CanDragReason::VariantOverrides:
                return "A variant of the canvas overrides the rect: edit it in the inspector";
        }
        return "";
    }

    UIHandleSet UIElementDrag::HandleRects(const Rect& rect, float handleSize) {
        UIHandleSet set;
        if (rect.w < kMinHandleSizes * handleSize || rect.h < kMinHandleSizes * handleSize) {
            return set;
        }
        const float left = rect.x;
        const float right = rect.x + rect.w;
        const float top = rect.y;
        const float bottom = rect.y + rect.h;
        const float centerX = rect.x + rect.w * kHalf;
        const float centerY = rect.y + rect.h * kHalf;
        const auto add = [&](UIDragHandle handle, float x, float y) { set.items[set.count++] = {handle, Square(x, y, handleSize)}; };

        add(UIDragHandle::TopLeft, left, top);
        add(UIDragHandle::TopRight, right, top);
        add(UIDragHandle::BottomRight, right, bottom);
        add(UIDragHandle::BottomLeft, left, bottom);
        if (rect.w >= kEdgeHandleSizes * handleSize) {
            add(UIDragHandle::Top, centerX, top);
            add(UIDragHandle::Bottom, centerX, bottom);
        }
        if (rect.h >= kEdgeHandleSizes * handleSize) {
            add(UIDragHandle::Right, right, centerY);
            add(UIDragHandle::Left, left, centerY);
        }
        return set;
    }

    UIDragHandle UIElementDrag::HitHandle(const Rect& rect, const Vec2& point, float handleSize) {
        const UIHandleSet set = HandleRects(rect, handleSize);
        for (const UIHandleRect& item : set.View()) {
            if (Contains(item.rect, point)) {
                return item.handle;
            }
        }
        return Contains(rect, point) ? UIDragHandle::Body : UIDragHandle::None;
    }

    Rect UIElementDrag::Resize(const Rect& start, UIDragHandle handle, const Vec2& delta, const UIResizeOptions& options) {
        if (handle == UIDragHandle::None) {
            return start;
        }
        if (handle == UIDragHandle::Body) {
            Vec2 move = delta;
            if (options.lockAxis) {
                (std::abs(move.x) >= std::abs(move.y) ? move.y : move.x) = 0.0f;
            }
            return {start.x + move.x, start.y + move.y, start.w, start.h};
        }

        const float startRight = start.x + start.w;
        const float startBottom = start.y + start.h;
        float left = start.x;
        float right = startRight;
        float top = start.y;
        float bottom = startBottom;
        if (MovesLeft(handle)) {
            left = std::min(left + delta.x, right - kMinSize);
        } else if (MovesRight(handle)) {
            right = std::max(right + delta.x, left + kMinSize);
        }
        if (MovesTop(handle)) {
            top = std::min(top + delta.y, bottom - kMinSize);
        } else if (MovesBottom(handle)) {
            bottom = std::max(bottom + delta.y, top + kMinSize);
        }

        float width = right - left;
        float height = bottom - top;
        if (options.keepAspect && IsCorner(handle) && start.w > 0.0f && start.h > 0.0f) {
            const float factorX = width / start.w;
            const float factorY = height / start.h;
            float factor = std::abs(factorX - 1.0f) >= std::abs(factorY - 1.0f) ? factorX : factorY;
            factor = std::max({factor, kMinSize / start.w, kMinSize / start.h});
            width = start.w * factor;
            height = start.h * factor;
            left = MovesLeft(handle) ? startRight - width : start.x;
            top = MovesTop(handle) ? startBottom - height : start.y;
        }
        return {left, top, width, height};
    }

    UISnapLines UIElementDrag::SnapLines(const Rect& box) {
        return {{box.x, box.x + box.w * kHalf, box.x + box.w}, {box.y, box.y + box.h * kHalf, box.y + box.h}};
    }

    UISnapResult UIElementDrag::Snap(const Rect& wanted, UIDragHandle handle, const UISnapLines& lines, float threshold) {
        UISnapResult result;
        result.rect = wanted;
        if (handle == UIDragHandle::None) {
            return result;
        }

        if (handle == UIDragHandle::Body) {
            if (const Optional<LineHit> hit = NearestBodyLine(wanted.x, wanted.w, lines.x, threshold)) {
                result.rect.x += hit->shift;
                result.guideX = hit->line;
            }
            if (const Optional<LineHit> hit = NearestBodyLine(wanted.y, wanted.h, lines.y, threshold)) {
                result.rect.y += hit->shift;
                result.guideY = hit->line;
            }
            return result;
        }

        // The moved edge goes onto the line; the opposite edge stays, unless that would leave less than the minimum size
        if (MovesLeft(handle)) {
            if (const Optional<LineHit> hit = NearestLine(wanted.x, lines.x, threshold); hit && wanted.w - hit->shift >= kMinSize) {
                result.rect.x = hit->line;
                result.rect.w = wanted.w - hit->shift;
                result.guideX = hit->line;
            }
        } else if (MovesRight(handle)) {
            if (const Optional<LineHit> hit = NearestLine(wanted.x + wanted.w, lines.x, threshold); hit && wanted.w + hit->shift >= kMinSize) {
                result.rect.w = wanted.w + hit->shift;
                result.guideX = hit->line;
            }
        }
        if (MovesTop(handle)) {
            if (const Optional<LineHit> hit = NearestLine(wanted.y, lines.y, threshold); hit && wanted.h - hit->shift >= kMinSize) {
                result.rect.y = hit->line;
                result.rect.h = wanted.h - hit->shift;
                result.guideY = hit->line;
            }
        } else if (MovesBottom(handle)) {
            if (const Optional<LineHit> hit = NearestLine(wanted.y + wanted.h, lines.y, threshold); hit && wanted.h + hit->shift >= kMinSize) {
                result.rect.h = wanted.h + hit->shift;
                result.guideY = hit->line;
            }
        }
        return result;
    }

}  // namespace BECore
