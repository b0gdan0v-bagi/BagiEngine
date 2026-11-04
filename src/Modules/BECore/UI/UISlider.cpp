#include "UISlider.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/UI/UIProgressBar.h>
#include <Events/InputEvents.h>
#include <Generated/UISlider.gen.hpp>
#include <cmath>

namespace BECore {

    namespace {

        constexpr int SignificantDigits = 6;  // FLT_DIG: a decimal of this many digits survives a float and back, the rest of a longer text is noise
        constexpr int MaxDecimals = 9;
        constexpr size_t NumberBufferSize = 64;  // the largest float written without an exponent has 39 digits

    }  // namespace

    UISlider::UISlider() {
        interactive = true;
    }

    UISlider::Parts UISlider::GetParts(const Rect& area, float fraction) {
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
        if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) {
            return minValue;
        }
        const Parts parts = GetParts(rect, 0.0f);
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

    eastl::string UISlider::FormatValue(float value) {
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
        eastl::string text(buffer, result.ptr);
        if (text.find('.') != eastl::string::npos) {
            while (text.back() == '0') {
                text.pop_back();
            }
            if (text.back() == '.') {
                text.pop_back();
            }
        }
        return text == "-0" ? eastl::string("0") : text;
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

    void UISlider::DrawSelf(IRenderer& renderer) {
        UIElement::DrawSelf(renderer);
        if (rect.IsEmpty()) {
            return;
        }
        const Parts parts = GetParts(rect, GetFraction());
        renderer.DrawFilledRect(parts.track.x, parts.track.y, parts.track.w, parts.track.h, GetCurrentTrack());
        if (parts.filled.w > 0.0f) {
            renderer.DrawFilledRect(parts.filled.x, parts.filled.y, parts.filled.w, parts.filled.h, GetCurrentFill());
        }
        if (!parts.thumb.IsEmpty()) {
            renderer.DrawFilledRect(parts.thumb.x, parts.thumb.y, parts.thumb.w, parts.thumb.h, GetCurrentThumb());
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
