#include "CoreManager.h"

#include <BECore/Animation/TweenManager.h>
#include <BECore/Application/CommandLine.h>
#include <BECore/Assert/AssertHandlers.h>
#include <BECore/Assert/CRTDebugHook.h>
#include <BECore/Audio/AudioControl.h>
#include <BECore/Audio/AudioManager.h>
#include <BECore/Clipboard/Clipboard.h>
#include <BECore/Clipboard/ClipboardControl.h>
#include <BECore/Config/ConfigFactory.h>
#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlService.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/FileWatchControl.h>
#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/FileSystem/PlatformFileWatchBackend.h>
#include <BECore/Input/InputControl.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Logger/OutputSink.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/MainWindow/MainWindowManager.h>
#include <BECore/Memory/MemTag.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/ProfilerControl.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Tests/TestManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Time/GamePause.h>
#include <BECore/Time/Time.h>
#include <BECore/UI/UIControl.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UIVars.h>
#include <BECore/Widgets/WidgetManager.h>
#include <Events/EventsProviderManager.h>
#include <Events/EventsQueueRegistry.h>
#include <TaskSystem/TaskManager.h>

namespace BECore {

    namespace {

        /// Longest wait of an idle cycle: how late the window events and the requests of a client are looked at
        constexpr std::chrono::milliseconds kIdleWait{8};

    }  // namespace

    CoreManager::CoreManager()
        : _commandLine(MakeUnique<CommandLine>()), _fileSystem(MakeUnique<FileSystem>()), _fileWatcher(MakeUnique<FileWatcher>(*_fileSystem)), _configManager(MakeUnique<ConfigManager>()),
          _testManager(MakeUnique<TestManager>()), _resourceManager(MakeUnique<ResourceManager>()), _widgetManager(MakeUnique<WidgetManager>()),
          _eventsProviderManager(MakeUnique<EventsProviderManager>()), _clipboard(MakeUnique<Clipboard>()), _audioManager(MakeUnique<AudioManager>()),
          _mainWindowManager(MakeUnique<MainWindowManager>()), _rendererManager(MakeUnique<RendererManager>()), _uiInput(MakeUnique<UIInput>()), _uiEnvironment(MakeUnique<UIEnvironment>()),
          _uiVars(MakeUnique<UIVars>()), _uiStrings(MakeUnique<UIStrings>()), _uiThemes(MakeUnique<UIThemes>()), _textureLibrary(MakeUnique<TextureLibrary>()),
          _fontAtlasLibrary(MakeUnique<FontAtlasLibrary>()), _time(MakeUnique<Time>()), _pause(MakeUnique<GamePause>()), _tweenManager(MakeUnique<TweenManager>()),
          _frameStats(MakeUnique<FrameStatsCollector>()), _controlService(MakeUnique<ControlService>()) {}

    CoreManager::~CoreManager() = default;

    const IntrusivePtr<IRenderer>& CoreManager::GetRenderer() {
        return GetInstance()._rendererManager->GetRenderer();
    }

    const IntrusivePtr<IMainWindow>& CoreManager::GetMainWindow() {
        return GetInstance()._mainWindowManager->GetMainWindow();
    }

    TaskManager& CoreManager::GetTaskManager() {
        return TaskManager::GetInstance();
    }

    void CoreManager::OnApplicationPreInit(PassKey<Application>) {
        BE_PROFILE_SCOPE("CoreManager::OnApplicationPreInit");
        InitializeCoreServices();
        _testManager->RunAllTests();
    }

    void CoreManager::OnCookInit(PassKey<Application>) {
        _fileSystem->Initialize();
        TaskManager::GetInstance().Initialize(PassKey<CoreManager>{});
    }

    void CoreManager::OnCookDeinit(PassKey<Application>) {
        TaskManager::GetInstance().Shutdown(PassKey<CoreManager>{});
    }

    void CoreManager::OnTestEnvironmentInit(PassKey<Tests::EngineEnvironment>) {
        InitializeCoreServices();

        // A failed assert has to be reported by the test runner, not trap the whole test process
        if (auto* debugBreak = _assertHandlerManager->GetHandler<DebugBreakHandler>()) {
            debugBreak->SetEnabled(false);
        }
    }

    void CoreManager::OnTestEnvironmentDeinit(PassKey<Tests::EngineEnvironment>) {
        _audioManager->Shutdown();
        _tweenManager->Clear();  // no callbacks: what they point at may be gone
        _controlService->Shutdown();
        _configManager->StopWatching();
        _fileWatcher->Stop();
        TaskManager::GetInstance().Shutdown(PassKey<CoreManager>{});
    }

    void CoreManager::SetSceneManagerForTest(IntrusivePtr<SceneManager> manager, PassKey<Tests::EngineEnvironment>) {
        _sceneManager = std::move(manager);
    }

    void CoreManager::UpdateGameForTest(FrameKind kind, PassKey<Tests::EngineEnvironment>) {
        UpdateGame(kind);
    }

