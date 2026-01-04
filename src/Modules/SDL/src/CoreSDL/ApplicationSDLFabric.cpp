#include "ApplicationSDLFabric.h"

#include <Application/Application.h>
#include <Core/Config/XmlConfig.h>
#include <Core/FileSystem/FileSystem.h>
#include <Core/RefCounted/New.h>
#include <CoreSDL/SDLEventsProvider.h>
#include <CoreSDL/SDLMainWindow.h>

namespace Core {

    bool ApplicationSDLFabric::Create(const XmlConfig& config) {
        auto configPath = config.GetRoot().ParseAttribute<std::string_view>("path");
        if (!configPath || configPath->empty()) {
            return false;
        }

        auto window = Core::New<SDLMainWindow>();
        if (!window->Initialize(*configPath)) {
            return false;
        }
        Application::GetInstance().SetMainWindow(window, PassKey<ApplicationMainAccess>{});

         auto sdlEventsProvider = Core::New<SDLEventsProvider>();
        if (!sdlEventsProvider->Initialize()) {
            return false;
        }
        Application::GetEventsProviderManager().RegisterProvider(sdlEventsProvider);

        return true;
    }
}  // namespace Core

