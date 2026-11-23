#pragma once

#include <BECore/Renderer/IRenderer.h>
#include <algorithm>
#include <cmath>

namespace BECore::Tests {

    /**
     * What the SDL software rasterizer pays for a triangle list (`SDL_SW_FillTriangle`): every triangle takes a temporary surface of its bounding rectangle, clipped to the
     * output, so the cost is that rectangle per triangle and not the covered area.
     */
    struct SoftwareRasterCost {
        size_t triangles = 0;
        double bboxPixels = 0.0;     ///< the sum of the bounding rectangles of all triangles, in output pixels
        double blendedPixels = 0.0;  ///< the part of it that belongs to triangles with a vertex alpha below 255

        SoftwareRasterCost& operator+=(const SoftwareRasterCost& other) {
            triangles += other.triangles;
            bboxPixels += other.bboxPixels;
            blendedPixels += other.blendedPixels;
            return *this;
        }
    };

    /// The cost of the triangles @p indices of @p vertices. @p pixelsPerUnit maps the vertex units to output pixels; a rectangle is cut to @p output (as the rasterizer cuts
    /// it) when there is one. A trailing index pair that is no whole triangle is ignored.
    [[nodiscard]] inline SoftwareRasterCost MeasureRasterCost(Span<const GeometryVertex> vertices, Span<const int> indices, float pixelsPerUnit = 1.0f, const Optional<Rect>& output = {}) {
        SoftwareRasterCost cost;
        const auto clampX = [&output](float value) { return output ? std::clamp(value, output->x, output->x + output->w) : value; };
        const auto clampY = [&output](float value) { return output ? std::clamp(value, output->y, output->y + output->h) : value; };
        for (size_t i = 0; i + 3 <= indices.size(); i += 3) {
            const GeometryVertex* corners[3] = {&vertices[static_cast<size_t>(indices[i])], &vertices[static_cast<size_t>(indices[i + 1])], &vertices[static_cast<size_t>(indices[i + 2])]};
            const float minX = std::min({corners[0]->x, corners[1]->x, corners[2]->x}) * pixelsPerUnit;
            const float maxX = std::max({corners[0]->x, corners[1]->x, corners[2]->x}) * pixelsPerUnit;
            const float minY = std::min({corners[0]->y, corners[1]->y, corners[2]->y}) * pixelsPerUnit;
            const float maxY = std::max({corners[0]->y, corners[1]->y, corners[2]->y}) * pixelsPerUnit;
            const double width = static_cast<double>(clampX(std::ceil(maxX))) - static_cast<double>(clampX(std::floor(minX)));
            const double height = static_cast<double>(clampY(std::ceil(maxY))) - static_cast<double>(clampY(std::floor(minY)));
            const double pixels = width * height;
            const bool blended = corners[0]->color.a < 255 || corners[1]->color.a < 255 || corners[2]->color.a < 255;
            ++cost.triangles;
            cost.bboxPixels += pixels;
            cost.blendedPixels += blended ? pixels : 0.0;
        }
        return cost;
    }

}  // namespace BECore::Tests
