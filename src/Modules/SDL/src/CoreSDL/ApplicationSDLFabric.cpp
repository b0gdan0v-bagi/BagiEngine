#include "ApplicationSDLFabric.h"

#include <BECore/Config/XmlConfig.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/MainWindowAccessor.h>
#include <BECore/RefCounted/New.h>
#include <CoreSDL/SDLEventsProvider.h>
#include <CoreSDL/SDLMainWindow.h>

#include <Generated/EnumRenderer.gen.hpp>

namespace BECore {

    bool ApplicationSDLFabric::Create(const XmlConfig& config) {
        auto configPath = config.GetRoot().ParseAttribute<eastl::string_view>("path");
        if (!configPath || configPath->empty()) {
            return false;
        }

        auto window = BECore::New<SDLMainWindow>();
        if (!window->Initialize(*configPath)) {
            return false;
        }
        CoreManager::GetInstance().GetMainWindowManager().SetMainWindow(window);

        // Create renderer backend from config attribute (default: SDLRendererBackend)
        const auto rendererType = config.GetRoot()
            .ParseAttribute<RendererType>("renderer")
            .value_or(RendererType::SDLRendererBackend);

        auto renderer = RendererFactory::Create(rendererType);
        if (!renderer) {
            return false;
        }
        if (!renderer->Initialize(*window)) {
            return false;
        }
        CoreManager::GetRendererManager().SetRenderer(std::move(renderer));

        auto sdlEventsProvider = BECore::New<SDLEventsProvider>();
        if (!sdlEventsProvider->Initialize()) {
            return false;
        }
        CoreManager::GetEventsProviderManager().RegisterProvider(sdlEventsProvider);

        return true;
    }

}  // namespace BECore
