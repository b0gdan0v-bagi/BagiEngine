#include "UISwitchComponent.h"

#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIThemes.h>
#include <Generated/UISwitchComponent.gen.hpp>

namespace BECore {

    namespace {

        constexpr Color kWhite{255, 255, 255, 255};  // what the off track and the thumb derive from: the colour of a caption nobody coloured

    }  // namespace

    UISwitchComponent::Parts UISwitchComponent::GetParts(const Rect& area) {
        Parts parts;
        const float switchHeight = area.h * kSwitchHeightRatio;
        const float switchWidth = std::min(switchHeight * kSwitchAspect, std::max(0.0f, area.w));
        const float margin = (area.h - switchHeight) * 0.5f;  // the same air at the right end as above and below the track

        parts.track = {std::max(area.x, area.x + area.w - margin - switchWidth), area.y + margin, switchWidth, switchHeight};

        const float thumb = std::max(0.0f, std::min(switchHeight, switchWidth) - 2.0f * kThumbInset);
        const float thumbY = parts.track.y + (switchHeight - thumb) * 0.5f;
        parts.thumbOff = {parts.track.x + kThumbInset, thumbY, thumb, thumb};
        parts.thumbOn = {parts.track.x + parts.track.w - kThumbInset - thumb, thumbY, thumb, thumb};
        return parts;
    }

    Color UISwitchComponent::GetCurrentTrack(const UIStateLook::State& state) const {
        Color base;
        if (state.checked) {
            base = fillColor.a > 0 ? Color(fillColor) : kFillFallback;
        } else {
            base = trackColor.a > 0 ? Color(trackColor) : WithAlpha(kWhite, kOffTrackAlpha);
        }
        if (state.disabled) {
            return WithAlpha(base, kDisabledAlpha);
        }
        if (state.pressed) {
            return ShiftColor(base, -kPressDarken);
        }
        if (state.hovered) {
            return ShiftColor(base, kHoverBrighten);
        }
        return base;
    }

    Color UISwitchComponent::GetCurrentThumb(const UIStateLook::State& state) const {
        const Color base = thumbColor.a > 0 ? Color(thumbColor) : kWhite;
        return state.disabled ? WithAlpha(base, kDisabledAlpha) : base;
    }

    void UISwitchComponent::ApplyTheme(UIThemes& themes) {
        themes.Resolve(trackColor);
        themes.Resolve(fillColor);
        themes.Resolve(thumbColor);
    }

    void UISwitchComponent::DrawSelf(IRenderer& renderer, UIElement& owner) {
        if (owner.rect.IsEmpty()) {
            return;
        }
        const UIStateLook::State state = owner.GetVisualState();
        const Parts parts = GetParts(owner.rect);
        if (!parts.track.IsEmpty()) {
            owner.DrawPart(renderer, parts.track, GetCurrentTrack(state), partShape);
        }
        const Rect& thumb = state.checked ? parts.thumbOn : parts.thumbOff;
        if (!thumb.IsEmpty()) {
            owner.DrawPart(renderer, thumb, GetCurrentThumb(state), partShape);
        }
    }

}  // namespace BECore
