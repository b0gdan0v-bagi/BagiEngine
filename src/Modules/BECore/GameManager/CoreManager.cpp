#include "CoreManager.h"

#include <BECore/Assert/CRTDebugHook.h>
#include <BECore/Logger/OutputSink.h>
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
        // Install CRT debug hooks as early as possible
        InstallCRTDebugHooks();
        
        {
            // For scope before
            auto tempTestSync = New<BECore::OutputSink>();
            tempTestSync->Initialize();
            auto stackTraceHandler = New<BECore::StackTraceHandler>();
            stackTraceHandler->Initialize();

            _fileSystem.Initialize();
            TaskManager::GetInstance().Initialize(PassKey<CoreManager>{});
            _configManager.Initialize();
        }
        
        _loggerManager.Initialize();
        _assertHandlerManager.Initialize();
        _resourceManager.Initialize();
        _testManager.RunAllTests();
    }

    void CoreManager::OnApplicationInit(PassKey<Application>) {
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

