#include "UISlider.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/UI/UIProgressBar.h>
#include <BECore/UI/UIThemes.h>
#include <Events/InputEvents.h>
#include <Generated/UISlider.gen.hpp>

namespace BECore {

    UISlider::UISlider() {
        interactive = true;
    }

    UISlider::Parts UISlider::GetParts(const Rect& area, float fraction) {
        return UISliderComponent::GetParts(area, fraction);
    }

    float UISlider::GetValue() const {
        if (bind.Empty()) {
            return minValue;
        }
        return UIText::ValueInRange(CoreManager::GetUIVars().Get(bind), minValue, maxValue);
    }

    float UISlider::GetFraction() const {
        return UIText::FractionInRange(GetValue(), minValue, maxValue);
    }

    float UISlider::ValueAt(float canvasX) const {
        return UISliderComponent::ValueAtX(rect, minValue, maxValue, step, canvasX);
    }

    eastl::string UISlider::FormatValue(float value) {
        return UISliderComponent::FormatValue(value);
    }

    Color UISlider::GetCurrentTrack() const {
        const Color base = trackColor.a > 0 ? Color(trackColor) : UIProgressBar::kTrackFallback;
        return disabled ? WithAlpha(base, kDisabledAlpha) : base;
    }

    Color UISlider::GetCurrentFill() const {
        const Color base = fillColor.a > 0 ? Color(fillColor) : kFillFallback;
        return disabled ? WithAlpha(base, kDisabledAlpha) : base;
    }

    Color UISlider::GetCurrentThumb() const {
        const Color base = thumbColor.a > 0 ? Color(thumbColor) : kThumbFallback;
        if (disabled) {
            return WithAlpha(base, kDisabledAlpha);
        }
        // The press is held wherever the pointer went, so it does not need the pointer over the slider
        if (pressed) {
            return ShiftColor(base, -kPressDarken);
        }
        return hovered ? ShiftColor(base, kHoverBrighten) : base;
    }

    void UISlider::ApplyTheme(UIThemes& themes) {
        UIElement::ApplyTheme(themes);
        themes.Resolve(trackColor);
        themes.Resolve(fillColor);
        themes.Resolve(thumbColor);
    }

    void UISlider::DrawSelf(IRenderer& renderer) {
        UIElement::DrawSelf(renderer);
        if (rect.IsEmpty()) {
            return;
        }
        const Parts parts = GetParts(rect, GetFraction());
        DrawPart(renderer, parts.track, GetCurrentTrack(), partShape);
        if (parts.filled.w > 0.0f) {
            DrawPart(renderer, parts.filled, GetCurrentFill(), partShape);
        }
        if (!parts.thumb.IsEmpty()) {
            DrawPart(renderer, parts.thumb, GetCurrentThumb(), partShape);
        }
    }

    void UISlider::Follow(float x) {
        if (disabled || action.Empty()) {
            return;
        }
        const PoolString text = PoolString::Intern(FormatValue(ValueAt(x)));
        if (text == _lastEmitted) {
            return;
        }
        _lastEmitted = text;
        InputEvents::ActionEvent::Emit(action, text);
    }

    void UISlider::OnPress(float x, float /*y*/) {
        _lastEmitted = PoolString{};  // a new gesture always emits, whatever the last one ended on
        Follow(x);
    }

    void UISlider::OnDrag(float x, float /*y*/) {
        Follow(x);
    }

}  // namespace BECore
