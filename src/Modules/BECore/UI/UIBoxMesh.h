#pragma once

#include <BECore/Renderer/IRenderer.h>

namespace BECore {

    /// The four corner radii of a box, in UI units. The order is the one of a file and of a CSS `border-radius`: top-left, top-right, bottom-right, bottom-left.
    struct UICorners {
        float topLeft = 0.0f;
        float topRight = 0.0f;
        float bottomRight = 0.0f;
        float bottomLeft = 0.0f;

        [[nodiscard]] constexpr bool operator==(const UICorners&) const = default;

        [[nodiscard]] static constexpr UICorners Uniform(float radius) {
            return {radius, radius, radius, radius};
        }

        /// No corner is rounded
        [[nodiscard]] constexpr bool IsZero() const {
            return topLeft == 0.0f && topRight == 0.0f && bottomRight == 0.0f && bottomLeft == 0.0f;
        }

        /**
         * The radii that fit a box of @p width x @p height. A negative or non-finite radius is 0. When the two radii on one side add up to more than that
         * side, all four radii shrink by the same factor (as CSS does), so neighbouring corners never cross and a radius larger than half the shorter side
         * becomes half of it. A box with no area has no radii.
         */
        [[nodiscard]] UICorners Clamped(float width, float height) const;

        /// Every radius reduced by @p amount and not below 0: the corners of the same box moved inwards by @p amount (the inner edge of a border)
        [[nodiscard]] UICorners Inset(float amount) const;
    };

    /// A shadow or a glow (the builder treats both alike): the box moved by the offset, grown by the spread, softened over the blur
    struct UIShadowSpec {
        float offsetX = 0.0f;  ///< UI units, to the right
        float offsetY = 0.0f;  ///< UI units, down
        float blur = 0.0f;     ///< UI units: the alpha falls from full to 0 over twice this width, centered on the edge (CSS blur radius); 0 is a hard edge
        float spread = 0.0f;   ///< UI units the box grows by on every side before the blur (negative shrinks it)
        Color color;           ///< the colour at full strength; its alpha is the peak of the shadow

        [[nodiscard]] bool operator==(const UIShadowSpec& other) const {
            return offsetX == other.offsetX && offsetY == other.offsetY && blur == other.blur && spread == other.spread && color.r == other.color.r && color.g == other.color.g &&
                   color.b == other.color.b && color.a == other.color.a;
        }
    };

    /// None is one flat colour; Vertical goes from the top edge to the bottom edge, Horizontal from the left edge to the right one
    CORE_ENUM(UIGradientDirection, uint8_t, None, Vertical, Horizontal)

    /// The shape of the parts of a toggle, a slider and a progress bar: Square is the plain rectangle of every part, Round a pill for a track and a circle for a square thumb
    CORE_ENUM(UIPartShape, uint8_t, Square, Round)

    /// A two-colour gradient: the colour of the box is its start, `end` is the colour at the far edge
    struct UIGradientSpec {
        UIGradientDirection direction = UIGradientDirection::None;
        Color end;

        [[nodiscard]] bool operator==(const UIGradientSpec& other) const {
            return direction == other.direction && end.r == other.end.r && end.g == other.end.g && end.b == other.end.b && end.a == other.end.a;
        }
    };

    /**
     * @brief Turns a rounded box into triangles for IRenderer::DrawGeometry (untextured, per-vertex alpha), anti-aliased at any scale.
     *
     * Pure, like UINineSlice: no renderer, scene or window. Every function appends to the vertex and index lists it is given (the element keeps them as
     * scratch, so a steady frame allocates nothing) and draws nothing for a rect with no area, a non-finite number or one past `UIBounds::kMaxExtent`. Indices are positions in @p vertices
     * (not relative to this call), so several calls can fill one list for one draw. Every triangle has the same winding: clockwise on the screen (a positive
     * cross product in y-down coordinates).
     *
     * Units: positions, radii, widths, offsets and blurs are UI units; @p pixelsPerUnit is the output pixels of one unit (`ViewTransform::scale` at the
     * draw). It sets the number of segments of a corner and the feather: the edge is a ring of vertices of alpha 0 and, one output pixel inside it, a ring of
     * full alpha, so the shape's true edge sits in the middle of the ramp and the covered area stays the rect's. Nothing is drawn for a @p pixelsPerUnit outside
     * [`UIBounds::kMinScale`, `UIBounds::kMaxScale`].
     *
     * Colours are straight alpha. The corners come from `UICorners`; pass them through `Clamped` first or not: every function clamps them to the rect itself.
     */
    class UIBoxMesh {
    public:
        static constexpr float kFeatherPixels = 1.0f;                ///< the width of the alpha ramp of every edge, in output pixels
        static constexpr float kCornerSagittaPixels = 0.1f;          ///< how far a corner's polygon may sit inside the true arc, in output pixels
        static constexpr int kMinCornerSegments = 2;                 ///< the fewest segments of a rounded corner (a quarter circle)
        static constexpr int kMaxCornerSegments = 32;                ///< the most segments of a rounded corner
        static constexpr int kBlurRings = 8;                         ///< the steps of the falloff of a blur (the rings are one more)
        static constexpr int kReducedBlurRings = 4;                  ///< the same for GeometryDetail::Reduced
        static constexpr float kReducedCornerSagittaPixels = 0.25f;  ///< the corner tolerance of a shadow or glow at GeometryDetail::Reduced
        static constexpr float kHighlightWidth = 1.0f;               ///< the thickness of the inner top line, in UI units