    void CoreManager::InitializeCoreServices() {
        // Install CRT debug hooks as early as possible
        InstallCRTDebugHooks();

        {
            // For scope before
            auto tempTestSync = New<BECore::OutputSink>();
            tempTestSync->Initialize();
            auto stackTraceHandler = New<BECore::StackTraceHandler>();
            stackTraceHandler->Initialize();

            _fileSystem->Initialize();
            TaskManager::GetInstance().Initialize(PassKey<CoreManager>{});
            _configManager->Initialize();
        }

        _loggerManager = MakeFromConfig<LoggerManager>(ConfigNames::logger.View());
        _loggerManager->Initialize();

        _assertHandlerManager = MakeFromConfig<AssertHandlerManager>(ConfigNames::assertHandlers.View());
        _assertHandlerManager->Initialize();
        _resourceManager->Initialize();
        _textureLibrary->Initialize();
        _fontAtlasLibrary->Initialize();
        _uiEnvironment->Initialize();
        _uiStrings->Initialize();
        _uiThemes->Initialize();
        _audioManager->Initialize();

        _controlService->SetPause(_pause.get());
        RegisterControlBuiltins(*_controlService);
        RegisterUIControl(*_controlService);
        RegisterInputControl(*_controlService);
        RegisterProfilerControl(*_controlService);
        RegisterClipboardControl(*_controlService);
        RegisterAudioControl(*_controlService, *_audioManager);
        RegisterFileWatchControl(*_controlService, *_fileWatcher);
    }

    void CoreManager::OnApplicationInit(PassKey<Application>) {
        BE_PROFILE_SCOPE("CoreManager::OnApplicationInit");
        _widgetManager->CreateWidgets();
        _sceneManager = LoadSceneManagerFromConfig();  // SceneConfig, with its node files
        _sceneManager->Initialize();
    }

    void CoreManager::StartFileWatching(PassKey<Application>, bool enabled) {
        if (enabled) {
            _fileWatcher->Start(MakePlatformFileWatchBackend());
            _configManager->StartWatching(*_fileWatcher);
        }
    }

    void CoreManager::OnGameCycle(PassKey<Application>, FrameKind kind) {
        BE_PROFILE_SCOPE("Game cycle");
        {
            // Update: input, commands, tasks, tweens and the logic (FrameStage::Update; the scene's part is SceneUpdate)
            const FrameStatsCollector::StageScope update(*_frameStats, FrameStage::Update);
            {
                BE_PROFILE_SCOPE("Input");
                _eventsProviderManager->ProcessEvents();
                _clipboard->Update();  // the answers of the reads and the pastes of the page, after the input that asked for them
                _fileWatcher->Update();
            }
            _controlService->Drain();  // commands of the control API see this frame's input and run before the tasks and the logic
            UpdateGame(kind);
            {
                // Every kind of frame: a paused game keeps its music fed (a held frame has a frame time of 0, so the fades stand still)
                BE_PROFILE_SCOPE("Audio");
                _audioManager->Update(_time->GetDeltaTime());
            }
        }
        // Render: the widgets (Editor: ViewportWidget draws the scene in its update), queued events, the screen draws and ImGui
        const FrameStatsCollector::StageScope render(*_frameStats, FrameStage::Render);
        {
            BE_PROFILE_SCOPE("Widgets update");
            _widgetManager->UpdateAll();  // clear, build the ImGui frame (Editor: ViewportWidget draws the scene)
        }
        {
            BE_PROFILE_SCOPE("Queued events");
            EventsQueueRegistry::UpdateAll();
        }
        {
            BE_PROFILE_SCOPE("Draw screen");
            _widgetManager->DrawScreenAll();  // Game: SceneRenderWidget draws the scene into the back buffer
        }
        {
            BE_PROFILE_SCOPE("Draw widgets");
            _widgetManager->DrawAll();  // ImGui::Render; the present follows in Application (RenderPresentEvent)
        }
    }

    void CoreManager::UpdateGame(FrameKind kind) {
        if (kind == FrameKind::Held) {
            return;  // a paused game: a task that finished on a worker reaches the game with the next game frame
        }
        // The game clock of the delayed tasks gets the same frame delta as the tweens below (the clamp and the manual clock apply)
        TaskManager::GetInstance().AdvanceGameTime(PassKey<CoreManager>{}, _time->GetDeltaTime());
        {
            BE_PROFILE_SCOPE("Tasks");
            TaskManager::GetInstance().Update(PassKey<CoreManager>{});
        }
        // Values are set before the logic of the frame reads them. The frame time is clamped to Time::kMaxFrameTime, so a pause does not finish every tween at once;
        // the manual clock gives the same ticks, and OnIdleCycle runs no frame and advances nothing
        {
            BE_PROFILE_SCOPE("Tweens");
            _tweenManager->Advance(_time->GetDeltaTime());
        }
        {
            BE_PROFILE_SCOPE("Scene update");
            BE_MEM_TAG(Game);
            const FrameStatsCollector::StageScope sceneUpdate(*_frameStats, FrameStage::SceneUpdate);
            _sceneManager->UpdateAll();  // game logic before UI
        }
    }

    void CoreManager::OnIdleCycle(PassKey<Application>) {
        _eventsProviderManager->ProcessEvents();  // the window keeps answering and a close still quits
        _fileWatcher->Update();                   // a native backend posts from its own thread: without a take the queue would grow while the clock stands still
        _controlService->Drain();
        _audioManager->Update(0.0f);  // the manual clock between steps: the music is still fed, its fades wait for game time
        if (!_controlService->IsStepDue()) {
            _controlService->WaitForRequests(kIdleWait);
        }
    }

    void CoreManager::OnApplicationDeinit(PassKey<Application>) {
        BE_PROFILE_SCOPE("CoreManager::OnApplicationDeinit");
        _tweenManager->Clear();     // no callbacks: the scenes and what the setters point at may be going away
        _audioManager->Shutdown();  // a backend still open here (none closed it at the clean-up) is closed before the task system goes
        _controlService->Shutdown();
        _configManager->StopWatching();
        _fileWatcher->Stop();
        TaskManager::GetInstance().Shutdown(PassKey<CoreManager>{});
    }

}  // namespace BECore
