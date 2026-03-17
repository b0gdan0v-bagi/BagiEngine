#pragma once

#include <BECore/Assert/AssertHandlers.h>
#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/MainWindow/MainWindowManager.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Tests/TestManager.h>
#include <BECore/Widgets/WidgetManager.h>
#include <Events/EventsProviderManager.h>

namespace BECore {

    // Forward declarations
    class Application;
    class TaskManager;

    class CoreManager : public Singleton<CoreManager> {
    public:
        CoreManager() = default;
        ~CoreManager() override = default;

        static WidgetManager& GetWidgetManager() {
            return GetInstance()._widgetManager;
        }

        static EventsProviderManager& GetEventsProviderManager() {
            return GetInstance()._eventsProviderManager;
        }

        static MainWindowManager& GetMainWindowManager() {
            return GetInstance()._mainWindowManager;
        }

        static FileSystem& GetFileSystem() {
            return GetInstance()._fileSystem;
        }

        static ConfigManager& GetConfigManager() {
            return GetInstance()._configManager;
        }

        static AssertHandlerManager& GetAssertHandlerManager() {
            return *GetInstance()._assertHandlerManager;
        }

        static TestManager& GetTestManager() {
            return GetInstance()._testManager;
        }

        static LoggerManager& GetLoggerManager() {
            return *GetInstance()._loggerManager;
        }

        static ResourceManager& GetResourceManager() {
            return GetInstance()._resourceManager;
        }

        static TaskManager& GetTaskManager();

        static const IntrusivePtr<IMainWindow>& GetMainWindow();

        static RendererManager& GetRendererManager() {
            return GetInstance()._rendererManager;
        }

        static const IntrusivePtr<IRenderer>& GetRenderer() {
            return GetInstance()._rendererManager.GetRenderer();
        }

        static SceneManager& GetSceneManager() {
            return *GetInstance()._sceneManager;
        }

        // Pre-initialization function (FileSystem, AssertHandlers, Tests)
        void OnApplicationPreInit(PassKey<Application>);

        // Application initialization function
        void OnApplicationInit(PassKey<Application>);

        // Функция игрового цикла
        void OnGameCycle(PassKey<Application>) const;

        // Функция деинициализации приложения
        void OnApplicationDeinit(PassKey<Application>);

    private:
        FileSystem _fileSystem;
        ConfigManager _configManager;
        IntrusivePtrAtomic<LoggerManager> _loggerManager;
        IntrusivePtrAtomic<AssertHandlerManager> _assertHandlerManager;
        TestManager _testManager;
        ResourceManager _resourceManager;
        WidgetManager _widgetManager;
        EventsProviderManager _eventsProviderManager;
        MainWindowManager _mainWindowManager;
        RendererManager _rendererManager;
        IntrusivePtrAtomic<SceneManager> _sceneManager;
    };

}  // namespace BECore
