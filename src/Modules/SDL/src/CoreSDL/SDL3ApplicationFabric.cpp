#include "SDL3ApplicationFabric.h"

#include <BECore/Config/XmlNode.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/RefCounted/New.h>
#include <CoreSDL/SDLEventsProvider.h>
#include <CoreSDL/SDLMainWindow.h>

#include <Generated/EnumRenderer.gen.hpp>

namespace BECore {

    bool SDL3ApplicationFabric::Create(const XmlNode& configNode) {
        auto configPath = configNode.ParseAttribute<eastl::string_view>("path");
        if (!configPath || configPath->empty()) {
            return false;
        }

        auto window = BECore::New<SDLMainWindow>();
        if (!window->Initialize(*configPath)) {
            return false;
        }
        CoreManager::GetInstance().GetMainWindowManager().SetMainWindow(window);

        const auto rendererType = configNode
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
