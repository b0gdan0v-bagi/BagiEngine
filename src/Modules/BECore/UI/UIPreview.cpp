#include "UIPreview.h"

namespace BECore {

    float UIPreview::AspectOf(ViewportAspect aspect) {
        switch (aspect) {
            case ViewportAspect::Wide16x9:
                return 16.0f / 9.0f;
            case ViewportAspect::Standard4x3:
                return 4.0f / 3.0f;
            case ViewportAspect::UltraWide21x9:
                return 21.0f / 9.0f;
            case ViewportAspect::Portrait9x16:
                return 9.0f / 16.0f;
            case ViewportAspect::Free:
                break;
        }
        return 0.0f;
    }

    std::pair<uint32_t, uint32_t> UIPreview::FitSize(ViewportAspect aspect, float availableWidth, float availableHeight) {
        if (availableWidth < 1.0f || availableHeight < 1.0f) {
            return {0, 0};
        }
        float width = availableWidth;
        float height = availableHeight;
        if (const float ratio = AspectOf(aspect); ratio > 0.0f) {
            if (availableWidth / availableHeight > ratio) {
                width = availableHeight * ratio;
            } else {
                height = availableWidth / ratio;
            }
        }
        return {std::max(static_cast<uint32_t>(width), 1u), std::max(static_cast<uint32_t>(height), 1u)};
    }

    Rect UIPreview::SimulatedSafeArea(SafeAreaSimulation simulation, float width, float height) {
        const float shortSide = std::min(width, height);
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        switch (simulation) {
            case SafeAreaSimulation::TopNotch:
                top = shortSide * kNotchShare;
                break;
            case SafeAreaSimulation::SideInsets:
                left = shortSide * kSideInsetShare;
                right = shortSide * kSideInsetShare;
                bottom = shortSide * kBottomInsetShare;
                break;
            case SafeAreaSimulation::None:
                break;
        }
        return {left, top, width - left - right, height - top - bottom};
    }

}  // namespace BECore
