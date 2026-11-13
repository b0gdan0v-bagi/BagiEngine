#include "UIBoxMesh.h"

#include <BECore/UI/UIGeometryBounds.h>
#include <cmath>
#include <numbers>

namespace BECore {

    namespace {

        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kHalfPi = kPi * 0.5f;
        constexpr int kCornerCount = 4;
        constexpr int kMaxRings = UIBoxMesh::kBlurRings + 1;

        /// Where each corner's arc starts, in the order top-left, top-right, bottom-right, bottom-left: the outline runs clockwise on the screen (y down)
        constexpr float kCornerStartAngle[kCornerCount] = {kPi, kPi + kHalfPi, 0.0f, kHalfPi};

        /// One outline of the box and the alpha its vertices get: `offset` moves the outline outwards (negative: inwards) from the edge of the rect
        struct Ring {
            float offset = 0.0f;
            float alpha = 0.0f;
        };

        struct Arc {
            float centerX = 0.0f;
            float centerY = 0.0f;
            float radius = 0.0f;
        };

        /// One vertex of an outline: where it is, and which corner and step of it, so a colour function can tell a position on the arc
        struct PointInfo {
            float x = 0.0f;
            float y = 0.0f;
            int corner = 0;
            int step = 0;
            int segments = 0;
        };

        /// Which corners an outline visits and how much of each. A closed path is the whole box; the highlight is the top: the second half of the top-left
        /// arc and the first half of the top-right one.
        struct Path {
            bool closed = true;
            bool topHalves = false;
        };

        /// A box with an area whose numbers are within UIBounds::kMaxExtent and a scale within the range of a real output (a number past either would make a vertex
        /// or the feather infinite)
        [[nodiscard]] bool IsValidBox(const Rect& rect, float pixelsPerUnit) {
            return UIBounds::IsBounded(rect.x) && UIBounds::IsBounded(rect.y) && UIBounds::IsBounded(rect.w) && UIBounds::IsBounded(rect.h) && rect.w > 0.0f && rect.h > 0.0f &&
                   pixelsPerUnit >= UIBounds::kMinScale && pixelsPerUnit <= UIBounds::kMaxScale;
        }

        [[nodiscard]] unsigned char ScaleAlpha(unsigned char alpha, float factor) {
            return static_cast<unsigned char>(std::clamp(std::lround(static_cast<float>(alpha) * factor), 0L, 255L));
        }

        [[nodiscard]] unsigned char LerpChannel(unsigned char from, unsigned char to, float t) {
            const float value = static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * t;
            return static_cast<unsigned char>(std::clamp(std::lround(value), 0L, 255L));
        }

        [[nodiscard]] float Smoothstep(float t) {
            const float x = std::clamp(t, 0.0f, 1.0f);
            return x * x * (3.0f - 2.0f * x);
        }

        /// The arcs of the outline of @p rect moved by @p offset. A corner that is rounded keeps its centre and grows or shrinks its radius (so the outline is a true
        /// offset curve); a square corner stays square unless @p roundOutwards asks for the offset curve of a point (a shadow's soft corner)
        [[nodiscard]] eastl::array<Arc, kCornerCount> MakeArcs(const Rect& rect, const UICorners& corners, float offset, bool roundOutwards) {
            const float left = rect.x - offset;
            const float top = rect.y - offset;
            const float right = rect.x + rect.w + offset;
            const float bottom = rect.y + rect.h + offset;

            const auto grow = [&](float radius) { return radius > 0.0f || roundOutwards ? std::max(0.0f, radius + offset) : 0.0f; };
            const UICorners radii = UICorners{grow(corners.topLeft), grow(corners.topRight), grow(corners.bottomRight), grow(corners.bottomLeft)}.Clamped(right - left, bottom - top);

            return {{
                {left + radii.topLeft, top + radii.topLeft, radii.topLeft},
                {right - radii.topRight, top + radii.topRight, radii.topRight},
                {right - radii.bottomRight, bottom - radii.bottomRight, radii.bottomRight},
                {left + radii.bottomLeft, bottom - radii.bottomLeft, radii.bottomLeft},
            }};
        }

        /// The steps of corner @p corner that a path visits: [first, last]
        struct StepRange {
            int first = 0;
            int last = 0;
        };

        [[nodiscard]] StepRange VisitedSteps(const Path& path, int corner, int segments) {
            if (!path.topHalves) {
                return {0, segments};
            }
            const int half = segments - segments / 2;
            return corner == 0 ? StepRange{segments / 2, segments} : StepRange{0, half};
        }

