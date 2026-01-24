#include "CoreManager.h"

#include <Events/EventsQueueRegistry.h>
#include <TaskSystem/TaskManager.h>

namespace BECore {

    const IntrusivePtr<IMainWindow>& CoreManager::GetMainWindow() {
        return GetInstance()._mainWindowManager.GetMainWindow();
    }

    TaskManager& CoreManager::GetTaskManager() {
        return TaskManager::GetInstance();
    }

    void CoreManager::OnApplicationPreInit(PassKey<Application>) {
        _fileSystem.Initialize();
        _loggerManager.Initialize();
        _assertHandlerManager.Initialize();
        _resourceManager.Initialize(&_fileSystem);
        _testManager.RunAllTests();
    }

    void CoreManager::OnApplicationInit(PassKey<Application>) {
        TaskManager::GetInstance().Initialize(PassKey<CoreManager>{});
        _widgetManager.CreateWidgets();
    }

    void CoreManager::OnGameCycle(PassKey<Application>) const {
        _eventsProviderManager.ProcessEvents();
        TaskManager::GetInstance().Update(PassKey<CoreManager>{});
        _widgetManager.UpdateAll();
        EventsQueueRegistry::UpdateAll();
        _widgetManager.DrawAll();
    }

    void CoreManager::OnApplicationDeinit(PassKey<Application>) {
        TaskManager::GetInstance().Shutdown(PassKey<CoreManager>{});
    }

}  // namespace BECore

