#include "CoreManager.h"

#include <Events/EventsQueueRegistry.h>

namespace BECore {

    const IntrusivePtr<IMainWindow>& CoreManager::GetMainWindow() {
        return GetInstance()._mainWindowManager.GetMainWindow();
    }

    void CoreManager::OnApplicationPreInit(PassKey<Application>) {
        _fileSystem.Initialize();
        _loggerManager.Initialize();
        _assertHandlerManager.Initialize();
        _testManager.RunAllTests();
    }

    void CoreManager::OnApplicationInit(PassKey<Application>) {
        _widgetManager.CreateWidgets();
    }

    void CoreManager::OnGameCycle(PassKey<Application>) const {
        _eventsProviderManager.ProcessEvents();
        _widgetManager.UpdateAll();
        EventsQueueRegistry::UpdateAll();
        _widgetManager.DrawAll();
    }

    void CoreManager::OnApplicationDeinit(PassKey<Application>) {
    }

}  // namespace BECore

