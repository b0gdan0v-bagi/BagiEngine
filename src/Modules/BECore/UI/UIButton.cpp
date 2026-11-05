#include "UIButton.h"

#include <BECore/Renderer/IRenderer.h>
#include <Events/InputEvents.h>
#include <Generated/UIButton.gen.hpp>

namespace BECore {

    namespace {

        constexpr int SelectedBrighten = 56;  // more than kHoverBrighten: a selected button under the pointer is still told from a hovered one

    }  // namespace

    UIButton::UIButton() {
        interactive = true;
        align = TextAlign::Center;
        verticalAlign = UIVerticalAlign::Center;
        wrap = false;
    }

    Color UIButton::GetCurrentFill() const {
        if (disabled || groupDisabled) {
            if (disabledColor.a > 0) {
                return disabledColor;
            }
            return WithAlpha(color, kDisabledAlpha);
        }
        // A press that moved off the button is not shown as pressed: letting go there does nothing
        Color base = color;
        if (selected) {
            base = selectedColor.a > 0 ? Color(selectedColor) : ShiftColor(color, SelectedBrighten);
        }
        if (pressed && hovered) {
            return pressedColor.a > 0 ? Color(pressedColor) : ShiftColor(base, -kPressDarken);
        }
        if (hovered) {
            return hoverColor.a > 0 ? Color(hoverColor) : ShiftColor(base, kHoverBrighten);
        }
        return base;
    }

    void UIButton::DrawSelf(IRenderer& renderer) {
        const Color fill = GetCurrentFill();
        if (fill.a > 0 && !rect.IsEmpty()) {
            renderer.DrawFilledRect(rect.x, rect.y, rect.w, rect.h, fill);
        }
        if (selected && !rect.IsEmpty()) {
            const float height = std::min(kSelectedBarHeight, rect.h);
            Color bar = textColor;
            if (disabled || groupDisabled) {
                bar = WithAlpha(bar, kDisabledAlpha);
            }
            renderer.DrawFilledRect(rect.x, rect.y + rect.h - height, rect.w, height, bar);
        }
        DrawText(renderer);
    }

    void UIButton::OnClick() {
        if (disabled || groupDisabled || action.Empty()) {
            return;
        }
        InputEvents::ActionEvent::Emit(action, value);
    }

}  // namespace BECore
