#include "SDL3ApplicationFabric.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Renderer/IRenderer.h>
#include <CoreSDL/SDLEventsProvider.h>
#include <CoreSDL/SDLMainWindow.h>

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

        const auto rendererTypeName = configNode.ParseAttribute<eastl::string_view>("renderer").value_or("SDLRendererBackend");

        auto renderer = AbstractFactory<IRenderer>::GetInstance().Create(rendererTypeName);
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
