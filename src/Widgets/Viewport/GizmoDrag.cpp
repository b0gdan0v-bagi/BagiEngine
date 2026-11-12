#include "GizmoDrag.h"

#include <numbers>

namespace BECore::Gizmo {

    namespace {

        constexpr float kRadiansToDegrees = 180.0f / std::numbers::pi_v<float>;
        constexpr float kAxisAlignedEpsilon = 1e-4f;  // a unit axis whose other component is below this lies along a screen axis
        constexpr float kMiddle = 0.5f;

        float Dot(Vec2 a, Vec2 b) {
            return a.x * b.x + a.y * b.y;
        }

        float AngleOf(Vec2 vector) {
            return std::atan2(vector.y, vector.x) * kRadiansToDegrees;
        }

        /// -1 for an anchor at the start of an axis, +1 at its end, 0 in the middle
        float SideOf(float anchor) {
            if (anchor < kMiddle) {
                return -1.0f;
            }
            return anchor > kMiddle ? 1.0f : 0.0f;
        }

        /// Where on an axis the point that stays put is: the far end from a handle at one end, the middle for a handle in the middle
        float FixedAnchor(float side) {
            if (side == 0.0f) {
                return kMiddle;
            }
            return side > 0.0f ? 0.0f : 1.0f;
        }

        /// The factor that turns @p size into @p wanted, 1 for a size that is not positive
        float FactorOf(float size, float wanted) {
            return size > 0.0f ? wanted / size : 1.0f;
        }

        /// The smallest factor that keeps both sides of @p item at kMinItemSize or more (0 for a side that is not positive: nothing to keep)
        float MinFactor(const Item& item) {
            float result = 0.0f;
            if (item.width > 0.0f) {
                result = std::max(result, kMinItemSize / item.width);
            }
            if (item.height > 0.0f) {
                result = std::max(result, kMinItemSize / item.height);
            }
            return result;
        }

    }  // namespace

    void GizmoDrag::Begin(Tool tool, Space space, Handle handle, eastl::span<const Item> items, Vec2 point, const SnapSettings& snap) {
        _active = false;
        _start.assign(items.begin(), items.end());
        _result.assign(items.begin(), items.end());
        if (items.empty() || handle == Handle::None) {
            return;
        }
        _active = true;
        _tool = tool;
        _handle = handle;
        _snap = snap;
        _startPoint = point;
        _frame = MakeFrame(items, space);
        _pivot = _frame.origin;
    }

    eastl::span<const Item> GizmoDrag::Update(Vec2 point, bool snapModifier, bool keepAspect) {
        if (!_active) {
            return {};
        }
        _result.assign(_start.begin(), _start.end());
        const SnapSettings snap = WithSnapModifier(_snap, snapModifier);
        switch (_tool) {
            case Tool::Translate:
                ApplyTranslate(point, snap);
                break;
            case Tool::Rotate:
                ApplyRotate(point, snap);
                break;
            case Tool::Scale:
                ApplyScale(point, snap, keepAspect);
                break;
        }
        return {_result.data(), _result.size()};
    }

    eastl::span<const Item> GizmoDrag::Cancel() {
        _active = false;
        _result.assign(_start.begin(), _start.end());
        return {_start.data(), _start.size()};
    }

    void GizmoDrag::End() {
        _active = false;
    }

    void GizmoDrag::ApplyTranslate(Vec2 point, const SnapSettings& snap) {
        Vec2 offset = point - _startPoint;
        const bool alongAxis = _handle == Handle::AxisX || _handle == Handle::AxisY;
        Vec2 axis;
        if (alongAxis) {
            axis = _handle == Handle::AxisX ? AxisX(_frame) : AxisY(_frame);
            offset = axis * Dot(offset, axis);
        } else if (_handle != Handle::Free) {
            return;
        }
        if (snap.position && snap.positionStep > 0.0f) {
            const Item& first = _start[0];
            const float step = snap.positionStep;
            if (!alongAxis) {
                offset = {SnapValue(first.x + offset.x, step) - first.x, SnapValue(first.y + offset.y, step) - first.y};
            } else if (std::abs(axis.y) < kAxisAlignedEpsilon) {
                offset = {SnapValue(first.x + offset.x, step) - first.x, 0.0f};
            } else if (std::abs(axis.x) < kAxisAlignedEpsilon) {
                offset = {0.0f, SnapValue(first.y + offset.y, step) - first.y};
            } else {
                offset = axis * SnapValue(Dot(offset, axis), step);
            }
        }
        for (Item& item : _result) {
            item.x += offset.x;
            item.y += offset.y;
        }
    }