        /// Twice the signed area of the triangle (a, b, c) in y-down coordinates: positive is clockwise on the screen
        [[nodiscard]] float Cross(const GeometryVertex& a, const GeometryVertex& b, const GeometryVertex& c) {
            return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        }

        void AddTriangle(eastl::vector<int>& indices, int a, int b, int c) {
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(c);
        }

        /**
         * The fully opaque inside of the closed outline whose points start at @p ring (the corners one after the other, each `segments + 1` points, clockwise).
         * The outline is convex, so the four arc centres (@p arcs) are inside it and the core is cut along them: the quad of the centres (two triangles), a small
         * fan from each centre over the points of its own corner, and for each side the quad between the two centres and the two points where the arcs meet the
         * side. A triangle is at best half of its bounding box, and these are close to it, so the bounding boxes the SDL software rasterizer pays for
         * (`SDL_SW_FillTriangle`) add up to about twice the area, where a fan from one point would cost its number of points times the area. A corner with no radius has no fan and no side
         * triangle (they would have no area). The centres are appended to @p vertices (their colour is @p colorOf at the centre: a gradient is linear in the
         * position, so it interpolates exactly) with the alpha of the ring.
         */
        template <typename ColorFn>
        void AppendCore(eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices, int ring, const int* segments, const eastl::array<Arc, kCornerCount>& arcs, float alpha,
                        ColorFn&& colorOf) {
            int cornerFirst[kCornerCount];
            int next = ring;
            for (int c = 0; c < kCornerCount; ++c) {
                cornerFirst[c] = next;
                next += segments[c] + 1;
            }

            int center[kCornerCount];
            for (int c = 0; c < kCornerCount; ++c) {
                PointInfo point;
                point.x = arcs[c].centerX;
                point.y = arcs[c].centerY;
                point.corner = c;
                point.segments = segments[c];
                Color color = colorOf(point);
                color.a = ScaleAlpha(color.a, alpha);
                center[c] = static_cast<int>(vertices.size());
                vertices.push_back(GeometryVertex{point.x, point.y, color, 0.0f, 0.0f});
            }

            // The quad of the centres can have one reflex corner (a large radius next to small ones); its diagonal must not leave it, so try 0-2 and else take 1-3
            const bool diagonalFromFirst = Cross(vertices[center[0]], vertices[center[1]], vertices[center[2]]) >= 0.0f && Cross(vertices[center[0]], vertices[center[2]], vertices[center[3]]) >= 0.0f;
            if (diagonalFromFirst) {
                AddTriangle(indices, center[0], center[1], center[2]);
                AddTriangle(indices, center[0], center[2], center[3]);
            } else {
                AddTriangle(indices, center[0], center[1], center[3]);
                AddTriangle(indices, center[1], center[2], center[3]);
            }

            for (int c = 0; c < kCornerCount; ++c) {
                for (int step = 0; step < segments[c]; ++step) {
                    AddTriangle(indices, center[c], cornerFirst[c] + step, cornerFirst[c] + step + 1);
                }
                // The side from the end of this corner's arc to the start of the next corner's: the quad (centre, arc end, next arc start, next centre)
                const int nextCorner = (c + 1) % kCornerCount;
                const int arcEnd = cornerFirst[c] + segments[c];
                const int nextStart = cornerFirst[nextCorner];
                if (arcs[c].radius > 0.0f) {
                    AddTriangle(indices, center[c], arcEnd, nextStart);
                }
                if (arcs[nextCorner].radius > 0.0f) {
                    AddTriangle(indices, center[c], nextStart, center[nextCorner]);
                }
            }
        }

