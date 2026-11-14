#include "UISliderComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIProgressBar.h>
#include <BECore/UI/UIThemes.h>
#include <Events/InputEvents.h>
#include <Generated/UISliderComponent.gen.hpp>
#include <cmath>

namespace BECore {

    namespace {

        constexpr int SignificantDigits = 6;  // FLT_DIG: a decimal of this many digits survives a float and back, the rest of a longer text is noise
        constexpr int MaxDecimals = 9;
        constexpr size_t NumberBufferSize = 64;  // the largest float written without an exponent has 39 digits

    }  // namespace

    UISliderComponent::Parts UISliderComponent::GetParts(const Rect& area, float fraction) {
        Parts parts;
        const float thumbSize = std::max(0.0f, std::min(area.h, area.w));
        const float trackHeight = std::min(kTrackHeight, area.h);
        parts.travelStart = area.x + thumbSize * 0.5f;
        parts.travelEnd = std::max(parts.travelStart, area.x + area.w - thumbSize * 0.5f);

        const float centerX = parts.travelStart + (parts.travelEnd - parts.travelStart) * std::clamp(fraction, 0.0f, 1.0f);
        const float centerY = area.y + area.h * 0.5f;
        parts.track = {area.x, centerY - trackHeight * 0.5f, area.w, trackHeight};
        parts.filled = {area.x, parts.track.y, std::max(0.0f, centerX - area.x), trackHeight};
        parts.thumb = {centerX - thumbSize * 0.5f, area.y + (area.h - thumbSize) * 0.5f, thumbSize, thumbSize};
        return parts;
    }

    float UISliderComponent::ValueAtX(const Rect& area, float minValue, float maxValue, float step, float canvasX) {
        if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) {
            return minValue;
        }
        const Parts parts = GetParts(area, 0.0f);
        const double travel = static_cast<double>(parts.travelEnd) - parts.travelStart;
        const double along = travel > 0.0 ? std::clamp((static_cast<double>(canvasX) - parts.travelStart) / travel, 0.0, 1.0) : 0.0;

        // In double: the sum of float steps drifts (0.1 three times is not 0.3)
        const double low = minValue;
        const double high = maxValue;
        double value = low + along * (high - low);
        if (std::isfinite(step) && step > 0.0f) {
            const double snapped = std::clamp(low + std::round((value - low) / step) * step, low, high);
            // A range that is not a whole number of steps still has its maximum, which the pointer at the end asks for
            value = std::abs(value - high) < std::abs(value - snapped) ? high : snapped;
        }
        return static_cast<float>(std::clamp(value, low, high));
    }

    String UISliderComponent::FormatValue(float value) {
        if (!std::isfinite(value)) {
            return "0";
        }
        const double number = value;
        int decimals = SignificantDigits - 1;
        if (number != 0.0) {
            const int exponent = static_cast<int>(std::floor(std::log10(std::abs(number))));
            decimals = std::clamp(SignificantDigits - 1 - exponent, 0, MaxDecimals);
        }
        char buffer[NumberBufferSize];
        const auto result = std::to_chars(buffer, buffer + NumberBufferSize, number, std::chars_format::fixed, decimals);
        if (result.ec != std::errc{}) {
            return "0";
        }
        String text(buffer, result.ptr);
        if (text.find('.') != String::npos) {
            while (text.back() == '0') {
                text.pop_back();
            }
            if (text.back() == '.') {
                text.pop_back();
            }
        }
        return text == "-0" ? String("0") : text;
    }

    float UISliderComponent::GetValue() const {
        if (bind.Empty()) {
            return minValue;
        }
        return UIText::ValueInRange(CoreManager::GetUIVars().Get(bind), minValue, maxValue);
    }

    float UISliderComponent::GetFraction() const {
        return UIText::FractionInRange(GetValue(), minValue, maxValue);
    }

    float UISliderComponent::ValueAt(const UIElement& owner, float canvasX) const {
        return ValueAtX(owner.rect, minValue, maxValue, step, canvasX);
    }

    Color UISliderComponent::GetCurrentTrack(const UIElement& owner) const {
        const Color base = trackColor.a > 0 ? Color(trackColor) : UIProgressBar::kTrackFallback;
        return owner.IsDisabled() ? WithAlpha(base, kDisabledAlpha) : base;
    }

    Color UISliderComponent::GetCurrentFill(const UIElement& owner) const {
        const Color base = fillColor.a > 0 ? Color(fillColor) : kFillFallback;
        return owner.IsDisabled() ? WithAlpha(base, kDisabledAlpha) : base;
    }

    Color UISliderComponent::GetCurrentThumb(const UIElement& owner) const {
        const Color base = thumbColor.a > 0 ? Color(thumbColor) : kThumbFallback;
        if (owner.IsDisabled()) {
            return WithAlpha(base, kDisabledAlpha);
        }
        // The press is held wherever the pointer went, so it does not need the pointer over the slider (the owner's own flag, not the visual state that drops a press that left)
        if (owner.pressed) {
            return ShiftColor(base, -kPressDarken);
        }
        return owner.hovered ? ShiftColor(base, kHoverBrighten) : base;
    }

    void UISliderComponent::ApplyTheme(UIThemes& themes) {
        themes.Resolve(trackColor);
        themes.Resolve(fillColor);
        themes.Resolve(thumbColor);
    }

    bool UISliderComponent::MakesInteractive() const {
        return true;
    }

    void UISliderComponent::DrawSelf(IRenderer& renderer, UIElement& owner) {
        if (owner.rect.IsEmpty()) {
            return;
        }
        const Parts parts = GetParts(owner.rect, GetFraction());
        owner.DrawPart(renderer, parts.track, GetCurrentTrack(owner), partShape);
        if (parts.filled.w > 0.0f) {
            owner.DrawPart(renderer, parts.filled, GetCurrentFill(owner), partShape);
        }
        if (!parts.thumb.IsEmpty()) {
            owner.DrawPart(renderer, parts.thumb, GetCurrentThumb(owner), partShape);
        }
    }

    void UISliderComponent::Follow(const UIElement& owner, float x) {
        if (owner.IsDisabled() || action.Empty()) {
            return;
        }
        const PoolString text = PoolString::Intern(FormatValue(ValueAt(owner, x)));
        if (text == _lastEmitted) {
            return;
        }
        _lastEmitted = text;
        InputEvents::ActionEvent::Emit(action, text);
    }

    void UISliderComponent::OnPress(UIElement& owner, float x, float /*y*/) {
        _lastEmitted = PoolString{};  // a new gesture always emits, whatever the last one ended on
        Follow(owner, x);
    }

    void UISliderComponent::OnDrag(UIElement& owner, float x, float /*y*/) {
        Follow(owner, x);
    }

}  // namespace BECore
