#include "UILabel.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Text/TextRenderer.h>
#include <Generated/UILabel.gen.hpp>

namespace BECore {

    float UILabel::GetFontSize() const {
        return active.fontSize > 0.0f ? active.fontSize : DefaultFontSize;
    }

    TextLayoutOptions UILabel::MakeOptions(float width, float height, float scale) const {
        // The text is laid out and rasterized in output pixels, so that a glyph lands on the pixels it was drawn for (the way TextComponent does it)
        TextLayoutOptions options;
        options.fontSize = static_cast<float>(FontAtlasLibrary::QuantizeSize(GetFontSize() * scale));
        options.minFontSize = minFontSize * scale;
        options.maxWidth = width * scale;
        options.maxHeight = height * scale;
        options.align = align;
        options.overflow = overflow;
        options.wrap = wrap;
        options.lineSpacing = lineSpacing;
        return options;
    }

    Font* UILabel::ResolveFont() const {
        const PoolString path = font.Empty() ? CoreManager::GetFontAtlasLibrary().GetSettings().defaultFont : font;
        if (path != _fontPath) {
            // First use, or the field was changed. A path that does not load is logged once and tried again only when it changes
            _fontPath = path;
            _fontHandle = CoreManager::GetResourceManager().Load<Font>(path.ToStringView());
            if (!_fontHandle || _fontHandle->GetState() != ResourceState::Loaded) {
                LOG_ERROR("UILabel: cannot load the font '{}'"_format(path.ToStringView()));
                _fontHandle.Reset();
            }
        }
        return _fontHandle ? _fontHandle.Get() : nullptr;
    }

    void UILabel::DrawSelf(IRenderer& renderer) {
        UIElement::DrawSelf(renderer);
        DrawText(renderer);
    }

    void UILabel::DrawText(IRenderer& renderer) {
        if (text.empty() || rect.IsEmpty() || textColor.a == 0) {
            return;
        }
        Font* resolved = ResolveFont();
        const float scale = renderer.GetViewTransform().scale;
        if (!resolved || scale <= 0.0f) {
            return;
        }

        const TextLayoutResult& layout = _layoutCache.Get(*resolved, eastl::string_view(text.data(), text.size()), MakeOptions(rect.w, rect.h, scale));
        if (layout.glyphs.empty()) {
            return;
        }
        auto atlas = CoreManager::GetFontAtlasLibrary().Get(renderer, IntrusivePtr<Font>(resolved), FontAtlasLibrary::QuantizeSize(layout.fontSize));
        if (!atlas) {
            return;
        }

        // The layout is in output pixels, the origin in the units of the canvas
        const float freeHeight = std::max(0.0f, rect.h * scale - layout.height);
        float offset = 0.0f;
        if (verticalAlign == UIVerticalAlign::Center) {
            offset = freeHeight * 0.5f;
        } else if (verticalAlign == UIVerticalAlign::Bottom) {
            offset = freeHeight;
        }

        const bool clip = layout.overflowed && overflow == TextOverflow::Clip;
        if (clip) {
            renderer.PushClipRect(renderer.GetViewTransform().ToOutput(rect.x, rect.y, rect.w, rect.h));
        }
        TextRenderer::Draw(renderer, *atlas, layout, rect.x, rect.y + offset / scale, textColor);
        if (clip) {
            renderer.PopClipRect();
        }
    }

    UIContentFit UILabel::CheckContent(float width, float height, float scale) const {
        UIContentFit fit;
        Font* resolved = text.empty() ? nullptr : ResolveFont();
        if (!resolved || width <= 0.0f || height <= 0.0f || scale <= 0.0f) {
            return fit;
        }
        const TextLayoutResult layout = TextLayout::Layout(*resolved, eastl::string_view(text.data(), text.size()), MakeOptions(width, height, scale));
        fit.hasContent = true;
        fit.fits = !layout.overflowed && !layout.truncated;
        fit.canShrink = overflow != TextOverflow::Clip;
        return fit;
    }

}  // namespace BECore
