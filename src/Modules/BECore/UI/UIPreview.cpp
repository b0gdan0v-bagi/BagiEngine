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

    Pair<uint32_t, uint32_t> UIPreview::FitSize(ViewportAspect aspect, float availableWidth, float availableHeight) {
        return FitRatio(AspectOf(aspect), availableWidth, availableHeight);
    }

    Pair<uint32_t, uint32_t> UIPreview::FitRatio(float ratio, float availableWidth, float availableHeight) {
        if (availableWidth < 1.0f || availableHeight < 1.0f) {
            return {0, 0};
        }
        float width = availableWidth;
        float height = availableHeight;
        if (ratio > 0.0f) {
            if (availableWidth / availableHeight > ratio) {
                width = availableHeight * ratio;
            } else {
                height = availableWidth / ratio;
            }
        }
        return {std::max(static_cast<uint32_t>(width), 1u), std::max(static_cast<uint32_t>(height), 1u)};
    }

    Span<const PreviewDevice> UIPreview::GetDevices() {
        // Logical points in portrait; the cut-out is the status bar / island / camera hole, the home bar the gesture area
        static constexpr PreviewDevice kDevices[] = {
            {"iPhone SE", 375, 667, 0.0f, 0.0f},    {"iPhone 15", 393, 852, 59.0f, 34.0f},      {"Pixel 8", 412, 915, 32.0f, 16.0f},
            {"Galaxy S23", 360, 780, 28.0f, 16.0f}, {"iPad 10th gen", 820, 1180, 24.0f, 20.0f}, {"Laptop 1280x800", 800, 1280, 0.0f, 0.0f},
        };
        return kDevices;
    }

    PreviewTarget UIPreview::TargetFor(const PreviewShape& shape, float availableWidth, float availableHeight) {
        if (availableWidth < 1.0f || availableHeight < 1.0f) {
            return {0, 0, 0.0f};
        }

        uint32_t width = 0;
        uint32_t height = 0;
        const auto devices = GetDevices();
        if (shape.custom) {
            width = shape.customWidth;
            height = shape.customHeight;
        } else if (shape.device >= 0) {
            if (static_cast<size_t>(shape.device) >= devices.size()) {
                // An index this build does not have is Free, not the preset that happens to be stored beside it
                const auto [freeWidth, freeHeight] = FitRatio(0.0f, availableWidth, availableHeight);
                return {freeWidth, freeHeight, 1.0f};
            }
            width = devices[static_cast<size_t>(shape.device)].width;
            height = devices[static_cast<size_t>(shape.device)].height;
        } else {
            float ratio = AspectOf(shape.aspect);
            if (shape.rotated && ratio > 0.0f) {
                ratio = 1.0f / ratio;
            }
            const auto [fitWidth, fitHeight] = FitRatio(ratio, availableWidth, availableHeight);
            return {fitWidth, fitHeight, 1.0f};
        }

        width = std::clamp(width, kMinPreviewSide, kMaxPreviewSide);
        height = std::clamp(height, kMinPreviewSide, kMaxPreviewSide);
        if (shape.rotated) {
            std::swap(width, height);
        }
        const float zoom = std::min({1.0f, availableWidth / static_cast<float>(width), availableHeight / static_cast<float>(height)});
        return {width, height, zoom};
    }

    Rect UIPreview::SimulatedSafeArea(SafeAreaSimulation simulation, float width, float height) {
        return SimulatedSafeArea(simulation, nullptr, width, height);
    }

    Rect UIPreview::SimulatedSafeArea(SafeAreaSimulation simulation, const PreviewDevice* device, float width, float height) {
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
            case SafeAreaSimulation::Device:
                if (device) {
                    // At least one pixel is left on each axis
                    if (width < height) {
                        top = std::clamp(device->cutout, 0.0f, std::max(height - 1.0f, 0.0f));
                        bottom = std::clamp(device->homeBar, 0.0f, std::max(height - top - 1.0f, 0.0f));
                    } else {
                        left = std::clamp(device->cutout, 0.0f, std::max((width - 1.0f) * 0.5f, 0.0f));
                        right = left;
                        bottom = std::clamp(device->homeBar, 0.0f, std::max(height - 1.0f, 0.0f));
                    }
                }
                break;
            case SafeAreaSimulation::None:
                break;
        }
        return {left, top, width - left - right, height - top - bottom};
    }

}  // namespace BECore