    void GizmoDrag::ApplyRotate(Vec2 point, const SnapSettings& snap) {
        if (_handle != Handle::Ring) {
            return;
        }
        float sweep = NormalizeAngle(AngleOf(point - _pivot) - AngleOf(_startPoint - _pivot));
        if (snap.angle) {
            sweep = SnapAngle(sweep, snap.angleStep);
        }
        if (sweep == 0.0f) {
            return;
        }
        const bool isGroup = _result.size() > 1;
        for (Item& item : _result) {
            if (isGroup) {
                // The item turns about its own pivot, which itself travels along the circle about the group's pivot
                const Vec2 moved = TurnAbout(PivotPoint(item), _pivot, sweep);
                item.x = moved.x - item.pivotX * item.width;
                item.y = moved.y - item.pivotY * item.height;
            }
            item.rotation = NormalizeAngle(item.rotation + sweep);
        }
    }

    void GizmoDrag::ApplyScale(Vec2 point, const SnapSettings& snap, bool keepAspect) {
        if (!IsCorner(_handle) && !IsEdge(_handle)) {
            return;
        }
        if (_result.size() > 1) {
            ApplyScaleGroup(point, snap);
        } else {
            ApplyScaleOne(point, snap, keepAspect);
        }
    }

    void GizmoDrag::ApplyScaleOne(Vec2 point, const SnapSettings& snap, bool keepAspect) {
        const Item& start = _start[0];
        Item& item = _result[0];
        const Vec2 center = PivotPoint(start);
        // The pointer's travel in the item's own axes, as they were at the press
        const Vec2 travel = TurnAbout(point, center, -start.rotation) - TurnAbout(_startPoint, center, -start.rotation);
        const Vec2 anchor = HandleAnchor(_handle);
        const float sideX = SideOf(anchor.x);
        const float sideY = SideOf(anchor.y);

        float factorX = sideX != 0.0f ? FactorOf(start.width, start.width + sideX * travel.x) : 1.0f;
        float factorY = sideY != 0.0f ? FactorOf(start.height, start.height + sideY * travel.y) : 1.0f;
        if (keepAspect) {
            // Both sides of a corner move; the one that moved further decides. An edge has only the one it moves
            float uniform = factorX;
            if (sideX == 0.0f) {
                uniform = factorY;
            } else if (sideY != 0.0f && std::abs(factorY - 1.0f) > std::abs(factorX - 1.0f)) {
                uniform = factorY;
            }
            factorX = uniform;
            factorY = uniform;
        }
        if (snap.scale) {
            factorX = SnapValue(factorX, snap.scaleStep);
            factorY = keepAspect ? factorX : SnapValue(factorY, snap.scaleStep);
        }
        if (keepAspect) {
            factorX = std::max(factorX, MinFactor(start));
            factorY = factorX;
        }
        const float width = std::max(kMinItemSize, start.width * factorX);
        const float height = std::max(kMinItemSize, start.height * factorY);
        if (width == start.width && height == start.height) {
            return;
        }

        // Keep the fixed point where it was: it is (fixedU, fixedV) in the rect, before and after, and the rect turns about its pivot
        const float fixedU = FixedAnchor(sideX);
        const float fixedV = FixedAnchor(sideY);
        const Vec2 fixedPoint = TurnAbout({start.x + fixedU * start.width, start.y + fixedV * start.height}, center, start.rotation);
        const Vec2 fixedFromPivot = TurnAbout({(fixedU - start.pivotX) * width, (fixedV - start.pivotY) * height}, {}, start.rotation);
        const Vec2 newCenter = fixedPoint - fixedFromPivot;
        item.width = width;
        item.height = height;
        item.x = newCenter.x - start.pivotX * width;
        item.y = newCenter.y - start.pivotY * height;
    }

    void GizmoDrag::ApplyScaleGroup(Vec2 point, const SnapSettings& snap) {
        const Vec2 startOffset = _startPoint - _pivot;
        const float startDistance = std::hypot(startOffset.x, startOffset.y);
        const Vec2 offset = point - _pivot;
        float factor = startDistance > 0.0f ? std::hypot(offset.x, offset.y) / startDistance : 1.0f;
        if (snap.scale) {
            factor = SnapValue(factor, snap.scaleStep);
        }
        for (const Item& item : _start) {
            factor = std::max(factor, MinFactor(item));
        }
        for (Item& item : _result) {
            const Vec2 pivotPoint = PivotPoint(item);
            const Vec2 moved = _pivot + (pivotPoint - _pivot) * factor;
            item.width *= factor;
            item.height *= factor;
            item.x = moved.x - item.pivotX * item.width;
            item.y = moved.y - item.pivotY * item.height;
        }
    }

}  // namespace BECore::Gizmo
