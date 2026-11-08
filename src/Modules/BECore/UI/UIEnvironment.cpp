#include "UIEnvironment.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>

namespace BECore {

    void UIEnvironment::Initialize() {
        _settings = UISettings::FromConfig(CoreManager::GetConfigManager().GetConfig("UIConfig"_intern));
    }

    void UIEnvironment::SetSettings(const UISettings& settings) {
        _settings = settings;
        _settings.userScale = UISettings::ClampUserScale(_settings.userScale);
    }

    void UIEnvironment::SetUserScale(float scale) {
        _settings.userScale = UISettings::ClampUserScale(scale);
    }

    UILayoutContext UIEnvironment::MakeContext(const IMainWindow* window, float outputWidth, float outputHeight) const {
        UILayoutContext context;
        context.outputWidth = outputWidth;
        context.outputHeight = outputHeight;
        context.userScale = _settings.userScale;
        if (window) {
            context.pixelDensity = window->GetPixelDensity();
            context.displayScale = window->GetDisplayScale();
        }
        if (_pixelDensityOverride && *_pixelDensityOverride > 0.0f) {
            context.pixelDensity = *_pixelDensityOverride;
        }

        if (_safeAreaOverride) {
            context.safeArea = *_safeAreaOverride;
        } else if (window) {
            // The window reports its safe area in window units; the effective density (the override, if any) turns them into pixels
            const Rect safe = window->GetSafeArea();
            context.safeArea = {safe.x * context.pixelDensity, safe.y * context.pixelDensity, safe.w * context.pixelDensity, safe.h * context.pixelDensity};
        }
        return context;
    }

}  // namespace BECore
