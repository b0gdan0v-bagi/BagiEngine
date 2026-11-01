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
        _layoutKey.font = nullptr;  // the cached layout belongs to the old font

        const PoolString path = _fontPath.Empty() ? CoreManager::GetFontAtlasLibrary().GetSettings().defaultFont : _fontPath;
        _font = CoreManager::GetResourceManager().Load<Font>(path.ToStringView());
        if (!_font || _font->GetState() != ResourceState::Loaded) {
            LOG_ERROR("TextComponent: cannot load the font '{}'"_format(path.ToStringView()));
            _font.Reset();
        }
    }

    void TextComponent::SetText(eastl::string_view text) {
        _text.assign(text.data(), text.size());
    }

    bool TextComponent::IsSame(const LayoutKey& a, const LayoutKey& b) {
        const TextLayoutOptions& x = a.options;
        const TextLayoutOptions& y = b.options;
        return a.font == b.font && a.text == b.text && x.fontSize == y.fontSize && x.maxWidth == y.maxWidth && x.maxHeight == y.maxHeight && x.align == y.align && x.lineSpacing == y.lineSpacing &&
               x.wrap == y.wrap && x.overflow == y.overflow && x.minFontSize == y.minFontSize;
    }

    void TextComponent::OnDraw() {
        const auto& renderer = CoreManager::GetRenderer();
        if (!renderer || _text.empty()) {
            return;
        }
        if (!_font && !_fontTried) {
            ReloadFont();
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
        LayoutKey key;
        key.text = _text;
        key.font = _font.operator->();
        key.options.fontSize = static_cast<float>(FontAtlasLibrary::QuantizeSize(_fontSize * scale));
        key.options.minFontSize = _minFontSize * scale;
        key.options.maxWidth = boxWidth * scale;
        key.options.maxHeight = boxHeight * scale;
        key.options.align = _align;
        key.options.overflow = _overflow;
        key.options.wrap = _wrap;
        key.options.lineSpacing = _lineSpacing;
        if (!IsSame(key, _layoutKey)) {
            _layout = TextLayout::Layout(*_font, _text, key.options);
            _layoutKey = key;
        }
        if (_layout.glyphs.empty()) {
            return;
        }

        auto atlas = CoreManager::GetFontAtlasLibrary().Get(*renderer, IntrusivePtr<Font>(_font.operator->()), FontAtlasLibrary::QuantizeSize(_layout.fontSize));
        if (!atlas) {
            return;
        }

        const bool clip = _layout.overflowed && _overflow == TextOverflow::Clip;
        if (clip) {
            renderer->PushClipRect(renderer->GetViewTransform().ToOutput(boxX, boxY, boxWidth, boxHeight));
        }
        TextRenderer::Draw(*renderer, *atlas, _layout, boxX, boxY, _color);
        if (clip) {
            renderer->PopClipRect();
        }
    }

}  // namespace BECore
