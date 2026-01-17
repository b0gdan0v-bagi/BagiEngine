#include "ApplicationSDLFabric.h"

#include <Application/Application.h>
#include <BECore/Config/XmlConfig.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/MainWindowAccessor.h>
#include <BECore/RefCounted/New.h>
#include <CoreSDL/SDLEventsProvider.h>
#include <CoreSDL/SDLMainWindow.h>

namespace BECore {

    bool ApplicationSDLFabric::Create(const XmlConfig& config) {
        auto configPath = config.GetRoot().ParseAttribute<std::string_view>("path");
        if (!configPath || configPath->empty()) {
            return false;
        }

        auto window = BECore::New<SDLMainWindow>();
        if (!window->Initialize(*configPath)) {
            return false;
        }
        CoreManager::GetInstance().GetMainWindowManager().SetMainWindow(window, {});

         auto sdlEventsProvider = BECore::New<SDLEventsProvider>();
        if (!sdlEventsProvider->Initialize()) {
            return false;
        }
        CoreManager::GetEventsProviderManager().RegisterProvider(sdlEventsProvider);

        return true;
    }
}  // namespace BECore

