#include "ControlWindowStatus.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UISettings.h>
#include <Generated/ControlWindowStatus.gen.hpp>
#include <cmath>

namespace BECore {

    WindowInsets SafeAreaInsets(const Rect& safe, int width, int height) {
        WindowInsets insets;
        insets.left = std::max(0.0f, safe.x);
        insets.top = std::max(0.0f, safe.y);
        insets.right = std::max(0.0f, static_cast<float>(width) - (safe.x + safe.w));
        insets.bottom = std::max(0.0f, static_cast<float>(height) - (safe.y + safe.h));
        return insets;
    }

    ControlWindowStatus ControlWindowStatus::Read() {
        const IntrusivePtr<IMainWindow>& window = CoreManager::GetMainWindow();
        const IntrusivePtr<IRenderer>& renderer = CoreManager::GetRenderer();

        ControlWindowStatus status;
        status.userScale = static_cast<double>(CoreManager::GetUIEnvironment().GetSettings().userScale);
        if (window) {
            status.width = window->GetWidth();
            status.height = window->GetHeight();
            status.density = static_cast<double>(window->GetPixelDensity());
            status.displayScale = static_cast<double>(window->GetDisplayScale());
            status.headless = window->IsHeadless();
            const WindowInsets insets = SafeAreaInsets(window->GetSafeArea(), status.width, status.height);
            status.safeArea.bottom = static_cast<double>(insets.bottom);
            status.safeArea.left = static_cast<double>(insets.left);
            status.safeArea.right = static_cast<double>(insets.right);
            status.safeArea.top = static_cast<double>(insets.top);
        }
        if (renderer) {
            status.outputHeight = static_cast<int32_t>(std::lround(renderer->GetOutputHeight()));
            status.outputWidth = static_cast<int32_t>(std::lround(renderer->GetOutputWidth()));
        }
        return status;
    }

}  // namespace BECore
