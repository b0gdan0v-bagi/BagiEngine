#include "Application.h"

#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Application/ApplicationFabric.h>
#include <BECore/Application/RunMode.h>
#include <BECore/Control/ControlSettings.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/MainWindow/WindowTitle.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerSettings.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Games/GameManager.h>
#include <TaskSystem/TaskManager.h>
#if BE_WITH_REMOTE_CONTROL
#include <RemoteControl/WebSocketServer.h>
#endif

namespace BECore {

    Application::Application() = default;
    Application::~Application() = default;

    void Application::SetArguments(int argc, const char* const* argv) {
        _arguments.clear();
        for (int i = 1; i < argc && argv != nullptr; ++i) {
            _arguments.emplace_back(argv[i]);
        }
        CoreManager::GetCommandLine().Set(_arguments);
    }

    bool Application::Initialize() {

        // First: a trace covers the start-up, and a profiler flag that cannot be honoured stops the start before anything is built
        BE_PROFILE_THREAD_ORDER("Main", 0);
        const auto profilerSettings = ResolveProfilerSettings(_arguments);
        if (!profilerSettings) {
            LOG_ERROR("Profiler: {}"_format(profilerSettings.error()));
            LogEvent::Flush();
            return false;
        }
        if (const auto started = Profiler::Start(*profilerSettings); !started) {
            LOG_ERROR("Profiler: {}"_format(started.error()));
            LogEvent::Flush();
            return false;
        }

        _isRunning = true;
        Subscribe<ApplicationEvents::QuitEvent, &Application::StopApplication>(this);
#if BE_WITH_EDITOR
        Subscribe<SDLEvents::SDLEventWrapper, &Application::OnSDLEvent>(this);
#endif

        CoreManager::GetInstance().OnApplicationPreInit({});

        LOG_INFO("Game started");

        // Before the window exists: a flag that cannot be read should not cost a window and a loaded scene.
        // Nothing flushes the log before the first frame, so an error that stops the start-up flushes it itself.
        const ApplicationConfigLoad load = LoadApplicationConfig();
        const auto controlSettings = ResolveControlSettings(_arguments, load);
        if (!controlSettings) {
            LOG_ERROR("Control API: {}"_format(controlSettings.error().message));
            LogEvent::Flush();
            return false;
        }

#if !BE_WITH_REMOTE_CONTROL
        if (controlSettings->enabled) {
            LOG_ERROR("Control API: not available in this build");
            LogEvent::Flush();
            return false;
        }
#endif

        // The control fields stopped the start-up above; what is left is every other field (`mode`, `type`, `renderer`, `path`)
        LogDeserializeErrors("ApplicationConfig", load.errors);

        if (load.config._taskFrameBudgetMs >= 0.0) {
            CoreManager::GetTaskManager().SetFrameBudget(std::chrono::duration_cast<TaskManager::Duration>(std::chrono::duration<double, std::milli>(load.config._taskFrameBudgetMs)));
        }

        // Before the first frame: the loop reads it from the start
        CoreManager::GetControlService().SetManualClock(controlSettings->manualClock);
        if (controlSettings->manualClock) {
            CoreManager::GetPause().SetPaused(true);  // a manual clock is a pause that only `step` advances
        }

        // The editor draws its viewport into an offscreen target, which the software renderer of a headless run does not have
        // The resolved mode (ResolveRunMode) without its warning: CreateWidgets resolves and warns once
        if (controlSettings->headless && kEditorBuilt && load.config._mode == RunMode::Editor) {
            LOG_ERROR("Headless: the Editor run mode cannot run without a window; set mode=\"Game\" in ApplicationConfig.xml");
            LogEvent::Flush();
            return false;
        }

        _headless = controlSettings->headless;

        ApplicationOptions options;
        options.headless = controlSettings->headless;
        if (!ApplicationFabric::GetInstance().Create(load.config, options)) {
            // The reason is in the log buffer; there is no frame to deliver it to the sinks
            LogEvent::Flush();
            return false;
        }

        CoreManager::GetInstance().OnApplicationInit({});

        // Boot the game layer once core systems (config, scenes, widgets) are up.
        GameManager::GetInstance().Initialize();
        UpdateWindowTitle();

        // Last, so that the methods of the game layer are registered before the first client can ask for them
#if BE_WITH_REMOTE_CONTROL
        if (controlSettings->enabled && !StartControlServer(controlSettings->port)) {
            return false;
        }
#endif

        return _isRunning;
    }

    void Application::Run() {
        while (RunFrame()) {
        }
        Finish();
    }

