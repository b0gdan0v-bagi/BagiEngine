#include "UITextComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIThemes.h>
#include <Generated/UITextComponent.gen.hpp>

namespace BECore {

    const eastl::string& UITextComponent::ShownText(const UIElement& owner, const eastl::string& own) {
        const eastl::string* replacement = owner.GetTextOverride();
        return replacement ? *replacement : own;
    }

    UITextParams UITextComponent::MakeParams(float fontSize, const eastl::string& shown) const {
        UITextParams params;
        params.text = eastl::string_view(shown.data(), shown.size());
        params.font = font;
        params.fontFamily = fontFamily;
        params.fontWeight = fontWeight;
        params.fontSize = fontSize;
        params.align = align;
        params.verticalAlign = verticalAlign;
        params.overflow = overflow;
        params.wrap = wrap;
        params.lineSpacing = lineSpacing;
        params.minFontSize = minFontSize;
        params.effects = textEffects;
        return params;
    }

    Color UITextComponent::GetStateColor(const UIStateLook::State& state) const {
        const auto given = [&](const UIColor& stateColor) { return stateColor.a > 0 ? Color(stateColor) : Color(textColor); };
        if (state.disabled) {
            return fadeWhenDisabled && disabledTextColor.a == 0 ? WithAlpha(textColor, kDisabledAlpha) : given(disabledTextColor);
        }
        if (state.pressed) {
            return given(pressedTextColor);
        }
        if (state.selected) {
            return given(selectedTextColor);
        }
        if (state.hovered) {
            return given(hoverTextColor);
        }
        return textColor;
    }

    void UITextComponent::ApplyTheme(UIThemes& themes) {
        themes.Resolve(textColor);
        themes.Resolve(hoverTextColor);
        themes.Resolve(pressedTextColor);
        themes.Resolve(disabledTextColor);
        themes.Resolve(selectedTextColor);
        textEffects.ResolveTheme(themes);
    }

    void UITextComponent::DrawSelf(IRenderer& renderer, UIElement& owner) {
        const Color targetColor = GetStateColor(owner.GetVisualState());
        const Color& stateColor = _colorBlend.Follow(targetColor, CoreManager::GetTime().GetTotalTime(), owner.GetStateDuration(), UIStateLook::kTransitionEase);
        _painter.Draw(renderer, MakeParams(owner.active.fontSize, ShownText(owner, text)), owner.GetComponentArea(), stateColor, textColor);
    }

    UIContentFit UITextComponent::CheckContent(const UIElement& owner, float width, float height, float scale) const {
        return _painter.Check(MakeParams(owner.active.fontSize, ShownText(owner, text)), width, height, scale);
    }

    eastl::string_view UITextComponent::GetDisplayText() const {
        return _painter.GetDisplayText(eastl::string_view(text.data(), text.size()));
    }

    eastl::string_view UITextComponent::GetDisplayText(const UIElement& owner) const {
        const eastl::string& shown = ShownText(owner, text);
        return _painter.GetDisplayText(eastl::string_view(shown.data(), shown.size()));
    }

    Vec2 UITextComponent::MeasureText(const UIElement& owner, float maxWidth, float scale) const {
        return _painter.Measure(MakeParams(owner.active.fontSize, ShownText(owner, text)), maxWidth, scale);
    }

    PoolString UITextComponent::GetResolvedFontPath() const {
        return _painter.GetResolvedFontPath(MakeParams(0.0f, text));
    }

}  // namespace BECore
