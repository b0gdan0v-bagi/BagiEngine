#include "SDL3ApplicationFabric.h"

#include <BECore/Application/ApplicationConfig.h>
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

    bool SDL3ApplicationFabric::Create(const ApplicationConfig& config, const ApplicationOptions& options) {
        if (config._path.empty()) {
            LOG_ERROR("ApplicationConfig: no path (the window config of SDL3ApplicationFabric)");
            return false;
        }

        auto window = BECore::New<SDLMainWindow>();
        window->SetHeadless(options.headless);
        if (!window->Initialize(eastl::string_view(config._path.data(), config._path.size()))) {
            return false;
        }
        CoreManager::GetInstance().GetMainWindowManager().SetMainWindow(window);

        // Headless drawing is the SDL software renderer: the Vulkan backend needs a real surface
        const bool defaultRenderer = options.headless || config._renderer.empty();
        const eastl::string_view rendererTypeName = defaultRenderer ? kSdlRendererName : eastl::string_view(config._renderer.data(), config._renderer.size());
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
