#include "GlyphOutline.h"

namespace BECore {

    namespace {

        constexpr float kEdgeReach = 0.5f;  // a source pixel still reaches the pixel this far beyond the radius, at a fraction of its coverage
        constexpr float kFullCoverage = 255.0f;

    }  // namespace

    GlyphOutline::Kernel::Kernel(int radius) : _radius(std::clamp(radius, 0, kMaxOutlinePixels)) {
        const auto side = static_cast<size_t>(2 * _radius + 1);
        _weights.resize(side * side, 0.0f);
        for (int dy = -_radius; dy <= _radius; ++dy) {
            for (int dx = -_radius; dx <= _radius; ++dx) {
                const float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                _weights[static_cast<size_t>(dy + _radius) * side + static_cast<size_t>(dx + _radius)] = std::clamp(static_cast<float>(_radius) + kEdgeReach - distance, 0.0f, 1.0f);
            }
        }
    }

    void GlyphOutline::Dilate(Span<const uint8_t> source, uint32_t width, uint32_t height, const Kernel& kernel, Span<uint8_t> target) {
        const auto radius = static_cast<uint32_t>(kernel.GetRadius());
        const uint32_t targetWidth = width + 2 * radius;
        const uint32_t targetHeight = height + 2 * radius;
        ASSERT(source.size() == static_cast<size_t>(width) * height, "the source is width * height bytes");
        ASSERT(target.size() == static_cast<size_t>(targetWidth) * targetHeight, "the target is the source grown by the radius on every side");
        if (radius == 0) {
            std::copy(source.begin(), source.end(), target.begin());
            return;
        }
        std::fill(target.begin(), target.end(), uint8_t{0});

        const Span<const float> weights = kernel.GetWeights();
        const size_t side = 2 * static_cast<size_t>(radius) + 1;
        for (uint32_t row = 0; row < height; ++row) {
            for (uint32_t column = 0; column < width; ++column) {
                const uint8_t coverage = source[static_cast<size_t>(row) * width + column];
                if (coverage == 0) {
                    continue;
                }
                // The source pixel (column, row) is the pixel (column + radius, row + radius) of the target; it reaches a square around it
                for (size_t ky = 0; ky < side; ++ky) {
                    uint8_t* line = &target[(static_cast<size_t>(row) + ky) * targetWidth + column];
                    for (size_t kx = 0; kx < side; ++kx) {
                        const float weight = weights[ky * side + kx];
                        if (weight <= 0.0f) {
                            continue;
                        }
                        const auto value = static_cast<uint8_t>(std::min(std::lround(static_cast<float>(coverage) * weight), std::lround(kFullCoverage)));
                        line[kx] = std::max(line[kx], value);
                    }
                }
            }
        }
    }

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
        result.coverage.resize(static_cast<size_t>(result.width) * result.height);

        const ScratchScope scratch;
        const Kernel kernel(radius);
        Dilate(Span<const uint8_t>(glyph.coverage.data(), glyph.coverage.size()), glyph.width, glyph.height, kernel, Span<uint8_t>(result.coverage.data(), result.coverage.size()));
        return result;
    }

}  // namespace BECore
