#include "UIButton.h"

#include <BECore/Renderer/IRenderer.h>
#include <Events/InputEvents.h>
#include <Generated/UIButton.gen.hpp>

namespace BECore {

    namespace {

        constexpr int SelectedBrighten = 56;  // more than kHoverBrighten: a selected button under the pointer is still told from a hovered one

        unsigned char ShiftChannel(unsigned char channel, int delta) {
            return static_cast<unsigned char>(std::clamp(static_cast<int>(channel) + delta, 0, 255));
        }

    }  // namespace

    UIButton::UIButton() {
        interactive = true;
        align = TextAlign::Center;
        verticalAlign = UIVerticalAlign::Center;
        wrap = false;
    }

    Color UIButton::GetCurrentFill() const {
        return GetCurrentBox().fill;
    }

    UIButton::Look UIButton::GetCurrentBox() const {
        Look look{box, color, box.gradientColor};
        const bool styled = box.HasEffect();
        const bool off = disabled || groupDisabled;

        // The explicit colour of a state replaces the start, the far end keeps its offset from it
        const auto replaceStart = [&look](const Color& start) {
            look.gradientEnd = {ShiftChannel(look.gradientEnd.r, start.r - look.fill.r), ShiftChannel(look.gradientEnd.g, start.g - look.fill.g),
                                ShiftChannel(look.gradientEnd.b, start.b - look.fill.b), ShiftChannel(look.gradientEnd.a, start.a - look.fill.a)};
            look.fill = start;
        };
        const auto shiftBoth = [&look](int delta) {
            look.fill = ShiftColor(look.fill, delta);
            look.gradientEnd = ShiftColor(look.gradientEnd, delta);
        };

        // The look says the far end twice (the field and the member); a box without a gradient has no end to keep
        const auto keepEnd = [&look]() {
            if (look.box.gradientDirection != UIGradientDirection::None) {
                look.box.gradientColor = look.gradientEnd;
            }
        };

        if (selected && styled) {
            look.box.borderColor = textColor;
            look.box.borderWidth = std::max(look.box.borderWidth, kSelectedBorderMin);
            if (look.box.glow.color.a == 0) {
                look.box.glow = {0.0f, 0.0f, kSelectedGlowBlur, 0.0f, WithAlpha(textColor, kSelectedGlowAlpha)};
            }
        }

        if (off) {
            if (disabledColor.a > 0) {
                replaceStart(disabledColor);
            } else {
                look.fill = WithAlpha(look.fill, kDisabledAlpha);
                look.gradientEnd = WithAlpha(look.gradientEnd, kDisabledAlpha);
            }
            if (styled) {
                look.box.borderColor = WithAlpha(look.box.borderColor, kDisabledAlpha);
                look.box.highlight = WithAlpha(look.box.highlight, kDisabledAlpha);
                look.box.shadow = {};
                look.box.glow = {};
            }
            keepEnd();
            return look;
        }

        if (selected) {
            if (selectedColor.a > 0) {
                replaceStart(selectedColor);
            } else {
                look.fill = ShiftColor(look.fill, SelectedBrighten);
                look.gradientEnd = ShiftColor(look.gradientEnd, SelectedBrighten);
            }
        }
        // A press that moved off the button is not shown as pressed: letting go there does nothing
        if (pressed && hovered) {
            if (pressedColor.a > 0) {
                replaceStart(pressedColor);
            } else {
                shiftBoth(-kPressDarken);
            }
            if (styled) {
                look.box.borderColor = ShiftColor(look.box.borderColor, -kPressDarken);
                look.box.shadow.offsetY *= kPressShadowOffsetScale;
                look.box.shadow.blur *= kPressShadowBlurScale;
                look.box.highlight = {};
            }
        } else if (hovered) {
            if (hoverColor.a > 0) {
                replaceStart(hoverColor);
            } else {
                shiftBoth(kHoverBrighten);
            }
            if (styled) {
                look.box.borderColor = ShiftColor(look.box.borderColor, kHoverBrighten);
            }
        }
        keepEnd();
        return look;
    }

    void UIButton::DrawSelf(IRenderer& renderer) {
        const Look look = GetCurrentBox();
        DrawBox(renderer, look.box, look.fill, look.gradientEnd);
        if (selected && !rect.IsEmpty()) {
            const float height = std::min(kSelectedBarHeight, rect.h);
            Color bar = textColor;
            if (disabled || groupDisabled) {
                bar = WithAlpha(bar, kDisabledAlpha);
            }
            // The rounded bottom corners are left to the shape: the bar spans the straight part of the bottom edge
            const UICorners radii = box.cornerRadius.Clamped(rect.w, rect.h);
            const float left = radii.bottomLeft;
            const float width = rect.w - left - radii.bottomRight;
            if (width > 0.0f) {
                renderer.DrawFilledRect(rect.x + left, rect.y + rect.h - height, width, height, bar);
            }
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