    bool Application::RunFrame() {

        ControlService& control = CoreManager::GetControlService();

        GamePause& pause = CoreManager::GetPause();
        Time& time = CoreManager::GetTime();
        if (control.IsManualClock() && !pause.IsStepDue()) {
            // Game time moves only for the frames that a `step` asks for; in between the window and the requests stay alive
            BE_PROFILE_SCOPE("Idle");
            CoreManager::GetInstance().OnIdleCycle({});
            PublishControlStatus();
            LogEvent::Flush();
            return _isRunning;
        }
        // The first thing of a frame: what kind of frame it is decides the clock and, in OnGameCycle, what the game does
        const FrameDecision frame = pause.BeginFrame();
        switch (frame.kind) {
            case FrameKind::Running:
                time.Tick({});
                break;
            case FrameKind::Stepped:
                time.Advance(frame.seconds);
                break;
            case FrameKind::Held:
                time.Hold();
                break;
        }
        BE_PROFILE_SCOPE("Frame");
        FrameStatsCollector& frameStats = CoreManager::GetFrameStats();
        frameStats.BeginFrame(frame.kind == FrameKind::Held);
        RenderEvents::NewFrameEvent::Emit();
        CoreManager::GetInstance().OnGameCycle({}, frame.kind);
        {
            BE_PROFILE_SCOPE("Present");
            const FrameStatsCollector::StageScope present(frameStats, FrameStage::Present);
            RenderEvents::RenderPresentEvent::Emit();
        }
        {
            const IntrusivePtr<IRenderer>& renderer = CoreManager::GetRenderer();
            frameStats.EndFrame(renderer ? renderer->TakeRenderCounters() : RenderCounters{}, AllocTracker::GetTotals());
        }
#if BE_WITH_EDITOR
        // The only safe point to replace the widgets: the frame is over, so no widget is inside its own call and no ImGui frame is open.
        // The manual-clock idle branch above never gets here, because no frame is open there either
        CoreManager::GetWidgetManager().ApplyPendingMode();
#endif
        UpdateWindowTitle();  // after ApplyPendingMode: the mode and a game switch both show here
        PublishControlStatus();
        LogEvent::Flush();
        control.EndFrame();
        BE_PROFILE_FRAME();
        return _isRunning;
    }

    void Application::UpdateWindowTitle() {
        const IntrusivePtr<IMainWindow>& window = CoreManager::GetMainWindow();
        if (!window) {
            return;
        }
        const IGame* game = GameManager::GetInstance().GetActive();
        const PoolString gameName = game != nullptr ? game->GetName() : PoolString();
        const bool editorMode = CoreManager::GetWidgetManager().GetMode() == RunMode::Editor;
        if (_titleApplied && gameName == _titleGame && editorMode == _titleEditor) {
            return;
        }
        _titleApplied = true;
        _titleGame = gameName;
        _titleEditor = editorMode;
        window->SetTitle(ComposeWindowTitle(window->GetConfiguredTitle(), gameName.ToStringView(), editorMode));
    }

    void Application::PublishControlStatus() const {
#if BE_WITH_REMOTE_CONTROL
        if (_controlServer) {
            CoreManager::GetControlService().SetServerStatus({_controlServer->IsRunning(), _controlServer->GetPort(), _controlServer->GetClientCount()});
        }
#endif
    }

    void Application::Finish() {
        ASSERT(!_finished, "Application::Finish() must run once");
        _finished = true;
        Cleanup();
    }

#if BE_WITH_EDITOR
    void Application::OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const {
        const SDL_KeyboardEvent& key = event.event.key;
        if (event.event.type != SDL_EVENT_KEY_DOWN || !IsEditorToggle(key.key, key.mod, key.repeat)) {
            return;
        }
        if (_headless) {
            LOG_WARNING("Run mode: Ctrl+Shift+E ignored, a headless run has no window for the editor");
            return;
        }
        CoreManager::GetWidgetManager().RequestToggle();
    }
#endif

#if BE_WITH_REMOTE_CONTROL
    bool Application::StartControlServer(uint16_t port) {
        _controlServer = MakeUnique<WebSocketServer>(CoreManager::GetControlService());
        const auto started = _controlServer->Start(port);
        if (!started) {
            LOG_ERROR("Control API: {}"_format(started.error().message));
            LogEvent::Flush();
            _controlServer.reset();
            return false;
        }
        return true;
    }
#endif

    void Application::Cleanup() {
#if BE_WITH_REMOTE_CONTROL
        // The transport stops before ControlService::Shutdown (OnApplicationDeinit), as Control/CLAUDE.md asks
        if (_controlServer) {
            _controlServer->Stop();
            _controlServer.reset();
            CoreManager::GetControlService().SetServerStatus({});
        }
#endif
        GameManager::GetInstance().Shutdown();
        // The glyph pages and the widgets own GPU objects (a texture, the viewport's target, ImGui's descriptors): they go before
        // the renderer's own cleanup destroys the device. Direct calls, not handlers of ApplicationCleanUpEvent: EnTT calls the
        // handlers in the reverse order of the subscription, so the renderer's Destroy would run first
        CoreManager::GetFontAtlasLibrary().Clear();
        CoreManager::GetWidgetManager().DestroyAll();
        ApplicationEvents::ApplicationCleanUpEvent::Emit();
        CoreManager::GetInstance().OnApplicationDeinit({});
        // After the deinit: the worker threads have been joined, so every zone is complete in the trace
        if (const auto finished = Profiler::Finish(); !finished) {
            LOG_ERROR("Profiler: {}"_format(finished.error()));
        }
        LogEvent::Flush();
    }

    void Application::StopApplication() {
        _isRunning = false;
    }

}  // namespace BECore
