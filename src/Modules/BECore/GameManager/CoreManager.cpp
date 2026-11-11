#include "CoreManager.h"

#include <BECore/Assert/CRTDebugHook.h>
#include <BECore/Config/ConfigFactory.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Logger/OutputSink.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/UI/UIControl.h>
#include <Events/EventsQueueRegistry.h>
#include <TaskSystem/TaskManager.h>

namespace BECore {

    namespace {

        /// Longest wait of an idle cycle: how late the window events and the requests of a client are looked at
        constexpr std::chrono::milliseconds kIdleWait{8};

    }  // namespace

    const IntrusivePtr<IMainWindow>& CoreManager::GetMainWindow() {
        return GetInstance()._mainWindowManager.GetMainWindow();
    }

    TaskManager& CoreManager::GetTaskManager() {
        return TaskManager::GetInstance();
    }

    void CoreManager::OnApplicationPreInit(PassKey<Application>) {
        InitializeCoreServices();
        _testManager.RunAllTests();
    }

    void CoreManager::OnTestEnvironmentInit(PassKey<Tests::EngineEnvironment>) {
        InitializeCoreServices();

        // A failed assert has to be reported by the test runner, not trap the whole test process
        if (auto* debugBreak = _assertHandlerManager->GetHandler<DebugBreakHandler>()) {
            debugBreak->SetEnabled(false);
        }
    }

    void CoreManager::OnTestEnvironmentDeinit(PassKey<Tests::EngineEnvironment>) {
        _tweenManager.Clear();  // no callbacks: what they point at may be gone
        _controlService.Shutdown();
        TaskManager::GetInstance().Shutdown(PassKey<CoreManager>{});
    }

    void CoreManager::SetSceneManagerForTest(IntrusivePtrAtomic<SceneManager> manager, PassKey<Tests::EngineEnvironment>) {
        _sceneManager = std::move(manager);
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
        _fontAtlasLibrary.Initialize();
        _uiEnvironment.Initialize();
        _uiStrings.Initialize();

        RegisterControlBuiltins(_controlService);
        RegisterUIControl(_controlService);
    }

    void CoreManager::OnApplicationInit(PassKey<Application>) {
        _widgetManager.CreateWidgets();
        _sceneManager = LoadSceneManagerFromConfig();  // SceneConfig, with its node files
        _sceneManager->Initialize();
    }

    void CoreManager::OnGameCycle(PassKey<Application>) {
        {
            BE_PROFILE_SCOPE("Input");
            _eventsProviderManager.ProcessEvents();
        }
        BE_PROFILE_SCOPE("Game cycle");
        _controlService.Drain();  // commands of the control API see this frame's input and run before the tasks and the logic
        // The game clock of the delayed tasks gets the same frame delta as the tweens below (the clamp and the manual clock apply)
        TaskManager::GetInstance().AdvanceGameTime(PassKey<CoreManager>{}, _time.GetDeltaTime());
        {
            BE_PROFILE_SCOPE("Tasks");
            TaskManager::GetInstance().Update(PassKey<CoreManager>{});
        }
        // Values are set before the logic of the frame reads them. The frame time is clamped to Time::kMaxFrameTime, so a pause does not finish every tween at once;
        // the manual clock gives the same ticks, and OnIdleCycle runs no frame and advances nothing
        {
            BE_PROFILE_SCOPE("Tweens");
            _tweenManager.Advance(_time.GetDeltaTime());
        }
        {
            BE_PROFILE_SCOPE("Scene update");
            _sceneManager->UpdateAll();  // game logic before UI
        }
        {
            BE_PROFILE_SCOPE("Widgets update");
            _widgetManager.UpdateAll();  // clear, build the ImGui frame (Editor: ViewportWidget draws the scene)
        }
        {
            BE_PROFILE_SCOPE("Queued events");
            EventsQueueRegistry::UpdateAll();
        }
        {
            BE_PROFILE_SCOPE("Draw screen");
            _widgetManager.DrawScreenAll();  // Game: SceneRenderWidget draws the scene into the back buffer
        }
        {
            BE_PROFILE_SCOPE("Draw and present");
            _widgetManager.DrawAll();  // ImGui::Render + GPU present
        }
    }

    void CoreManager::OnIdleCycle(PassKey<Application>) {
        _eventsProviderManager.ProcessEvents();  // the window keeps answering and a close still quits
        _controlService.Drain();
        if (!_controlService.IsStepDue()) {
            _controlService.WaitForRequests(kIdleWait);
        }
    }

    void CoreManager::OnApplicationDeinit(PassKey<Application>) {
        _tweenManager.Clear();  // no callbacks: the scenes and what the setters point at may be going away
        _controlService.Shutdown();
        TaskManager::GetInstance().Shutdown(PassKey<CoreManager>{});
    }

}  // namespace BECore
