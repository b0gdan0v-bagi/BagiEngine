#pragma once

#include <BECore/Text/TextEffects.h>
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
     *
     * With TextEffects the whole layout is drawn in up to three passes, so neighbouring glyphs never paint over each
     * other's effects: the shadow, then the outline, then the fill. The shadow uses the pictures of the outline atlas
     * when there is an outline (so it is the shape of outline and fill together), else those of the fill atlas.
     */
    class TextRenderer {
    public:
        /// Makes sure the atlas has the glyphs (new pages are made now), then draws. Returns the number of quads drawn.
        static size_t Draw(IRenderer& renderer, FontAtlas& atlas, const TextLayoutResult& layout, float originX, float originY, const Color& tint);

        /// The same with effects. @p outlineAtlas is the atlas of the same font and size with the outline pixels of the
        /// effects (null: no outline pass, whatever the effects say). Prepares the glyphs of both atlases.
        /// Clipping is the caller's. Returns the number of quads drawn in all the passes.
        static size_t Draw(IRenderer& renderer, FontAtlas& fillAtlas, FontAtlas* outlineAtlas, const TextLayoutResult& layout, float originX, float originY, const Color& tint,
                           const TextEffects& effects);
    };

}  // namespace BECore
