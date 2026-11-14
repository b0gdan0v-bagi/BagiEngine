#pragma once

#include <BECore/Math/Vec2.h>

namespace BECore {

    /**
     * @brief A rotation about a point and a mirror for IRenderer::DrawTransformedRect() and DrawTransformedTexture().
     */
    struct DrawTransform {
        float rotation = 0.0f;  ///< degrees, clockwise on the screen (y points down)
        float pivotX = 0.0f;    ///< the point the draw turns around: absolute, in the units of the draw call (not relative to the rect)
        float pivotY = 0.0f;
        bool flipX = false;  ///< mirrors the picture about its own center, left to right (the default draws swap the UVs, SDL uses `SDL_FLIP_*`; independent of the rotation)
        bool flipY = false;  ///< the same, top to bottom

        [[nodiscard]] constexpr bool IsIdentity() const {
            return rotation == 0.0f && !flipX && !flipY;
        }
    };

    /// The four corners of a rect turned about the pivot of @p transform, in the order top-left, top-right, bottom-left, bottom-right
    /// of the unrotated rect (the order of the two triangles {0, 1, 2} and {2, 1, 3}). Flips are not part of the corners: they are the UVs.
    // std: the games' tests take the result as std::array until core-type-aliases-cleanup
    [[nodiscard]] std::array<Vec2, 4> MakeTransformedQuad(const Rect& rect, const DrawTransform& transform);

}  // namespace BECore
