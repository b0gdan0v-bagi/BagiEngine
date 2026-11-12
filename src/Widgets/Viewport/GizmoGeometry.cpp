#include "GizmoGeometry.h"

#include <numbers>

namespace BECore::Gizmo {

    namespace {

        constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0f;
        constexpr float kHalfTurn = 180.0f;
        constexpr float kFullTurn = 360.0f;
        constexpr float kMiddle = 0.5f;
        constexpr float kAnchorEnd = 1.0f;

        float Length(Vec2 vector) {
            return std::hypot(vector.x, vector.y);
        }

        /// The size of one scene unit in screen pixels' terms: a zero or negative scale would divide by it, so it falls back to 1
        float SafeUnits(float unitsPerScreenPixel) {
            return unitsPerScreenPixel > 0.0f ? unitsPerScreenPixel : 1.0f;
        }

        /// The distance from (@p px, @p py) to the segment from (0, 0) to (@p endX, @p endY)
        float DistanceToSegmentFromOrigin(float px, float py, float endX, float endY) {
            const float lengthSquared = endX * endX + endY * endY;
            const float along = lengthSquared > 0.0f ? std::clamp((px * endX + py * endY) / lengthSquared, 0.0f, 1.0f) : 0.0f;
            return std::hypot(px - endX * along, py - endY * along);
        }

        Quad CornersOf(const Rect& rect) {
            return {Vec2{rect.x, rect.y}, Vec2{rect.x + rect.w, rect.y}, Vec2{rect.x + rect.w, rect.y + rect.h}, Vec2{rect.x, rect.y + rect.h}};
        }

        Handle HitTranslate(const Frame& frame, Vec2 point, float units) {
            const Vec2 local = TurnAbout(point, frame.origin, -frame.angle) - frame.origin;
            const float px = local.x / units;
            const float py = local.y / units;
            if (std::abs(px) <= kCenterHalfSize && std::abs(py) <= kCenterHalfSize) {
                return Handle::Free;
            }
            const float distanceX = DistanceToSegmentFromOrigin(px, py, kArrowLength, 0.0f);
            const float distanceY = DistanceToSegmentFromOrigin(px, py, 0.0f, kArrowLength);
            if (distanceX > kHandleHitRadius && distanceY > kHandleHitRadius) {
                return Handle::None;
            }
            return distanceX <= distanceY ? Handle::AxisX : Handle::AxisY;
        }

        Handle HitRotate(const Frame& frame, Vec2 point, float units) {
            const float distance = Length(point - frame.origin) / units;
            return std::abs(distance - kRingRadius) <= kRingHitHalfWidth ? Handle::Ring : Handle::None;
        }

        Handle HitScale(const Frame& frame, Vec2 point, float units) {
            const size_t lastHandle = frame.isGroup ? static_cast<size_t>(Handle::Corner3) : static_cast<size_t>(Handle::Edge3);
            Handle best = Handle::None;
            float bestDistance = std::numeric_limits<float>::max();
            for (size_t index = static_cast<size_t>(Handle::Corner0); index <= lastHandle; ++index) {
                const auto handle = static_cast<Handle>(index);
                const float distance = Length(HandlePosition(frame, handle, units) - point) / units;
                // Strictly nearer replaces, so a tie keeps the earlier handle (a corner before an edge)
                if (distance <= kHandleHitRadius && distance < bestDistance) {
                    best = handle;
                    bestDistance = distance;
                }
            }
            return best;
        }

    }  // namespace

    SnapSettings WithSnapModifier(const SnapSettings& snap, bool modifier) {
        SnapSettings result = snap;
        if (modifier) {
            result.position = !snap.position;
            result.angle = !snap.angle;
            result.scale = !snap.scale;
        }
        return result;
    }

    float SnapValue(float value, float step) {
        return step > 0.0f ? std::round(value / step) * step : value;
    }

    float NormalizeAngle(float degrees) {
        float wrapped = std::fmod(degrees, kFullTurn);
        if (wrapped > kHalfTurn) {
            wrapped -= kFullTurn;
        } else if (wrapped <= -kHalfTurn) {
            wrapped += kFullTurn;
        }
        return wrapped;
    }

    float SnapAngle(float degrees, float step) {
        return NormalizeAngle(SnapValue(degrees, step));
    }

    Vec2 TurnAbout(Vec2 point, Vec2 center, float degrees) {
        const float radians = degrees * kDegreesToRadians;
        const float sine = std::sin(radians);
        const float cosine = std::cos(radians);
        const float dx = point.x - center.x;
        const float dy = point.y - center.y;
        return {center.x + dx * cosine - dy * sine, center.y + dx * sine + dy * cosine};
    }

