#include "TextComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Text/TextRenderer.h>
#include <Generated/TextComponent.gen.hpp>

namespace BECore {

    void TextComponent::OnAttached() {
        ReloadFont();
    }

    void TextComponent::ReloadFont() {
        _fontTried = true;
        _font.Reset();
        _layoutCache.Reset();  // the cached layout belongs to the old font

        _fontRequest = FontRequest{_fontPath, _fontFamily, _fontWeight};
        bool known = true;
        const PoolString path = CoreManager::GetFontAtlasLibrary().ResolveFontPath(_fontRequest, &known);
        if (!known) {
            LOG_WARNING("TextComponent: no font family '{}' in TextConfig.xml, using the default font"_format(_fontFamily.ToStringView()));
        }
        _font = CoreManager::GetResourceManager().Load<Font>(path.ToStringView());
        if (!_font || _font->GetState() != ResourceState::Loaded) {
            LOG_ERROR("TextComponent: cannot load the font '{}'"_format(path.ToStringView()));
            _font.Reset();
        }
    }

    void TextComponent::SetText(eastl::string_view text) {
        _text.assign(text.data(), text.size());
    }

    void TextComponent::OnDraw() {
        BE_PROFILE_SCOPE("TextComponent::OnDraw");
        const auto& renderer = CoreManager::GetRenderer();
        if (!renderer || _text.empty()) {
            return;
        }
        if (!_fontTried || _fontRequest != FontRequest{_fontPath, _fontFamily, _fontWeight}) {
            ReloadFont();  // first draw, or the path, family or weight changed
        }
        if (!_font) {
            return;
        }

        float boxX = 0.0f;
        float boxY = 0.0f;
        float boxWidth = 0.0f;
        float boxHeight = 0.0f;
        if (auto transform = GetNode().GetComponent<TransformComponent>()) {
            boxX = transform->_x;
            boxY = transform->_y;
            boxWidth = transform->_width;
            boxHeight = transform->_height;
        }

        // The text is laid out and rasterized in output pixels, so that a glyph lands on the pixels it was drawn for
        const float scale = renderer->GetViewTransform().scale;
        if (scale <= 0.0f) {
            return;
        }
        TextLayoutOptions options;
        options.fontSize = static_cast<float>(FontAtlasLibrary::QuantizeSize(_fontSize * scale));
        options.minFontSize = _minFontSize * scale;
        options.maxWidth = boxWidth * scale;
        options.maxHeight = boxHeight * scale;
        options.align = _align;
        options.overflow = _overflow;
        options.wrap = _wrap;
        options.lineSpacing = _lineSpacing;
        const TextLayoutResult& layout = _layoutCache.Get(*_font, _text, options);
        if (layout.glyphs.empty()) {
            return;
        }

        const int pixelSize = FontAtlasLibrary::QuantizeSize(layout.fontSize);
        auto atlas = CoreManager::GetFontAtlasLibrary().Get(*renderer, IntrusivePtr<Font>(_font.operator->()), pixelSize);
        if (!atlas) {
            return;
        }

        TextEffects effects;
        effects.outlineColor = _outlineColor;
        effects.shadowColor = _shadowColor;
        if (_outlineWidth > 0.0f && _outlineColor.a > 0) {
            effects.outlineWidth = static_cast<float>(FontAtlasLibrary::QuantizeOutline(_outlineWidth * scale));
        }
        effects.shadowOffsetX = std::round(_shadowOffsetX * scale);
        effects.shadowOffsetY = std::round(_shadowOffsetY * scale);
        IntrusivePtr<FontAtlas> outlineAtlas;
        if (effects.HasOutline()) {
            outlineAtlas = CoreManager::GetFontAtlasLibrary().Get(*renderer, IntrusivePtr<Font>(_font.operator->()), pixelSize, static_cast<int>(effects.outlineWidth));
        }

        const bool clip = layout.overflowed && _overflow == TextOverflow::Clip;
        if (clip) {
            renderer->PushClipRect(renderer->GetViewTransform().ToOutput(boxX, boxY, boxWidth, boxHeight));
        }
        TextRenderer::Draw(*renderer, *atlas, outlineAtlas.Get(), layout, boxX, boxY, _color, effects);
        if (clip) {
            renderer->PopClipRect();
        }
    }

}  // namespace BECore
