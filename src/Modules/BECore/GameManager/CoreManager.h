#pragma once

#include <BECore/Assert/AssertHandlers.h>
#include <BECore/Config/ConfigManager.h>
#include <BECore/Control/ControlService.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/MainWindow/MainWindowManager.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Tests/TestManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Time/Time.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/Widgets/WidgetManager.h>
#include <Events/EventsProviderManager.h>

namespace BECore {

    // Forward declarations
    class Application;
    class TaskManager;

    namespace Tests {
        class EngineEnvironment;
    }  // namespace Tests

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

        static TextureLibrary& GetTextureLibrary() {
            return GetInstance()._textureLibrary;
        }

        static FontAtlasLibrary& GetFontAtlasLibrary() {
            return GetInstance()._fontAtlasLibrary;
        }

        static Time& GetTime() {
            return GetInstance()._time;
        }

        static ControlService& GetControlService() {
            return GetInstance()._controlService;
        }

        static UIInput& GetUIInput() {
            return GetInstance()._uiInput;
        }

        static UIEnvironment& GetUIEnvironment() {
            return GetInstance()._uiEnvironment;
        }

        // Pre-initialization function (FileSystem, AssertHandlers, runtime ITests)
        void OnApplicationPreInit(PassKey<Application>);

        // Core services for the gtest executable: the same services as OnApplicationPreInit,
        // without running the runtime ITests and with the debugger break on assert disabled
        void OnTestEnvironmentInit(PassKey<Tests::EngineEnvironment>);
        void OnTestEnvironmentDeinit(PassKey<Tests::EngineEnvironment>);

        // Application initialization function
        void OnApplicationInit(PassKey<Application>);

        // Функция игрового цикла
        void OnGameCycle(PassKey<Application>);

        // Idle cycle of the manual clock: no frame runs, the window stays alive and control requests are answered (Control/CLAUDE.md)
        void OnIdleCycle(PassKey<Application>);

        // Функция деинициализации приложения
        void OnApplicationDeinit(PassKey<Application>);

    private:
        // FileSystem, TaskManager, ConfigManager, Logger, AssertHandlers, ResourceManager, TextureLibrary, FontAtlasLibrary, UIEnvironment, the builtin control methods
        void InitializeCoreServices();

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
        // Declared before _sceneManager, so they outlive the scenes: a canvas unregisters from UIInput when its scene is destroyed
        UIInput _uiInput;
        UIEnvironment _uiEnvironment;
        IntrusivePtrAtomic<SceneManager> _sceneManager;
        TextureLibrary _textureLibrary;
        FontAtlasLibrary _fontAtlasLibrary;
        Time _time;
        ControlService _controlService;
    };

}  // namespace BECore
