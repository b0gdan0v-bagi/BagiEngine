#include "UIBoxTexture.h"

#include <BECore/UI/UIGeometryBounds.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr float kFeatherPixels = UIBoxMesh::kFeatherPixels;
        constexpr float kHalfFeather = kFeatherPixels * 0.5f;
        constexpr int kSafetyPixels = 1;  ///< added to every reach, so a rounding never cuts an edge of the paint
        constexpr int kChannels = 4;
        constexpr float kMaxChannel = 255.0f;

        enum class Side { Left, Right, Top, Bottom };

        [[nodiscard]] bool IsVisible(const Color& color) {
            return color.a > 0;
        }

        [[nodiscard]] bool IsBoundedLook(const UIBoxLook& look) {
            const auto spec = [](const UIShadowSpec& s) { return UIBounds::IsBounded(s.offsetX) && UIBounds::IsBounded(s.offsetY) && UIBounds::IsBounded(s.blur) && UIBounds::IsBounded(s.spread); };
            return UIBounds::IsBounded(look.corners.topLeft) && UIBounds::IsBounded(look.corners.topRight) && UIBounds::IsBounded(look.corners.bottomRight) &&
                   UIBounds::IsBounded(look.corners.bottomLeft) && UIBounds::IsBounded(look.borderWidth) && spec(look.shadow) && spec(look.glow) && look.scale >= UIBounds::kMinScale &&
                   look.scale <= UIBounds::kMaxScale;
        }

        [[nodiscard]] float CornerAt(const UICorners& corners, Side side) {
            switch (side) {
                case Side::Left:
                    return std::max(corners.topLeft, corners.bottomLeft);
                case Side::Right:
                    return std::max(corners.topRight, corners.bottomRight);
                case Side::Top:
                    return std::max(corners.topLeft, corners.topRight);
                case Side::Bottom:
                    return std::max(corners.bottomLeft, corners.bottomRight);
            }
            return 0.0f;
        }

        /// How far @p spec moves its shape towards the inside of the box at @p side (units)
        [[nodiscard]] float ShiftInwards(const UIShadowSpec& spec, Side side) {
            switch (side) {
                case Side::Left:
                    return spec.offsetX;
                case Side::Right:
                    return -spec.offsetX;
                case Side::Top:
                    return spec.offsetY;
                case Side::Bottom:
                    return -spec.offsetY;
            }
            return 0.0f;
        }

        /// What the paint of a box reaches at one side, in output pixels: `outside` beyond the edge of the box, `inside` how far from the edge it is not yet the same along the edge
        struct Reach {
            float outside = kHalfFeather;  // the soft edge of the fill
            float inside = 0.0f;
        };

        /// The rings of a shadow or glow go from `reach` outside the edge of its shape to `reach` inside it; the corner of each ring keeps its centre and stays within the larger of its
        /// radius and the ring's inset (UIBoxMesh::AppendShadow), so the paint is the same along the edge beyond that
        void AddShadow(const UIBoxLook& look, const UIShadowSpec& spec, Side side, Reach& reach) {
            if (!IsVisible(spec.color)) {
                return;
            }
            const float feather = kFeatherPixels / look.scale;
            const float ring = spec.blur < feather ? feather * 0.5f : spec.blur;
            const float inwards = ShiftInwards(spec, side);
            const float radius = CornerAt(look.corners, side);
            const float grown = radius > 0.0f ? std::max(0.0f, radius + spec.spread) : 0.0f;
            reach.outside = std::max(reach.outside, (spec.spread - inwards + ring) * look.scale);
            reach.inside = std::max(reach.inside, (inwards - spec.spread + std::max(ring, grown)) * look.scale);
        }

        [[nodiscard]] Reach ReachAt(const UIBoxLook& look, Side side) {
            Reach reach;
            reach.inside = std::max(CornerAt(look.corners, side) * look.scale, kHalfFeather);
            AddShadow(look, look.shadow, side, reach);
            AddShadow(look, look.glow, side, reach);
            if (look.borderWidth > 0.0f) {
                reach.inside = std::max(reach.inside, look.borderWidth * look.scale + kHalfFeather);
            }
            if (IsVisible(look.highlight) && side != Side::Bottom) {
                reach.inside = std::max(reach.inside, UIBoxMesh::kHighlightWidth * look.scale + kFeatherPixels);
            }
            return reach;
        }

        [[nodiscard]] int CeilPixels(float value) {
            return static_cast<int>(std::ceil(std::max(value, 0.0f))) + kSafetyPixels;
        }

        [[nodiscard]] UIBoxTextureAxis MakeAxis(const UIBoxLook& look, Side before, Side after, int size, bool graded) {
            const Reach first = ReachAt(look, before);
            const Reach last = ReachAt(look, after);
            UIBoxTextureAxis axis;
            axis.size = size;
            axis.marginBefore = CeilPixels(first.outside);
            axis.marginAfter = CeilPixels(last.outside);
            axis.capBefore = CeilPixels(first.inside);
            axis.capAfter = CeilPixels(last.inside);
            axis.sliced = !graded && size >= axis.capBefore + axis.capAfter;
            return axis;
        }

        // ---- the rasterizer: the triangles of the mesh into a premultiplied float buffer ----

        constexpr int kSubpixelBits = 8;
        constexpr int64_t kSubpixel = int64_t{1} << kSubpixelBits;
        constexpr int64_t kHalfSubpixel = kSubpixel / 2;

        struct FixedPoint {
            int64_t x = 0;
            int64_t y = 0;
        };

        /// Twice the signed area of (a, b, p): positive when p is on the inside of a clockwise (on the screen, y down) edge a to b
        [[nodiscard]] int64_t Edge(const FixedPoint& a, const FixedPoint& b, int64_t px, int64_t py) {
            return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
        }

        /// The top-left rule: a pixel centre exactly on an edge belongs to the triangle only when the edge is a top or a left one, so two triangles that share an edge cover it once
        [[nodiscard]] bool IsTopLeft(const FixedPoint& a, const FixedPoint& b) {
            const int64_t dx = b.x - a.x;
            const int64_t dy = b.y - a.y;
            return (dy == 0 && dx > 0) || dy < 0;
        }

        struct Canvas {
            int width = 0;
            int height = 0;
            Vector<float> pixels;  ///< premultiplied RGBA, 0 to 1
        };

        [[nodiscard]] int64_t FloorDiv(int64_t value, int64_t divisor) {
            int64_t quotient = value / divisor;
            if (value % divisor != 0 && ((value < 0) != (divisor < 0))) {
                --quotient;
            }
            return quotient;
        }

        void FillTriangle(Canvas& canvas, const GeometryVertex& a, const GeometryVertex& b, const GeometryVertex& c, float scale) {
            if (a.color.a == 0 && b.color.a == 0 && c.color.a == 0) {
                return;
            }
            const GeometryVertex* vertex[3] = {&a, &b, &c};
            const auto toFixed = [scale](float value) { return static_cast<int64_t>(std::llround(static_cast<double>(value) * static_cast<double>(scale) * static_cast<double>(kSubpixel))); };
            FixedPoint p[3];
            for (int i = 0; i < 3; ++i) {
                p[i] = {toFixed(vertex[i]->x), toFixed(vertex[i]->y)};
            }
            int64_t area = Edge(p[0], p[1], p[2].x, p[2].y);
            if (area == 0) {
                return;
            }
            if (area < 0) {
                std::swap(p[1], p[2]);
                std::swap(vertex[1], vertex[2]);
                area = -area;
            }

            const int64_t minX = std::max<int64_t>(0, FloorDiv(std::min({p[0].x, p[1].x, p[2].x}), kSubpixel));
            const int64_t maxX = std::min<int64_t>(canvas.width - 1, FloorDiv(std::max({p[0].x, p[1].x, p[2].x}), kSubpixel));
            const int64_t minY = std::max<int64_t>(0, FloorDiv(std::min({p[0].y, p[1].y, p[2].y}), kSubpixel));
            const int64_t maxY = std::min<int64_t>(canvas.height - 1, FloorDiv(std::max({p[0].y, p[1].y, p[2].y}), kSubpixel));

            // edge[i] is the edge opposite vertex i, so its value over the area is the weight of that vertex
            const FixedPoint* from[3] = {&p[1], &p[2], &p[0]};
            const FixedPoint* to[3] = {&p[2], &p[0], &p[1]};
            int64_t bias[3];
            int64_t stepX[3];
            for (int i = 0; i < 3; ++i) {
                bias[i] = IsTopLeft(*from[i], *to[i]) ? 0 : -1;
                stepX[i] = -(to[i]->y - from[i]->y) * kSubpixel;
            }

            const double inverseArea = 1.0 / static_cast<double>(area);
            for (int64_t y = minY; y <= maxY; ++y) {
                const int64_t py = y * kSubpixel + kHalfSubpixel;
                int64_t edge[3];
                for (int i = 0; i < 3; ++i) {
                    edge[i] = Edge(*from[i], *to[i], minX * kSubpixel + kHalfSubpixel, py);
                }
                float* row = canvas.pixels.data() + (static_cast<size_t>(y) * static_cast<size_t>(canvas.width) + static_cast<size_t>(minX)) * kChannels;
                for (int64_t x = minX; x <= maxX; ++x, row += kChannels) {
                    const bool inside = ((edge[0] + bias[0]) | (edge[1] + bias[1]) | (edge[2] + bias[2])) >= 0;
                    if (inside) {
                        double red = 0.0;
                        double green = 0.0;
                        double blue = 0.0;
                        double alpha = 0.0;
                        for (int i = 0; i < 3; ++i) {
                            const double weight = static_cast<double>(edge[i]) * inverseArea;
                            const Color& color = vertex[i]->color;
                            red += weight * color.r;
                            green += weight * color.g;
                            blue += weight * color.b;
                            alpha += weight * color.a;
                        }
                        alpha /= kMaxChannel;
                        if (alpha > 0.0) {
                            const double keep = 1.0 - alpha;
                            const double scaleToUnit = alpha / kMaxChannel;
                            row[0] = static_cast<float>(red * scaleToUnit + static_cast<double>(row[0]) * keep);
                            row[1] = static_cast<float>(green * scaleToUnit + static_cast<double>(row[1]) * keep);
                            row[2] = static_cast<float>(blue * scaleToUnit + static_cast<double>(row[2]) * keep);
                            row[3] = static_cast<float>(alpha + static_cast<double>(row[3]) * keep);
                        }
                    }
                    for (int i = 0; i < 3; ++i) {
                        edge[i] += stepX[i];
                    }
                }
            }
        }

        [[nodiscard]] uint8_t ToByte(float unit) {
            return static_cast<uint8_t>(std::clamp(std::lround(unit * kMaxChannel), 0L, 255L));
        }

        struct Segment {
            int source = 0;
            int sourceLength = 0;
            int destination = 0;
            int destinationLength = 0;
        };

        /// The pieces of one axis, the box starting at 0
        size_t MakeSegments(const UIBoxTextureAxis& axis, Segment (&out)[3]) {
            const int length = axis.Length();
            if (!axis.sliced) {
                out[0] = {0, length, -axis.marginBefore, length};
                return 1;
            }
            const int lead = axis.marginBefore + axis.capBefore;
            const int trail = axis.capAfter + axis.marginAfter;
            out[0] = {0, lead, -axis.marginBefore, lead};
            out[1] = {lead + UIBoxTexture::kMiddlePixels / 2, 1, axis.capBefore, axis.size - axis.capBefore - axis.capAfter};
            out[2] = {length - trail, trail, axis.size - axis.capAfter, trail};
            return 3;
        }

    }  // namespace

    bool UIBoxLook::operator==(const UIBoxLook& other) const {
        return corners == other.corners && borderWidth == other.borderWidth && ColorsEqual(borderColor, other.borderColor) && gradientDirection == other.gradientDirection &&
               ColorsEqual(fill, other.fill) && ColorsEqual(gradientEnd, other.gradientEnd) && shadow == other.shadow && glow == other.glow && ColorsEqual(highlight, other.highlight) &&
               scale == other.scale;
    }

    int UIBoxTextureAxis::ReferenceSize() const {
        return sliced ? capBefore + capAfter + UIBoxTexture::kMiddlePixels : size;
    }

    int UIBoxTextureAxis::Length() const {
        return marginBefore + ReferenceSize() + marginAfter;
    }

    UIBoxLook UIBoxTexture::MakeLook(const UIBoxStyle& style, const Color& fill, const Color& gradientEnd, float scale, float width, float height) {
        UIBoxLook look;
        look.corners = style.cornerRadius.Clamped(width, height);
        if (style.borderWidth > 0.0f && IsVisible(style.borderColor)) {
            look.borderWidth = style.borderWidth;
            look.borderColor = style.borderColor;
        }
        look.gradientDirection = style.gradientDirection;
        look.fill = fill;
        look.gradientEnd = look.gradientDirection == UIGradientDirection::None ? fill : gradientEnd;
        // A shadow or a glow with no alpha, a highlight with none, is the same as none: equal keys
        if (IsVisible(style.shadow.color)) {
            look.shadow = style.shadow;
        }
        if (IsVisible(style.glow.color)) {
            look.glow = style.glow;
        }
        if (IsVisible(style.highlight)) {
            look.highlight = style.highlight;
        }
        look.scale = scale;
        return look;
    }

    Optional<UIBoxTextureLayout> UIBoxTexture::MakeLayout(const UIBoxLook& look, int widthPixels, int heightPixels) {
        if (widthPixels < 1 || heightPixels < 1 || widthPixels > kMaxSide || heightPixels > kMaxSide || !IsBoundedLook(look)) {
            return std::nullopt;
        }
        UIBoxTextureLayout layout;
        layout.x = MakeAxis(look, Side::Left, Side::Right, widthPixels, look.gradientDirection == UIGradientDirection::Horizontal);
        layout.y = MakeAxis(look, Side::Top, Side::Bottom, heightPixels, look.gradientDirection == UIGradientDirection::Vertical);
        const int64_t width = layout.Width();
        const int64_t height = layout.Height();
        if (width > kMaxSide || height > kMaxSide || width * height > kMaxPixels) {
            return std::nullopt;
        }
        layout.key.look = look;
        layout.key.wholeWidth = layout.x.sliced ? 0 : widthPixels;
        layout.key.wholeHeight = layout.y.sliced ? 0 : heightPixels;
        return layout;
    }

    Vector<uint8_t> UIBoxTexture::Render(const UIBoxTextureLayout& layout) {
        const UIBoxLook& look = layout.key.look;
        const float scale = look.scale;
        const Rect rect{static_cast<float>(layout.x.marginBefore) / scale, static_cast<float>(layout.y.marginBefore) / scale, static_cast<float>(layout.x.ReferenceSize()) / scale,
                        static_cast<float>(layout.y.ReferenceSize()) / scale};

        Vector<GeometryVertex> vertices;
        Vector<int> indices;
        UIBoxMesh::AppendShadow(vertices, indices, rect, look.corners, look.shadow, scale);
        UIBoxMesh::AppendShadow(vertices, indices, rect, look.corners, look.glow, scale);
        UIBoxMesh::AppendFill(vertices, indices, rect, look.corners, look.fill, UIGradientSpec{look.gradientDirection, look.gradientEnd}, scale);
        if (look.borderWidth > 0.0f) {
            UIBoxMesh::AppendBorder(vertices, indices, rect, look.corners, look.borderWidth, look.borderColor, scale);
        }
        UIBoxMesh::AppendHighlight(vertices, indices, rect, look.corners, look.highlight, scale);

        Canvas canvas;
        canvas.width = layout.Width();
        canvas.height = layout.Height();
        canvas.pixels.assign(static_cast<size_t>(canvas.width) * static_cast<size_t>(canvas.height) * kChannels, 0.0f);
        const auto vertexCount = static_cast<int>(vertices.size());
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const int a = indices[i];
            const int b = indices[i + 1];
            const int c = indices[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= vertexCount || b >= vertexCount || c >= vertexCount) {
                continue;
            }
            FillTriangle(canvas, vertices[static_cast<size_t>(a)], vertices[static_cast<size_t>(b)], vertices[static_cast<size_t>(c)], scale);
        }

        Vector<uint8_t> rgba(canvas.pixels.size());
        for (size_t i = 0; i < canvas.pixels.size(); i += kChannels) {
            const float alpha = canvas.pixels[i + 3];
            if (alpha * kMaxChannel < 0.5f) {
                continue;  // stays 0: fully transparent
            }
            rgba[i] = ToByte(canvas.pixels[i] / alpha);
            rgba[i + 1] = ToByte(canvas.pixels[i + 1] / alpha);
            rgba[i + 2] = ToByte(canvas.pixels[i + 2] / alpha);
            rgba[i + 3] = ToByte(alpha);
        }
        return rgba;
    }

    size_t UIBoxTexture::MakePieces(const UIBoxTextureLayout& layout, Array<UIBoxTexturePiece, kMaxPieces>& pieces) {
        Segment columns[3];
        Segment rows[3];
        const size_t columnCount = MakeSegments(layout.x, columns);
        const size_t rowCount = MakeSegments(layout.y, rows);
        size_t count = 0;
        for (size_t r = 0; r < rowCount; ++r) {
            for (size_t c = 0; c < columnCount; ++c) {
                const Segment& column = columns[c];
                const Segment& row = rows[r];
                if (column.destinationLength <= 0 || row.destinationLength <= 0) {
                    continue;
                }
                pieces[count++] = {
                    {static_cast<float>(column.source), static_cast<float>(row.source), static_cast<float>(column.sourceLength), static_cast<float>(row.sourceLength)},
                    {static_cast<float>(column.destination), static_cast<float>(row.destination), static_cast<float>(column.destinationLength), static_cast<float>(row.destinationLength)}};
            }
        }
        return count;
    }

}  // namespace BECore