        /// What one call appends at most, whatever the numbers are (a shape has four corners of at most kMaxCornerSegments + 1 points on each ring, and at most kBlurRings + 1 rings;
        /// the strips are two triangles per point between two rings, the core is the quad of the four arc centres, a fan of at most kMaxCornerSegments triangles per
        /// corner and two triangles per side, and its four centres are vertices of their own): the lists of the element never grow past these in a call
        static constexpr int kMaxRingPoints = 4 * (kMaxCornerSegments + 1);
        static constexpr int kCoreCenterVertices = 4;
        static constexpr int kMaxVerticesPerCall = (kBlurRings + 1) * kMaxRingPoints + kCoreCenterVertices;
        static constexpr int kMaxCoreTriangles = 2 + 4 * kMaxCornerSegments + 4 * 2;
        static constexpr int kMaxIndicesPerCall = 6 * kBlurRings * kMaxRingPoints + 3 * kMaxCoreTriangles;

        /// The segments of one quarter-circle corner of @p radiusPixels (0 for a corner that is not rounded), within the limits above; @p sagittaPixels is how far the
        /// polygon may sit inside the arc
        [[nodiscard]] static int CornerSegments(float radiusPixels, float sagittaPixels = kCornerSagittaPixels);

        /**
         * A solid or gradient box. The fully opaque core is the rect inset by half the feather; outside it one feather of alpha falling to 0. A gradient runs
         * over the core (so the top vertices hold exactly the colour and the bottom ones exactly `gradient.end`). Nothing for a colour with no alpha at
         * both ends.
         *
         * The core (of a fill, a shadow and a glow) is not a fan from one corner: the SDL software renderer pays for every triangle the pixels of its bounding
         * rectangle (a temporary surface, `SDL_SW_FillTriangle` with blending), and a fan of n points would cost n times the area. It is cut along the four arc
         * centres into small triangles, about twice the area in bounding boxes; the covered area is the same, so Vulkan sees no difference.
         */
        static void AppendFill(eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices, const Rect& rect, const UICorners& corners, const Color& color, const UIGradientSpec& gradient,
                               float pixelsPerUnit);

        /**
         * The ring between the outline of @p rect and the outline moved inwards by @p width, feathered on both sides. It is drawn over a fill of the whole
         * rect; to avoid the overlap (a translucent border) fill the inner outline instead: `rect` inset by @p width on every side with `corners.Inset(width)`.
         * A width larger than the shape holds is the whole shape; a width under one output pixel is drawn as a fainter line.
         */
        static void AppendBorder(eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices, const Rect& rect, const UICorners& corners, float width, const Color& color,
                                 float pixelsPerUnit);

        /**
         * A shadow or a glow of the box: @p rect moved by the offset and grown by the spread, with the blur falloff outward and inward over the blur: the
         * alpha is the colour's at the inner ring and 0 at the outer one (`blur` units beyond the edge), falling by a smooth curve. A blur under one output
         * pixel is a hard edge with the plain feather. A shape smaller than its blur never reaches full alpha. The box itself is not cut out of the shadow:
         * the fill is drawn over it.
         *
         * @p detail is what the renderer asks for (`IRenderer::GetGeometryDetail()`): Full is the mesh above, Reduced has kReducedBlurRings steps and corners within
         * kReducedCornerSagittaPixels, a quarter of the triangles, for a renderer that pays for each one. The bounds of a call hold for both.
         */
        static void AppendShadow(eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices, const Rect& rect, const UICorners& corners, const UIShadowSpec& shadow, float pixelsPerUnit,
                                 GeometryDetail detail = GeometryDetail::Full);

        /**
         * A line of @ref kHighlightWidth units just inside the top edge, following the top corners (it starts and fades out half way round each of them), for a
         * raised look. It stays inside the shape. Nothing for a rect too small to hold it.
         */
        static void AppendHighlight(eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices, const Rect& rect, const UICorners& corners, const Color& color, float pixelsPerUnit);
    };

}  // namespace BECore
