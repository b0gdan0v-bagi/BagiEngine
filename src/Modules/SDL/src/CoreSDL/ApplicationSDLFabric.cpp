#include "ApplicationSDLFabric.h"

#include <Application/Application.h>
#include <Core/Config/XmlConfig.h>
#include <Core/FileSystem/FileSystem.h>
#include <Core/GameManager/CoreManager.h>
#include <Core/MainWindow/MainWindowAccessor.h>
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
        CoreManager::GetInstance().GetMainWindowManager().SetMainWindow(window, {});

         auto sdlEventsProvider = Core::New<SDLEventsProvider>();
        if (!sdlEventsProvider->Initialize()) {
            return false;
        }
        CoreManager::GetEventsProviderManager().RegisterProvider(sdlEventsProvider);

        return true;
    }
}  // namespace Core