        /**
         * Appends the vertices of every ring (each ring is one outline with the same number of points, so the corresponding points of two rings are
         * neighbours), strips of triangles between each two rings and, with @p fillCore, the core inside the last ring (`AppendCore`: not a fan, whose
         * bounding boxes would all be the whole box). @p colorOf gives the colour of a point before the ring's alpha is applied.
         */
        template <typename ColorFn>
        void AppendRings(eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices, const Rect& rect, const UICorners& corners, const Ring* rings, int ringCount, const Path& path,
                         bool roundOutwards, bool fillCore, float pixelsPerUnit, float cornerSagitta, ColorFn&& colorOf) {
            const UICorners baseCorners = corners.Clamped(rect.w, rect.h);
            const float halfShortSide = std::min(rect.w, rect.h) * 0.5f;

            // One number of segments per corner for every ring, from the largest radius of the corner, so the rings line up
            float largestOffset = -halfShortSide;
            for (int r = 0; r < ringCount; ++r) {
                largestOffset = std::max(largestOffset, rings[r].offset);
            }
            const auto largestArcs = MakeArcs(rect, baseCorners, largestOffset, roundOutwards);
            int segments[kCornerCount];
            for (int c = 0; c < kCornerCount; ++c) {
                segments[c] = UIBoxMesh::CornerSegments(largestArcs[c].radius * pixelsPerUnit, cornerSagitta);
            }

            const int firstCorner = 0;
            const int lastCorner = path.topHalves ? 1 : kCornerCount - 1;
            int pointsPerRing = 0;
            for (int c = firstCorner; c <= lastCorner; ++c) {
                const StepRange range = VisitedSteps(path, c, segments[c]);
                pointsPerRing += range.last - range.first + 1;
            }

            const int firstVertex = static_cast<int>(vertices.size());
            eastl::array<Arc, kCornerCount> lastArcs;
            for (int r = 0; r < ringCount; ++r) {
                // An outline cannot move in further than the middle of the shorter side
                const float offset = std::max(rings[r].offset, -halfShortSide);
                const auto arcs = MakeArcs(rect, baseCorners, offset, roundOutwards);
                lastArcs = arcs;
                for (int c = firstCorner; c <= lastCorner; ++c) {
                    const Arc& arc = arcs[c];
                    const StepRange range = VisitedSteps(path, c, segments[c]);
                    for (int step = range.first; step <= range.last; ++step) {
                        const float angle = kCornerStartAngle[c] + (segments[c] > 0 ? kHalfPi * static_cast<float>(step) / static_cast<float>(segments[c]) : 0.0f);
                        PointInfo point;
                        point.x = arc.centerX + arc.radius * std::cos(angle);
                        point.y = arc.centerY + arc.radius * std::sin(angle);
                        point.corner = c;
                        point.step = step;
                        point.segments = segments[c];
                        Color color = colorOf(point);
                        color.a = ScaleAlpha(color.a, rings[r].alpha);
                        vertices.push_back(GeometryVertex{point.x, point.y, color, 0.0f, 0.0f});
                    }
                }
            }

            const int stripLength = path.closed ? pointsPerRing : pointsPerRing - 1;
            for (int r = 0; r + 1 < ringCount; ++r) {
                const int outer = firstVertex + r * pointsPerRing;
                const int inner = outer + pointsPerRing;
                for (int i = 0; i < stripLength; ++i) {
                    const int next = (i + 1) % pointsPerRing;
                    indices.push_back(outer + i);
                    indices.push_back(outer + next);
                    indices.push_back(inner + next);
                    indices.push_back(outer + i);
                    indices.push_back(inner + next);
                    indices.push_back(inner + i);
                }
            }

            if (fillCore && path.closed) {
                AppendCore(vertices, indices, firstVertex + (ringCount - 1) * pointsPerRing, segments, lastArcs, rings[ringCount - 1].alpha, colorOf);
            }
        }

        /// The rings of a line from @p outerEdge to @p innerEdge (offsets of the outline, outer larger): a trapezoid of alpha whose slopes are one feather wide and
        /// centred on the two edges. A line thinner than a feather is a tent of the same area. Returns the number of rings.
        int MakeStrokeRings(float outerEdge, float innerEdge, float feather, Ring* out) {
            const float width = outerEdge - innerEdge;
            if (width >= feather) {
                out[0] = {outerEdge + feather * 0.5f, 0.0f};
                out[1] = {outerEdge - feather * 0.5f, 1.0f};
                out[2] = {innerEdge + feather * 0.5f, 1.0f};
                out[3] = {innerEdge - feather * 0.5f, 0.0f};
                return 4;
            }
            const float center = (outerEdge + innerEdge) * 0.5f;
            out[0] = {center + feather, 0.0f};
            out[1] = {center, width / feather};
            out[2] = {center - feather, 0.0f};
            return 3;
        }

    }  // namespace

    UICorners UICorners::Clamped(float width, float height) const {
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f) {
            return {};
        }
        const auto sane = [](float radius) { return std::isfinite(radius) && radius > 0.0f ? radius : 0.0f; };
        UICorners result{sane(topLeft), sane(topRight), sane(bottomRight), sane(bottomLeft)};

