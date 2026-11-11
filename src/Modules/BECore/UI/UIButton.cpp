#include "UIButton.h"

#include <BECore/Renderer/IRenderer.h>
#include <Events/InputEvents.h>
#include <Generated/UIButton.gen.hpp>

namespace BECore {

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
        return GetStateLook();
    }

    bool UIButton::IsDisabled() const {
        return disabled || groupDisabled;
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
        return UIStateLook::ResolveTint(base, {}, GetVisualState());
    }

    void UIButton::DrawSelf(IRenderer& renderer) {
        DrawStateBox(renderer);
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
        const Rect source = _iconSprite.GetSource();
        const Rect target = UISprite::FitInto(area, source);
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
        if (IsDisabled() || action.Empty()) {
            return;
        }
        InputEvents::ActionEvent::Emit(action, value);
    }

}  // namespace BECore
