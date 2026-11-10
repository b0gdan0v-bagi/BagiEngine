#pragma once

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
        [[nodiscard]] static GlyphBitmap Dilate(const GlyphBitmap& glyph, int radius);
    };

}  // namespace BECore
