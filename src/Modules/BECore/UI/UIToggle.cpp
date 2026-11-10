#include "UIToggle.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <Events/InputEvents.h>
#include <Generated/UIToggle.gen.hpp>

namespace BECore {

    namespace {

        constexpr float OffTrackAlpha = 0.35f;  // of the text color: a track that is there without shouting

    }  // namespace

    UIToggle::UIToggle() {
        interactive = true;
        verticalAlign = UIVerticalAlign::Center;
        wrap = false;
        onValue = PoolString::Intern("true");
        offValue = PoolString::Intern("false");
    }

    UIToggle::Parts UIToggle::GetParts(const Rect& area) {
        Parts parts;
        const float switchHeight = area.h * kSwitchHeightRatio;
        const float switchWidth = std::min(switchHeight * kSwitchAspect, std::max(0.0f, area.w));
        const float margin = (area.h - switchHeight) * 0.5f;  // the same air at the right end as above and below the track

        parts.track = {std::max(area.x, area.x + area.w - margin - switchWidth), area.y + margin, switchWidth, switchHeight};

        const float thumb = std::max(0.0f, std::min(switchHeight, switchWidth) - 2.0f * kThumbInset);
        const float thumbY = parts.track.y + (switchHeight - thumb) * 0.5f;
        parts.thumbOff = {parts.track.x + kThumbInset, thumbY, thumb, thumb};
        parts.thumbOn = {parts.track.x + parts.track.w - kThumbInset - thumb, thumbY, thumb, thumb};

        const float textWidth = std::max(0.0f, parts.track.x - kCaptionGap - area.x);
        parts.text = {area.x, area.y, textWidth, area.h};
        return parts;
    }

    bool UIToggle::IsOn() const {
        if (bind.Empty() || onValue.Empty()) {
            return false;
        }
        return CoreManager::GetUIVars().Get(bind) == onValue.ToStringView();
    }

    Color UIToggle::GetCurrentTrack() const {
        Color base;
        if (IsOn()) {
            base = fillColor.a > 0 ? Color(fillColor) : kFillFallback;
        } else {
            base = trackColor.a > 0 ? Color(trackColor) : WithAlpha(textColor, OffTrackAlpha);
        }
        if (disabled) {
            return WithAlpha(base, kDisabledAlpha);
        }
        if (pressed && hovered) {
            return ShiftColor(base, -kPressDarken);
        }
        if (hovered) {
            return ShiftColor(base, kHoverBrighten);
        }
        return base;
    }

    Color UIToggle::GetCurrentThumb() const {
        const Color base = thumbColor.a > 0 ? Color(thumbColor) : Color(textColor);
        return disabled ? WithAlpha(base, kDisabledAlpha) : base;
    }

    void UIToggle::DrawSelf(IRenderer& renderer) {
        if (rect.IsEmpty()) {
            return;
        }
        // The plate is the box of every element (a rounded, bordered or graded one is drawn); the default is one DrawFilledRect of `color`
        DrawBox(renderer, box, disabled ? WithAlpha(color, kDisabledAlpha) : Color(color), disabled ? WithAlpha(box.gradientColor, kDisabledAlpha) : Color(box.gradientColor));

        const Parts parts = GetParts(rect);
        DrawText(renderer, parts.text, disabled ? WithAlpha(textColor, kDisabledAlpha) : Color(textColor));

        if (!parts.track.IsEmpty()) {
            DrawPart(renderer, parts.track, GetCurrentTrack(), partShape);
        }
        const Rect& thumb = IsOn() ? parts.thumbOn : parts.thumbOff;
        if (!thumb.IsEmpty()) {
            DrawPart(renderer, thumb, GetCurrentThumb(), partShape);
        }
    }

    void UIToggle::OnClick() {
        if (disabled || action.Empty()) {
            return;
        }
        InputEvents::ActionEvent::Emit(action, IsOn() ? offValue : onValue);
    }

    UIContentFit UIToggle::CheckContent(float width, float height, float scale) const {
        const Parts parts = GetParts({0.0f, 0.0f, width, height});
        if (parts.text.w <= 0.0f && !GetDisplayText().empty() && width > 0.0f && height > 0.0f) {
            // The switch takes the whole width: a caption has no room at all, and nothing can shrink to nothing
            UIContentFit none;
            none.hasContent = true;
            none.fits = false;
            return none;
        }
        return UILabel::CheckContent(parts.text.w, parts.text.h, scale);
    }

}  // namespace BECore
