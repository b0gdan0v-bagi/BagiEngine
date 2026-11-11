#include "UITextPainter.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Text/TextRenderer.h>

namespace BECore {

    namespace {

        TextLayoutOptions MakeOptions(const UITextParams& params, float width, float height, float scale) {
            // The text is laid out and rasterized in output pixels, so that a glyph lands on the pixels it was drawn for (the way TextComponent does it)
            TextLayoutOptions options;
            options.fontSize = static_cast<float>(FontAtlasLibrary::QuantizeSize(UITextPainter::EffectiveFontSize(params.fontSize) * scale));
            options.minFontSize = params.minFontSize * scale;
            options.maxWidth = width * scale;
            options.maxHeight = height * scale;
            options.align = params.align;
            options.overflow = params.overflow;
            options.wrap = params.wrap;
            options.lineSpacing = params.lineSpacing;
            return options;
        }

    }  // namespace

    float UITextPainter::EffectiveFontSize(float fontSize) {
        return fontSize > 0.0f ? fontSize : kDefaultFontSize;
    }

    eastl::string_view UITextPainter::GetDisplayText(eastl::string_view source) {
        if (!UIVars::HasPlaceholders(source)) {
            return source;
        }
        const UIVars& variables = CoreManager::GetUIVars();
        UIStrings& strings = CoreManager::GetUIStrings();
        // A text that names no string does not depend on the string table, so a language switch leaves it alone
        const bool stale = eastl::string_view(_expandedSource.data(), _expandedSource.size()) != source || _expandedVersion != variables.GetVersion() ||
                           (_expandedUsesStrings && _expandedStringsVersion != strings.GetVersion());
        if (stale) {
            _expandedUsesStrings = UIStrings::HasReferences(source);
            if (_expandedUsesStrings) {
                // Strings first, variables second: a translation may hold `{score}`, and a string is not resolved again
                const size_t missingBefore = strings.GetMissing().size();
                const eastl::string resolved = strings.Resolve(source);
                const auto& missing = strings.GetMissing();
                for (size_t i = missingBefore; i < missing.size(); ++i) {
                    LOG_WARNING("UIText: no string '{}' in the language '{}' or the default one"_format(missing[i].key.c_str(), missing[i].language.ToStringView()));
                }
                _expanded = variables.Expand(eastl::string_view(resolved.data(), resolved.size()));
                _expandedStringsVersion = strings.GetVersion();
            } else {
                _expanded = variables.Expand(source);
            }
            _expandedSource.assign(source.data(), source.size());
            _expandedVersion = variables.GetVersion();
        }
        return eastl::string_view(_expanded.data(), _expanded.size());
    }

    Font* UITextPainter::ResolveFont(const UITextParams& params) {
        const FontRequest request{params.font, params.fontFamily, params.fontWeight};
        bool known = true;
        const PoolString path = CoreManager::GetFontAtlasLibrary().ResolveFontPath(request, &known);
        if (request != _fontRequest) {
            _fontRequest = request;
            if (!known) {
                LOG_WARNING("UIText: no font family '{}' in TextConfig.xml, using the default font"_format(params.fontFamily.ToStringView()));
            }
        }
        if (path != _fontPath) {
            // First use, or a field was changed. A path that does not load is logged once and tried again only when it changes
            _fontPath = path;
            _fontHandle = CoreManager::GetResourceManager().Load<Font>(path.ToStringView());
            if (!_fontHandle || _fontHandle->GetState() != ResourceState::Loaded) {
                LOG_ERROR("UIText: cannot load the font '{}'"_format(path.ToStringView()));
                _fontHandle.Reset();
            }
        }
        return _fontHandle ? _fontHandle.Get() : nullptr;
    }

    PoolString UITextPainter::GetResolvedFontPath(const UITextParams& params) {
        return ResolveFont(params) ? _fontPath : PoolString{};
    }

    void UITextPainter::Draw(IRenderer& renderer, const UITextParams& params, const Rect& area, const Color& color, const Color& fullColor) {
        const eastl::string_view shown = GetDisplayText(params.text);
        if (shown.empty() || area.IsEmpty() || color.a == 0) {
            return;
        }
        Font* resolved = ResolveFont(params);
        const float scale = renderer.GetViewTransform().scale;
        if (!resolved || scale <= 0.0f) {
            return;
        }

        const TextLayoutResult& layout = _layoutCache.Get(*resolved, shown, MakeOptions(params, area.w, area.h, scale));
        if (layout.glyphs.empty()) {
            return;
        }
        const int pixelSize = FontAtlasLibrary::QuantizeSize(layout.fontSize);
        auto atlas = CoreManager::GetFontAtlasLibrary().Get(renderer, IntrusivePtr<Font>(resolved), pixelSize);
        if (!atlas) {
            return;
        }

        // A control that fades its text (a disabled toggle) passes the faded colour: the effects fade by the same share, so no strong outline stays round faded text
        const float fade = fullColor.a > 0 ? std::min(1.0f, static_cast<float>(color.a) / static_cast<float>(fullColor.a)) : 1.0f;
        const TextEffects effects = params.effects.ToPixels(scale, fade);
        IntrusivePtr<FontAtlas> outlineAtlas;
        if (effects.HasOutline()) {
            outlineAtlas = CoreManager::GetFontAtlasLibrary().Get(renderer, IntrusivePtr<Font>(resolved), pixelSize, static_cast<int>(effects.outlineWidth));
        }

        // The layout is in output pixels, the origin in the units of the canvas
        const float freeHeight = std::max(0.0f, area.h * scale - layout.height);
        float offset = 0.0f;
        if (params.verticalAlign == UIVerticalAlign::Center) {
            offset = freeHeight * 0.5f;
        } else if (params.verticalAlign == UIVerticalAlign::Bottom) {
            offset = freeHeight;
        }

        const bool clip = layout.overflowed && params.overflow == TextOverflow::Clip;
        if (clip) {
            renderer.PushClipRect(renderer.GetViewTransform().ToOutput(area.x, area.y, area.w, area.h));
        }
        TextRenderer::Draw(renderer, *atlas, outlineAtlas.Get(), layout, area.x, area.y + offset / scale, color, effects);
        if (clip) {
            renderer.PopClipRect();
        }
    }

    Vec2 UITextPainter::Measure(const UITextParams& params, float maxWidth, float scale) {
        const eastl::string_view shown = GetDisplayText(params.text);
        Font* resolved = shown.empty() ? nullptr : ResolveFont(params);
        if (!resolved || maxWidth <= 0.0f || scale <= 0.0f) {
            return {};
        }
        // No height limit: the text is as tall as its lines
        const TextLayoutResult layout = TextLayout::Layout(*resolved, shown, MakeOptions(params, maxWidth, 0.0f, scale));
        return {layout.width / scale, layout.height / scale};
    }

    UIContentFit UITextPainter::Check(const UITextParams& params, float width, float height, float scale) {
        UIContentFit fit;
        const eastl::string_view shown = GetDisplayText(params.text);
        Font* resolved = shown.empty() ? nullptr : ResolveFont(params);
        if (!resolved || width <= 0.0f || height <= 0.0f || scale <= 0.0f) {
            return fit;
        }
        const TextLayoutResult layout = TextLayout::Layout(*resolved, shown, MakeOptions(params, width, height, scale));
        fit.hasContent = true;
        fit.fits = !layout.overflowed && !layout.truncated;
        fit.canShrink = params.overflow != TextOverflow::Clip;
        return fit;
    }

}  // namespace BECore
