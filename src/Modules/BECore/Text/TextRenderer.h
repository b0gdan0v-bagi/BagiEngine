#pragma once

#include <BECore/Text/TextLayout.h>

namespace BECore {

    class FontAtlas;
    class IRenderer;

    /**
     * @brief Draws a TextLayoutResult through IRenderer::DrawTexture(), one quad per glyph.
     *
     * The layout is in output pixels: it was made with the effective pixel size (font size times the scale of
     * IRenderer::GetViewTransform()), and the atlas is the one of that size, so a glyph is drawn 1:1. @p originX and
     * @p originY are the top-left corner of the text box in the units of the view transform. Each glyph is snapped to a
     * whole output pixel, which keeps the text sharp. The tint is the text color (white glyphs times the tint).
     */
    class TextRenderer {
    public:
        /// Makes sure the atlas has the glyphs (new pages are made now), then draws. Returns the number of quads drawn.
        static size_t Draw(IRenderer& renderer, FontAtlas& atlas, const TextLayoutResult& layout, float originX, float originY, const Color& tint);
    };

}  // namespace BECore
