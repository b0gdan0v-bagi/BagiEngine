#include "TextRenderer.h"

#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Text/FontAtlas.h>

namespace BECore {

    namespace {

        /// One pass over the whole layout: a quad per inked glyph, moved by (shiftX, shiftY) output pixels
        size_t DrawPass(IRenderer& renderer, const FontAtlas& atlas, const TextLayoutResult& layout, const ViewTransform& view, const Rect& originInOutput, float shiftX, float shiftY,
                        const Color& tint) {
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
                const float outputX = std::round(originInOutput.x + shiftX + positioned.x + glyph->offsetX);
                const float outputY = std::round(originInOutput.y + shiftY + positioned.y + glyph->offsetY);
                const float unitsX = view.ToViewX(outputX);
                const float unitsY = view.ToViewY(outputY);
                renderer.DrawTexture(*page, &glyph->source, unitsX, unitsY, glyph->source.w / view.scale, glyph->source.h / view.scale, tint);
                ++drawn;
            }
            return drawn;
        }

        /// Prepare() of the glyphs of the layout, but only when the atlas lacks one of them: a text drawn again (every frame) finds them all
        /// and builds no list, so a frame at rest does not allocate here
        void PrepareLayout(IRenderer& renderer, FontAtlas& atlas, const TextLayoutResult& layout) {
            const bool complete = eastl::all_of(layout.glyphs.begin(), layout.glyphs.end(), [&atlas](const PositionedGlyph& positioned) { return atlas.Find(positioned.glyph) != nullptr; });
            if (complete) {
                return;
            }
            eastl::vector<uint32_t> glyphs;
            glyphs.reserve(layout.glyphs.size());
            for (const PositionedGlyph& positioned : layout.glyphs) {
                glyphs.push_back(positioned.glyph);
            }
            atlas.Prepare(renderer, glyphs);
        }

    }  // namespace

    size_t TextRenderer::Draw(IRenderer& renderer, FontAtlas& atlas, const TextLayoutResult& layout, float originX, float originY, const Color& tint) {
        return Draw(renderer, atlas, nullptr, layout, originX, originY, tint, TextEffects{});
    }

    size_t TextRenderer::Draw(IRenderer& renderer, FontAtlas& fillAtlas, FontAtlas* outlineAtlas, const TextLayoutResult& layout, float originX, float originY, const Color& tint,
                              const TextEffects& effects) {
        BE_PROFILE_SCOPE("TextRenderer::Draw");
        if (layout.glyphs.empty()) {
            return 0;
        }

        PrepareLayout(renderer, fillAtlas, layout);
        const bool outlined = outlineAtlas != nullptr && effects.HasOutline();
        if (outlined) {
            PrepareLayout(renderer, *outlineAtlas, layout);
        }

        const ViewTransform view = renderer.GetViewTransform();
        if (view.scale <= 0.0f) {
            return 0;
        }
        const Rect originInOutput = view.ToOutput(originX, originY, 0.0f, 0.0f);

        size_t drawn = 0;
        if (effects.HasShadow()) {
            const FontAtlas& shadowAtlas = outlined ? *outlineAtlas : fillAtlas;
            drawn += DrawPass(renderer, shadowAtlas, layout, view, originInOutput, effects.shadowOffsetX, effects.shadowOffsetY, effects.shadowColor);
        }
        if (outlined) {
            drawn += DrawPass(renderer, *outlineAtlas, layout, view, originInOutput, 0.0f, 0.0f, effects.outlineColor);
        }
        drawn += DrawPass(renderer, fillAtlas, layout, view, originInOutput, 0.0f, 0.0f, tint);
        return drawn;
    }

}  // namespace BECore
