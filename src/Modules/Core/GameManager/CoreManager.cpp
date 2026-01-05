#include "CoreManager.h"

#include <Events/EventsQueueRegistry.h>

namespace Core {

    const IntrusivePtr<IMainWindow>& CoreManager::GetMainWindow() {
        return GetInstance()._mainWindowManager.GetMainWindow();
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

}  // namespace Core

