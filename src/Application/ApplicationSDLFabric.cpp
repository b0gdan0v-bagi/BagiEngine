#include "ApplicationSDLFabric.h"

#include <Application/Application.h>
#include <Core/Config/XmlConfig.h>
#include <Core/Events/SDLEventsProvider.h>
#include <Core/FileSystem/FileSystem.h>
#include <Core/MainWindow/SDLMainWindow.h>
#include <Core/Utils/New.h>

namespace Core {

    bool ApplicationSDLFabric::Create(const XmlConfig& config) {
        std::string configPath = config.Get<std::string>("root.path", "");
        if (configPath.empty()) {
            return false;
        }

        auto window = Core::New<SDLMainWindow>();
        if (!window->Initialize(configPath)) {
            return false;
        }
        Application::GetInstance().SetMainWindow(window);

         auto sdlEventsProvider = Core::New<SDLEventsProvider>();
        if (!sdlEventsProvider->Initialize()) {
            return false;
        }
        Application::GetEventManager().RegisterProvider(sdlEventsProvider);

        return true;
    }
}  // namespace Core