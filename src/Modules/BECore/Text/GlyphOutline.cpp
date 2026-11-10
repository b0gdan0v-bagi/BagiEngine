#include "GlyphOutline.h"

namespace BECore {

    namespace {

        constexpr float kEdgeReach = 0.5f;  // a source pixel still reaches the pixel this far beyond the radius, at a fraction of its coverage
        constexpr float kFullCoverage = 255.0f;

        /// The weight of a source pixel at an offset from the target pixel, for every offset in the square of the radius
        eastl::vector<float> MakeKernel(int radius) {
            const int side = 2 * radius + 1;
            eastl::vector<float> kernel(static_cast<size_t>(side) * static_cast<size_t>(side), 0.0f);
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                    kernel[static_cast<size_t>(dy + radius) * static_cast<size_t>(side) + static_cast<size_t>(dx + radius)] =
                        std::clamp(static_cast<float>(radius) + kEdgeReach - distance, 0.0f, 1.0f);
                }
            }
            return kernel;
        }

    }  // namespace

    GlyphBitmap GlyphOutline::Dilate(const GlyphBitmap& glyph, int radius) {
        radius = std::min(radius, kMaxOutlinePixels);
        if (radius <= 0 || glyph.width == 0 || glyph.height == 0) {
            return glyph;
        }

        const auto grow = static_cast<uint32_t>(radius);
        GlyphBitmap result;
        result.width = glyph.width + 2 * grow;
        result.height = glyph.height + 2 * grow;
        result.offsetX = glyph.offsetX - radius;
        result.offsetY = glyph.offsetY - radius;
        result.coverage.assign(static_cast<size_t>(result.width) * result.height, 0);

        const eastl::vector<float> kernel = MakeKernel(radius);
        const size_t side = 2 * static_cast<size_t>(radius) + 1;
        for (uint32_t row = 0; row < glyph.height; ++row) {
            for (uint32_t column = 0; column < glyph.width; ++column) {
                const uint8_t source = glyph.coverage[static_cast<size_t>(row) * glyph.width + column];
                if (source == 0) {
                    continue;
                }
                // The source pixel (column, row) is the pixel (column + radius, row + radius) of the result; it reaches a square around it
                for (size_t ky = 0; ky < side; ++ky) {
                    uint8_t* target = &result.coverage[(static_cast<size_t>(row) + ky) * result.width + column];
                    for (size_t kx = 0; kx < side; ++kx) {
                        const float weight = kernel[ky * side + kx];
                        if (weight <= 0.0f) {
                            continue;
                        }
                        const auto value = static_cast<uint8_t>(std::min(std::lround(static_cast<float>(source) * weight), std::lround(kFullCoverage)));
                        target[kx] = std::max(target[kx], value);
                    }
                }
            }
        }
        return result;
    }

}  // namespace BECore