        float factor = 1.0f;
        const auto limit = [&factor](float side, float sum) {
            if (sum > side) {
                factor = std::min(factor, side / sum);
            }
        };
        limit(width, result.topLeft + result.topRight);
        limit(width, result.bottomLeft + result.bottomRight);
        limit(height, result.topLeft + result.bottomLeft);
        limit(height, result.topRight + result.bottomRight);
        if (factor < 1.0f) {
            result.topLeft *= factor;
            result.topRight *= factor;
            result.bottomRight *= factor;
            result.bottomLeft *= factor;
        }
        return result;
    }

    UICorners UICorners::Inset(float given) const {
        const float amount = UIBounds::Bound(given);
        const auto shrink = [amount](float radius) { return std::max(0.0f, radius - amount); };
        return {shrink(topLeft), shrink(topRight), shrink(bottomRight), shrink(bottomLeft)};
    }

    int UIBoxMesh::CornerSegments(float radiusPixels, float sagittaPixels) {
        if (!(radiusPixels > 0.0f) || !std::isfinite(radiusPixels)) {
            return radiusPixels > 0.0f ? kMaxCornerSegments : 0;
        }
        // A chord of the arc that spans an angle `step` lies R * (1 - cos(step / 2)) inside it; the step is the one the tolerance allows if it is measured
        // against the whole angle, which is a little stricter than needed, so n >= (pi / 2) / acos(1 - tolerance / R)
        const float ratio = std::clamp(1.0f - sagittaPixels / radiusPixels, -1.0f, 1.0f);
        const float step = std::acos(ratio);
        const int segments = step > 0.0f ? static_cast<int>(std::ceil(kHalfPi / step)) : kMaxCornerSegments;
        return std::clamp(segments, kMinCornerSegments, kMaxCornerSegments);
    }

    void UIBoxMesh::AppendFill(eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices, const Rect& rect, const UICorners& corners, const Color& color, const UIGradientSpec& gradient,
                               float pixelsPerUnit) {
        if (!IsValidBox(rect, pixelsPerUnit)) {
            return;
        }
        const bool hasGradient = gradient.direction != UIGradientDirection::None;
        if (color.a == 0 && (!hasGradient || gradient.end.a == 0)) {
            return;
        }

        const float feather = kFeatherPixels / pixelsPerUnit;
        const float coreInset = std::min(feather * 0.5f, std::min(rect.w, rect.h) * 0.5f);
        const Ring rings[2] = {{feather * 0.5f, 0.0f}, {-coreInset, 1.0f}};

        // The gradient runs over the core, so the outermost vertices of the top (or left) hold exactly the start colour and the last ones exactly the end
        const bool vertical = gradient.direction == UIGradientDirection::Vertical;
        const float axisStart = vertical ? rect.y + coreInset : rect.x + coreInset;
        const float axisLength = (vertical ? rect.h : rect.w) - 2.0f * coreInset;

        AppendRings(vertices, indices, rect, corners, rings, 2, Path{}, false, true, pixelsPerUnit, UIBoxMesh::kCornerSagittaPixels, [&](const PointInfo& point) {
            if (!hasGradient) {
                return color;
            }
            const float t = axisLength > 0.0f ? std::clamp(((vertical ? point.y : point.x) - axisStart) / axisLength, 0.0f, 1.0f) : 0.0f;
            return Color{LerpChannel(color.r, gradient.end.r, t), LerpChannel(color.g, gradient.end.g, t), LerpChannel(color.b, gradient.end.b, t), LerpChannel(color.a, gradient.end.a, t)};
        });
    }

    void UIBoxMesh::AppendBorder(eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices, const Rect& rect, const UICorners& corners, float width, const Color& color,
                                 float pixelsPerUnit) {
        if (!IsValidBox(rect, pixelsPerUnit) || !UIBounds::IsBounded(width) || width <= 0.0f || color.a == 0) {
            return;
        }
        const float feather = kFeatherPixels / pixelsPerUnit;
        const float halfShortSide = std::min(rect.w, rect.h) * 0.5f;
        if (width + feather * 0.5f >= halfShortSide) {
            // Nothing is left inside: the border is the whole shape
            AppendFill(vertices, indices, rect, corners, color, UIGradientSpec{}, pixelsPerUnit);
            return;
        }

        Ring rings[kMaxRings];
        const int ringCount = MakeStrokeRings(0.0f, -width, feather, rings);
        AppendRings(vertices, indices, rect, corners, rings, ringCount, Path{}, false, false, pixelsPerUnit, UIBoxMesh::kCornerSagittaPixels, [&](const PointInfo&) { return color; });
    }

    void UIBoxMesh::AppendShadow(eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices, const Rect& rect, const UICorners& corners, const UIShadowSpec& shadow, float pixelsPerUnit,
                                 GeometryDetail detail) {
        if (!IsValidBox(rect, pixelsPerUnit) || !UIBounds::IsBounded(shadow.offsetX) || !UIBounds::IsBounded(shadow.offsetY) || !UIBounds::IsBounded(shadow.blur) ||
            !UIBounds::IsBounded(shadow.spread) || shadow.color.a == 0) {
            return;
        }

        // The shape the blur is centred on: the box moved by the offset and grown by the spread. A rounded corner grows its radius by the spread, a square one stays square
        const Rect base{rect.x + shadow.offsetX - shadow.spread, rect.y + shadow.offsetY - shadow.spread, rect.w + 2.0f * shadow.spread, rect.h + 2.0f * shadow.spread};
        if (!IsValidBox(base, pixelsPerUnit)) {
            return;
        }
        const UICorners source = corners.Clamped(rect.w, rect.h);
        const auto grow = [&shadow](float radius) { return radius > 0.0f ? std::max(0.0f, radius + shadow.spread) : 0.0f; };
        const UICorners baseCorners{grow(source.topLeft), grow(source.topRight), grow(source.bottomRight), grow(source.bottomLeft)};

        const float feather = kFeatherPixels / pixelsPerUnit;
        const float blur = std::max(shadow.blur, 0.0f);
        const Color color = shadow.color;
        const auto flat = [&color](const PointInfo&) { return color; };
        const bool reduced = detail == GeometryDetail::Reduced;
        const float sagitta = reduced ? kReducedCornerSagittaPixels : kCornerSagittaPixels;
        const int steps = reduced ? kReducedBlurRings : kBlurRings;

        if (blur < feather) {
            const float inset = std::min(feather * 0.5f, std::min(base.w, base.h) * 0.5f);
            const Ring rings[2] = {{feather * 0.5f, 0.0f}, {-inset, 1.0f}};
            AppendRings(vertices, indices, base, baseCorners, rings, 2, Path{}, false, true, pixelsPerUnit, sagitta, flat);
            return;
        }

        // Rings from `blur` outside the edge (alpha 0) to `blur` inside it, the alpha following a smoothstep of the distance. An outline cannot move in
        // further than the middle of the shorter side, so a small shape stops at the alpha it reaches there
        const float halfShortSide = std::min(base.w, base.h) * 0.5f;
        Ring rings[kMaxRings];
        for (int i = 0; i <= steps; ++i) {
            const float offset = std::max(blur * (1.0f - 2.0f * static_cast<float>(i) / static_cast<float>(steps)), -halfShortSide);
            rings[i] = {offset, Smoothstep((blur - offset) / (2.0f * blur))};
        }
        AppendRings(vertices, indices, base, baseCorners, rings, steps + 1, Path{}, true, true, pixelsPerUnit, sagitta, flat);
    }

    void UIBoxMesh::AppendHighlight(eastl::vector<GeometryVertex>& vertices, eastl::vector<int>& indices, const Rect& rect, const UICorners& corners, const Color& color, float pixelsPerUnit) {
        if (!IsValidBox(rect, pixelsPerUnit) || color.a == 0) {
            return;
        }
        const float feather = kFeatherPixels / pixelsPerUnit;
        if (std::min(rect.w, rect.h) * 0.5f <= kHighlightWidth + feather) {
            return;
        }

        // The line is moved in by half a feather so that its soft outer edge stays inside the shape
        Ring rings[kMaxRings];
        const int ringCount = MakeStrokeRings(-feather * 0.5f, -feather * 0.5f - kHighlightWidth, feather, rings);
        Path path;
        path.closed = false;
        path.topHalves = true;

        // The line fades out along the half arcs: full on the straight top, 0 at the far end of each arc
        AppendRings(vertices, indices, rect, corners, rings, ringCount, path, false, false, pixelsPerUnit, UIBoxMesh::kCornerSagittaPixels, [&](const PointInfo& point) {
            Color faded = color;
            const int half = point.segments - point.segments / 2;
            if (half > 0) {
                const float fraction =
                    point.corner == 0 ? static_cast<float>(point.step - point.segments / 2) / static_cast<float>(half) : 1.0f - static_cast<float>(point.step) / static_cast<float>(half);
                faded.a = ScaleAlpha(color.a, fraction);
            }
            return faded;
        });
    }

}  // namespace BECore
