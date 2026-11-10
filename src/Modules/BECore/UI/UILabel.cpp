#include "UILabel.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Text/TextRenderer.h>
#include <Generated/UILabel.gen.hpp>

namespace BECore {

    eastl::string_view UILabel::GetDisplayText() const {
        const eastl::string& source = GetSourceText();
        if (!UIVars::HasPlaceholders(source)) {
            return eastl::string_view(source.data(), source.size());
        }
        const UIVars& variables = CoreManager::GetUIVars();
        UIStrings& strings = CoreManager::GetUIStrings();
        // A text that names no string does not depend on the string table, so a language switch leaves it alone
        const bool stale = _expandedSource != source || _expandedVersion != variables.GetVersion() || (_expandedUsesStrings && _expandedStringsVersion != strings.GetVersion());
        if (stale) {
            const eastl::string_view sourceView(source.data(), source.size());
            _expandedUsesStrings = UIStrings::HasReferences(sourceView);
            if (_expandedUsesStrings) {
                // Strings first, variables second: a translation may hold `{score}`, and a string is not resolved again
                const size_t missingBefore = strings.GetMissing().size();
                const eastl::string resolved = strings.Resolve(sourceView);
                const auto& missing = strings.GetMissing();
                for (size_t i = missingBefore; i < missing.size(); ++i) {
                    LOG_WARNING("UILabel: no string '{}' in the language '{}' or the default one"_format(missing[i].key.c_str(), missing[i].language.ToStringView()));
                }
                _expanded = variables.Expand(eastl::string_view(resolved.data(), resolved.size()));
                _expandedStringsVersion = strings.GetVersion();
            } else {
                _expanded = variables.Expand(sourceView);
            }
            _expandedSource = source;
            _expandedVersion = variables.GetVersion();
        }
        return eastl::string_view(_expanded.data(), _expanded.size());
    }

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
        DrawText(renderer, rect, textColor);
    }

    void UILabel::DrawText(IRenderer& renderer, const Rect& area, const Color& color) {
        const eastl::string_view shown = GetDisplayText();
        if (shown.empty() || area.IsEmpty() || color.a == 0) {
            return;
        }
        Font* resolved = ResolveFont();
        const float scale = renderer.GetViewTransform().scale;
        if (!resolved || scale <= 0.0f) {
            return;
        }

        const TextLayoutResult& layout = _layoutCache.Get(*resolved, shown, MakeOptions(area.w, area.h, scale));
        if (layout.glyphs.empty()) {
            return;
        }
        auto atlas = CoreManager::GetFontAtlasLibrary().Get(renderer, IntrusivePtr<Font>(resolved), FontAtlasLibrary::QuantizeSize(layout.fontSize));
        if (!atlas) {
            return;
        }

        // The layout is in output pixels, the origin in the units of the canvas
        const float freeHeight = std::max(0.0f, area.h * scale - layout.height);
        float offset = 0.0f;
        if (verticalAlign == UIVerticalAlign::Center) {
            offset = freeHeight * 0.5f;
        } else if (verticalAlign == UIVerticalAlign::Bottom) {
            offset = freeHeight;
        }

        const bool clip = layout.overflowed && overflow == TextOverflow::Clip;
        if (clip) {
            renderer.PushClipRect(renderer.GetViewTransform().ToOutput(area.x, area.y, area.w, area.h));
        }
        TextRenderer::Draw(renderer, *atlas, layout, area.x, area.y + offset / scale, color);
        if (clip) {
            renderer.PopClipRect();
        }
    }

    UIContentFit UILabel::CheckContent(float width, float height, float scale) const {
        UIContentFit fit;
        const eastl::string_view shown = GetDisplayText();
        Font* resolved = shown.empty() ? nullptr : ResolveFont();
        if (!resolved || width <= 0.0f || height <= 0.0f || scale <= 0.0f) {
            return fit;
        }
        const TextLayoutResult layout = TextLayout::Layout(*resolved, shown, MakeOptions(width, height, scale));
        fit.hasContent = true;
        fit.fits = !layout.overflowed && !layout.truncated;
        fit.canShrink = overflow != TextOverflow::Clip;
        return fit;
    }

}  // namespace BECore
