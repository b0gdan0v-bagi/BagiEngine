#include "TextRenderer.h"

#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Text/FontAtlas.h>

namespace BECore {

    size_t TextRenderer::Draw(IRenderer& renderer, FontAtlas& atlas, const TextLayoutResult& layout, float originX, float originY, const Color& tint) {
        if (layout.glyphs.empty()) {
            return 0;
        }

        eastl::vector<uint32_t> glyphs;
        glyphs.reserve(layout.glyphs.size());
        for (const PositionedGlyph& positioned : layout.glyphs) {
            glyphs.push_back(positioned.glyph);
        }
        atlas.Prepare(renderer, glyphs);

        const ViewTransform view = renderer.GetViewTransform();
        if (view.scale <= 0.0f) {
            return 0;
        }
        const Rect originInOutput = view.ToOutput(originX, originY, 0.0f, 0.0f);

        size_t drawn = 0;
        for (const PositionedGlyph& positioned : layout.glyphs) {
            const AtlasGlyph* glyph = atlas.Find(positioned.glyph);
            if (glyph == nullptr || glyph->page < 0) {
                continue;
            }
            ITexture* page = atlas.GetPage(glyph->page);
            if (page == nullptr) {
                continue;
            }
            // The corner of the picture in output pixels, on a whole pixel; then back to the units of the view
            const float outputX = std::round(originInOutput.x + positioned.x + glyph->offsetX);
            const float outputY = std::round(originInOutput.y + positioned.y + glyph->offsetY);
            const float unitsX = view.ToViewX(outputX);
            const float unitsY = view.ToViewY(outputY);
            renderer.DrawTexture(*page, &glyph->source, unitsX, unitsY, glyph->source.w / view.scale, glyph->source.h / view.scale, tint);
            ++drawn;
        }
        return drawn;
    }

}  // namespace BECore
