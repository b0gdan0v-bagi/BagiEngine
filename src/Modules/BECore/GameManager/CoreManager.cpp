#include "CoreManager.h"

#include <BECore/Assert/CRTDebugHook.h>
#include <BECore/Config/ConfigFactory.h>
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

        _loggerManager = MakeFromConfig<LoggerManager>("LoggerConfig");
        _loggerManager->Initialize();

        _assertHandlerManager = MakeFromConfig<AssertHandlerManager>("AssertHandlersConfig");
        _assertHandlerManager->Initialize();
        _resourceManager.Initialize();
        _textureLibrary.Initialize();
        _testManager.RunAllTests();
    }

    void CoreManager::OnApplicationInit(PassKey<Application>) {
        _widgetManager.CreateWidgets();
        _sceneManager = MakeFromConfig<SceneManager>("SceneConfig");
        _sceneManager->Initialize();
    }

    void CoreManager::OnGameCycle(PassKey<Application>) const {
        _eventsProviderManager.ProcessEvents();
        TaskManager::GetInstance().Update(PassKey<CoreManager>{});
        _sceneManager->UpdateAll();         // game logic before UI
        _widgetManager.UpdateAll();         // UI + scene rendering via ViewportWidget
        EventsQueueRegistry::UpdateAll();
        _widgetManager.DrawScreenAll();
        _widgetManager.DrawAll();           // ImGui::Render + GPU present
        // Scene DrawAll is now driven by ViewportWidget inside UpdateAll()
    }

    void CoreManager::OnApplicationDeinit(PassKey<Application>) {
        TaskManager::GetInstance().Shutdown(PassKey<CoreManager>{});
    }

}  // namespace BECore
