#include "SDL3ApplicationFabric.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Renderer/IRenderer.h>
#include <CoreSDL/SDLEventsProvider.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreSDL/SDLScreenshot.h>
#include <Generated/SDL3ApplicationFabric.gen.hpp>

namespace BECore {

    namespace {

        constexpr eastl::string_view kSdlRendererName = "SDLRendererBackend";

    }  // namespace

    bool SDL3ApplicationFabric::Create(const XmlNode& configNode, const ApplicationOptions& options) {
        auto configPath = configNode.ParseAttribute<eastl::string_view>("path");
        if (!configPath || configPath->empty()) {
            return false;
        }

        auto window = BECore::New<SDLMainWindow>();
        window->SetHeadless(options.headless);
        if (!window->Initialize(*configPath)) {
            return false;
        }
        CoreManager::GetInstance().GetMainWindowManager().SetMainWindow(window);

        // Headless drawing is the SDL software renderer: the Vulkan backend needs a real surface
        const auto rendererTypeName = options.headless ? eastl::string_view(kSdlRendererName) : configNode.ParseAttribute<eastl::string_view>("renderer").value_or(kSdlRendererName);
        if (options.headless) {
            LOG_INFO("Headless: no window, drawing with {}"_format(rendererTypeName));
        }

        auto renderer = AbstractFactory<IRenderer>::GetInstance().Create(rendererTypeName);
        if (!renderer) {
            return false;
        }
        if (!renderer->Initialize(*window)) {
            return false;
        }
        CoreManager::GetRendererManager().SetRenderer(std::move(renderer));
        RegisterScreenshotControl(CoreManager::GetControlService());

        auto sdlEventsProvider = BECore::New<SDLEventsProvider>();
        if (!sdlEventsProvider->Initialize()) {
            return false;
        }
        CoreManager::GetEventsProviderManager().RegisterProvider(sdlEventsProvider);

        return true;
    }

}  // namespace BECore
