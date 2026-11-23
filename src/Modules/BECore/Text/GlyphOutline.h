#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Text/Font.h>

namespace BECore {

    /// The widest outline of a text in output pixels; wider requests are cut to it
    inline constexpr int kMaxOutlinePixels = 16;

    /**
     * @brief The picture of a glyph grown on every side, for the outline of a text. Pure CPU, no GPU.
     *
     * The result is `radius` pixels larger on every side (the offsets move by `-radius`, the width and height grow by
     * `2 * radius`). The coverage at a pixel p is the maximum over the source pixels q of
     * `coverage(q) * clamp(radius + 0.5 - distance(p, q), 0, 1)`, so the edge of the outline is soft over one pixel and
     * wherever the glyph is fully covered the result is 255: the outline picture contains the fill.
     *
     * Radius 0 (or less) returns the glyph unchanged, and so does a glyph with no ink (a space). The radius is cut to
     * kMaxOutlinePixels. The cost is the pixels of the glyph times the radius squared: an outline atlas fills lazily for that reason.
     */
    class GlyphOutline {
    public:
        /// The weight of a source pixel at every offset of the square of a radius, built once for as many glyphs as share the radius
        class Kernel {
        public:
            /// @p radius is cut to 0..kMaxOutlinePixels. The weights live in the calling thread's scratch arena: the kernel belongs to the ScratchScope
            /// that is alive when it is made and must not outlive it or cross threads
            explicit Kernel(int radius);

            [[nodiscard]] int GetRadius() const {
                return _radius;
            }
            [[nodiscard]] Span<const float> GetWeights() const {
                return Span<const float>(_weights.data(), _weights.size());
            }

        private:
            int _radius = 0;
            ArenaVector<float> _weights;
        };

        /// Dilate() of a glyph bitmap on the heap; builds its kernel for the call
        [[nodiscard]] static GlyphBitmap Dilate(const GlyphBitmap& glyph, int radius);

        /**
         * @brief Dilate() into a buffer of the caller: no allocation.
         * @p source is `width * height` coverage bytes, @p target `(width + 2 * radius) * (height + 2 * radius)` bytes (Debug asserts both);
         * it is overwritten. The result is the one of the bitmap form, byte for byte. Radius 0 copies the source.
         */
        static void Dilate(Span<const uint8_t> source, uint32_t width, uint32_t height, const Kernel& kernel, Span<uint8_t> target);
    };

}  // namespace BECore
