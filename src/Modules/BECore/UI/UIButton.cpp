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

    UIButton::Parts UIButton::GetParts(const Rect& area, bool hasIcon, bool hasText, UIIconPlacement placement, float iconSize) {
        Parts parts;
        const float width = std::max(0.0f, area.w);
        const float height = std::max(0.0f, area.h);
        if (!hasIcon) {
            parts.text = {area.x, area.y, width, height};
            return parts;
        }

        if (!hasText) {
            const float side = std::min(iconSize > 0.0f ? iconSize : std::min(width, height) * kIconOnlyShare, std::min(width, height));
            parts.icon = {area.x + (width - side) * 0.5f, area.y + (height - side) * 0.5f, side, side};
            return parts;
        }

        const float wanted = iconSize > 0.0f ? iconSize : height * kIconWithTextShare;
        if (placement == UIIconPlacement::Above) {
            const float padding = std::min(kIconPadding, height);
            const float side = std::max(0.0f, std::min({wanted, width, height - padding}));
            parts.icon = {area.x + (width - side) * 0.5f, area.y + padding, side, side};
            const float textTop = std::min(parts.icon.y + side + kIconGap, area.y + height);
            parts.text = {area.x, textTop, width, area.y + height - textTop};
        } else {
            const float padding = std::min(kIconPadding, width);
            const float side = std::max(0.0f, std::min({wanted, height, width - padding}));
            parts.icon = {area.x + padding, area.y + (height - side) * 0.5f, side, side};
            const float textLeft = std::min(parts.icon.x + side + kIconGap, area.x + width);
            parts.text = {textLeft, area.y, area.x + width - textLeft, height};
        }
        return parts;
    }

    Color UIButton::GetCurrentIconColor() const {
        const Color base = iconTint.a > 0 ? Color(iconTint) : Color(textColor);
        if (disabled || groupDisabled) {
            return WithAlpha(base, kDisabledAlpha);
        }
        // A press that moved off the button is not shown as pressed, as for the fill
        if (pressed && hovered) {
            return ShiftColor(base, -kPressDarken);
        }
        return hovered ? ShiftColor(base, kHoverBrighten) : base;
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
        if (icon.Empty()) {
            DrawText(renderer);
            return;
        }

        const bool hasText = !GetDisplayText().empty();
        const Parts parts = GetParts(rect, true, hasText, iconPlacement, iconSize);
        if (!parts.icon.IsEmpty()) {
            DrawIcon(renderer, parts.icon);
        }
        if (hasText) {
            DrawText(renderer, parts.text, textColor);
        }
    }

    void UIButton::DrawIcon(IRenderer& renderer, const Rect& area) {
        const Color tint = GetCurrentIconColor();
        ITexture* texture = _iconSprite.Resolve(icon);
        if (!texture || tint.a == 0) {
            return;
        }
        // The largest rect of the sprite's proportions that fits the icon rect, centred: the sprite is never squashed
        const Rect source = _iconSprite.GetSource();
        Rect target = area;
        if (!source.IsEmpty()) {
            const float scale = std::min(area.w / source.w, area.h / source.h);
            target.w = source.w * scale;
            target.h = source.h * scale;
            target.x = area.x + (area.w - target.w) * 0.5f;
            target.y = area.y + (area.h - target.h) * 0.5f;
        }
        renderer.DrawTexture(*texture, &source, target.x, target.y, target.w, target.h, tint);
    }

    UIContentFit UIButton::CheckContent(float width, float height, float scale) const {
        const bool hasText = !GetDisplayText().empty();
        if (icon.Empty() || !hasText) {
            return UILabel::CheckContent(width, height, scale);
        }
        const Parts parts = GetParts({0.0f, 0.0f, width, height}, true, true, iconPlacement, iconSize);
        if (parts.text.IsEmpty() && width > 0.0f && height > 0.0f) {
            // The icon takes the whole rect: a caption has no room at all, and nothing can shrink to nothing
            UIContentFit none;
            none.hasContent = true;
            none.fits = false;
            return none;
        }
        return UILabel::CheckContent(parts.text.w, parts.text.h, scale);
    }

    void UIButton::OnClick() {
        if (disabled || groupDisabled || action.Empty()) {
            return;
        }
        InputEvents::ActionEvent::Emit(action, value);
    }

}  // namespace BECore
