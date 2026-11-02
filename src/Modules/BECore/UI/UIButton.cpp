#include "UIButton.h"

#include <BECore/Renderer/IRenderer.h>
#include <Events/InputEvents.h>
#include <Generated/UIButton.gen.hpp>

namespace BECore {

    namespace {

        constexpr int HoverBrighten = 28;
        constexpr int PressDarken = 36;
        constexpr float DisabledAlpha = 0.5f;

        unsigned char ShiftChannel(unsigned char channel, int delta) {
            return static_cast<unsigned char>(std::clamp(static_cast<int>(channel) + delta, 0, 255));
        }

        Color Shift(const Color& color, int delta) {
            return {ShiftChannel(color.r, delta), ShiftChannel(color.g, delta), ShiftChannel(color.b, delta), color.a};
        }

    }  // namespace

    UIButton::UIButton() {
        interactive = true;
        align = TextAlign::Center;
        verticalAlign = UIVerticalAlign::Center;
        wrap = false;
    }

    Color UIButton::GetCurrentFill() const {
        if (disabled) {
            if (disabledColor.a > 0) {
                return disabledColor;
            }
            return {color.r, color.g, color.b, static_cast<unsigned char>(static_cast<float>(color.a) * DisabledAlpha)};
        }
        // A press that moved off the button is not shown as pressed: letting go there does nothing
        if (pressed && hovered) {
            return pressedColor.a > 0 ? Color(pressedColor) : Shift(color, -PressDarken);
        }
        if (hovered) {
            return hoverColor.a > 0 ? Color(hoverColor) : Shift(color, HoverBrighten);
        }
        return color;
    }

    void UIButton::DrawSelf(IRenderer& renderer) {
        const Color fill = GetCurrentFill();
        if (fill.a > 0 && !rect.IsEmpty()) {
            renderer.DrawFilledRect(rect.x, rect.y, rect.w, rect.h, fill);
        }
        DrawText(renderer);
    }

    void UIButton::OnClick() {
        if (disabled || action.Empty()) {
            return;
        }
        InputEvents::ActionEvent::Emit(action);
    }

}  // namespace BECore
