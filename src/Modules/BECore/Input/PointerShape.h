#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <algorithm>
#include <cstdint>

namespace BECore {

    /// TransformRect: the rect of the node's TransformComponent. Rect: an explicit rect. Circle and Ellipse: inscribed in the
    /// explicit rect, or in the node's rect when there is none (a circle has the shorter side as its diameter, centered)
    CORE_ENUM(PointerShapeKind, uint8_t, TransformRect, Rect, Circle, Ellipse)

    /**
     * @brief The area of a scene target that takes a pointer, in scene units. Pure code.
     *
     * Edges count as inside, like the rect test of a clip. A shape of zero or negative size contains nothing, and so does a
     * point that is not a number.
     */
    struct PointerShape {
        PointerShapeKind kind = PointerShapeKind::TransformRect;
        Optional<Rect> rect;  ///< used by Rect always, and by Circle and Ellipse when set

        [[nodiscard]] static PointerShape InRect(const Rect& area) {
            return PointerShape{PointerShapeKind::Rect, area};
        }

        [[nodiscard]] static PointerShape CircleIn(const Rect& area) {
            return PointerShape{PointerShapeKind::Circle, area};
        }

        [[nodiscard]] static PointerShape EllipseIn(const Rect& area) {
            return PointerShape{PointerShapeKind::Ellipse, area};
        }

        /// @param nodeRect the rect of the node's TransformComponent; only the kinds that use it read it
        [[nodiscard]] bool Contains(const Rect& nodeRect, float x, float y) const {
            const Rect& area = (kind == PointerShapeKind::TransformRect || !rect.has_value()) ? nodeRect : *rect;
            if (!(area.w > 0.0f && area.h > 0.0f)) {
                return false;
            }
            switch (kind) {
                case PointerShapeKind::Circle: {
                    const float radius = std::min(area.w, area.h) * 0.5f;
                    const float dx = x - (area.x + area.w * 0.5f);
                    const float dy = y - (area.y + area.h * 0.5f);
                    return dx * dx + dy * dy <= radius * radius;
                }
                case PointerShapeKind::Ellipse: {
                    const float rx = area.w * 0.5f;
                    const float ry = area.h * 0.5f;
                    const float nx = (x - (area.x + rx)) / rx;
                    const float ny = (y - (area.y + ry)) / ry;
                    return nx * nx + ny * ny <= 1.0f;
                }
                case PointerShapeKind::TransformRect:
                case PointerShapeKind::Rect:
                    break;
            }
            return x >= area.x && x <= area.x + area.w && y >= area.y && y <= area.y + area.h;
        }
    };

}  // namespace BECore
