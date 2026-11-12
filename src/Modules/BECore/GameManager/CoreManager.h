#pragma once

#include <BECore/Animation/TweenManager.h>
#include <BECore/Assert/AssertHandlers.h>
#include <BECore/Config/ConfigManager.h>
#include <BECore/Control/ControlService.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/MainWindow/MainWindowManager.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Tests/TestManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Time/GamePause.h>
#include <BECore/Time/Time.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UIVars.h>
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

        /// Whether the game runs, is paused or steps: the single truth for the editor, the control API and the manual clock (`Time/GamePause.h`). Main thread only
        static GamePause& GetPause() {
            return GetInstance()._pause;
        }

        /// The tweens of the game, ticked once per frame from the frame time (`Animation/CLAUDE.md`). Main thread only
        static TweenManager& GetTweens() {
            return GetInstance()._tweenManager;
        }

        /// The cost of the last frames: stage times, draws, allocations, named counters (Profiler/CLAUDE.md, "Frame statistics")
        static FrameStatsCollector& GetFrameStats() {
            return GetInstance()._frameStats;
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

        /// The variables that UI text is bound to (`text="Score: {score}"`)
        static UIVars& GetUIVars() {
            return GetInstance()._uiVars;
        }

        /// The string table of the UI (`text="{#menu.start}"`), loaded from `config/UIStrings.xml` when the game ships one
        static UIStrings& GetUIStrings() {
            return GetInstance()._uiStrings;
        }

        /// The colour themes of the UI (`color="@primary"`), loaded from `config/UIThemes.xml` when the game ships one; `SetTheme` switches the open screen at its next draw
        static UIThemes& GetUIThemes() {
            return GetInstance()._uiThemes;
        }

        // Pre-initialization function (FileSystem, AssertHandlers, runtime ITests)
        void OnApplicationPreInit(PassKey<Application>);

        // Core services for the gtest executable: the same services as OnApplicationPreInit,
        // without running the runtime ITests and with the debugger break on assert disabled
        void OnTestEnvironmentInit(PassKey<Tests::EngineEnvironment>);
        void OnTestEnvironmentDeinit(PassKey<Tests::EngineEnvironment>);

        // A test builds its scenes from an XML string and owns the manager. Components that switch scenes ask CoreManager::GetSceneManager(), which is
        // empty in the gtest executable, so the test hands its manager over here (and an empty pointer back at the end)
        void SetSceneManagerForTest(IntrusivePtrAtomic<SceneManager> manager, PassKey<Tests::EngineEnvironment>);

        // The part of OnGameCycle that a pause skips (game time of the tasks, tasks, tweens, scene update), so a test can run it without a window and the widgets
        void UpdateGameForTest(FrameKind kind, PassKey<Tests::EngineEnvironment>);

        // Application initialization function
        void OnApplicationInit(PassKey<Application>);

        // The game cycle of one frame; @p kind is what GamePause decided. A Held frame still polls input, answers the control API, updates the widgets and draws,
        // but advances neither game time, tasks, tweens nor the scene
        void OnGameCycle(PassKey<Application>, FrameKind kind);

        // Idle cycle of the manual clock: no frame runs, the window stays alive and control requests are answered (Control/CLAUDE.md)
        void OnIdleCycle(PassKey<Application>);

        // Функция деинициализации приложения
        void OnApplicationDeinit(PassKey<Application>);

    private:
        // FileSystem, TaskManager, ConfigManager, Logger, AssertHandlers, ResourceManager, TextureLibrary, FontAtlasLibrary, UIEnvironment, UIStrings, UIThemes, the builtin control methods
        void InitializeCoreServices();

        // Game time of the delayed tasks, tasks, tweens and the scene update: everything a Held frame leaves out. Does nothing for FrameKind::Held
        void UpdateGame(FrameKind kind);

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
        UIVars _uiVars;
        UIStrings _uiStrings;
        UIThemes _uiThemes;
        IntrusivePtrAtomic<SceneManager> _sceneManager;
        TextureLibrary _textureLibrary;
        FontAtlasLibrary _fontAtlasLibrary;
        Time _time;
        GamePause _pause;
        // A setter may point into a scene: OnApplicationDeinit and OnTestEnvironmentDeinit Clear() it first. Declared after _sceneManager on purpose: the
        // destructor then drops the tweens before the scenes go, a second guarantee besides Clear(); moving it above _sceneManager could break that silently
        TweenManager _tweenManager;
        FrameStatsCollector _frameStats;
        ControlService _controlService;
    };

}  // namespace BECore