    Vec2 PivotPoint(const Item& item) {
        return {item.x + item.pivotX * item.width, item.y + item.pivotY * item.height};
    }

    Quad TurnedQuad(const Item& item) {
        const Vec2 pivot = PivotPoint(item);
        Quad quad = CornersOf({item.x, item.y, item.width, item.height});
        for (Vec2& corner : quad) {
            corner = TurnAbout(corner, pivot, item.rotation);
        }
        return quad;
    }

    Rect Bounds(eastl::span<const Item> items) {
        if (items.empty()) {
            return {};
        }
        Vec2 low{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        Vec2 high{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        for (const Item& item : items) {
            for (const Vec2& corner : TurnedQuad(item)) {
                low = {std::min(low.x, corner.x), std::min(low.y, corner.y)};
                high = {std::max(high.x, corner.x), std::max(high.y, corner.y)};
            }
        }
        return {low.x, low.y, high.x - low.x, high.y - low.y};
    }

    Vec2 Pivot(eastl::span<const Item> items) {
        if (items.empty()) {
            return {};
        }
        if (items.size() == 1) {
            return PivotPoint(items[0]);
        }
        const Rect bounds = Bounds(items);
        return {bounds.x + bounds.w * kMiddle, bounds.y + bounds.h * kMiddle};
    }

    Frame MakeFrame(eastl::span<const Item> items, Space space) {
        Frame frame;
        frame.origin = Pivot(items);
        frame.isGroup = items.size() > 1;
        if (items.size() == 1) {
            frame.box = TurnedQuad(items[0]);
            frame.angle = space == Space::Local ? items[0].rotation : 0.0f;
        } else if (frame.isGroup) {
            frame.box = CornersOf(Bounds(items));
        }
        return frame;
    }

    Vec2 AxisX(const Frame& frame) {
        return TurnAbout({1.0f, 0.0f}, {}, frame.angle);
    }

    Vec2 AxisY(const Frame& frame) {
        return TurnAbout({0.0f, 1.0f}, {}, frame.angle);
    }

    Vec2 HandleAnchor(Handle handle) {
        switch (handle) {
            case Handle::Corner0:
                return {0.0f, 0.0f};
            case Handle::Corner1:
                return {kAnchorEnd, 0.0f};
            case Handle::Corner2:
                return {kAnchorEnd, kAnchorEnd};
            case Handle::Corner3:
                return {0.0f, kAnchorEnd};
            case Handle::Edge0:
                return {kMiddle, 0.0f};
            case Handle::Edge1:
                return {kAnchorEnd, kMiddle};
            case Handle::Edge2:
                return {kMiddle, kAnchorEnd};
            case Handle::Edge3:
                return {0.0f, kMiddle};
            default:
                return {kMiddle, kMiddle};
        }
    }

    float RingRadius(float unitsPerScreenPixel) {
        return kRingRadius * SafeUnits(unitsPerScreenPixel);
    }

    Vec2 HandlePosition(const Frame& frame, Handle handle, float unitsPerScreenPixel) {
        const float units = SafeUnits(unitsPerScreenPixel);
        switch (handle) {
            case Handle::AxisX:
                return frame.origin + AxisX(frame) * (kArrowLength * units);
            case Handle::AxisY:
                return frame.origin + AxisY(frame) * (kArrowLength * units);
            case Handle::Ring:
                return frame.origin + AxisX(frame) * (kRingRadius * units);
            case Handle::Corner0:
            case Handle::Corner1:
            case Handle::Corner2:
            case Handle::Corner3:
                return frame.box[static_cast<size_t>(handle) - static_cast<size_t>(Handle::Corner0)];
            case Handle::Edge0:
            case Handle::Edge1:
            case Handle::Edge2:
            case Handle::Edge3: {
                const size_t side = static_cast<size_t>(handle) - static_cast<size_t>(Handle::Edge0);
                return (frame.box[side] + frame.box[(side + 1) % frame.box.size()]) * kMiddle;
            }
            default:
                return frame.origin;
        }
    }

    Handle HitTest(const Frame& frame, Tool tool, Vec2 point, float unitsPerScreenPixel) {
        const float units = SafeUnits(unitsPerScreenPixel);
        switch (tool) {
            case Tool::Translate:
                return HitTranslate(frame, point, units);
            case Tool::Rotate:
                return HitRotate(frame, point, units);
            case Tool::Scale:
                return HitScale(frame, point, units);
        }
        return Handle::None;
    }

}  // namespace BECore::Gizmo
