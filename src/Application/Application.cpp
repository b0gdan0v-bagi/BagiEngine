#include "Application.h"

#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Application/ApplicationFabric.h>
#include <BECore/Application/RunMode.h>
#include <BECore/Control/ControlSettings.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Games/GameManager.h>
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
    }

    bool Application::Initialize() {

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

        // The control fields stopped the start-up above; what is left is every other field (`mode`, `type`, `renderer`, `path`)
        LogDeserializeErrors("ApplicationConfig", load.errors);

        // Before the first frame: the loop reads it from the start
        CoreManager::GetControlService().SetManualClock(controlSettings->manualClock);

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
            return false;
        }

        CoreManager::GetInstance().OnApplicationInit({});

        // Boot the game layer once core systems (config, scenes, widgets) are up.
        GameManager::GetInstance().Initialize();

        // Last, so that the methods of the game layer are registered before the first client can ask for them
#if BE_WITH_REMOTE_CONTROL
        if (controlSettings->enabled && !StartControlServer(controlSettings->port)) {
            return false;
        }
#else
        if (controlSettings->enabled) {
            LOG_ERROR("Control API: not available in this build");
            LogEvent::Flush();
            return false;
        }
#endif

        return _isRunning;
    }

    void Application::Run() {

        ControlService& control = CoreManager::GetControlService();

        while (_isRunning) {

            if (control.IsManualClock()) {
                // Game time moves only for the frames that a `step` asks for; in between the window and the requests stay alive
                if (!control.IsStepDue()) {
                    CoreManager::GetInstance().OnIdleCycle({});
                    LogEvent::Flush();
                    continue;
                }
                CoreManager::GetTime().Advance(control.GetStepSeconds());
            } else {
                CoreManager::GetTime().Tick({});
            }
            RenderEvents::NewFrameEvent::Emit();
            CoreManager::GetInstance().OnGameCycle({});
            RenderEvents::RenderPresentEvent::Emit();
#if BE_WITH_EDITOR
            // The only safe point to replace the widgets: the frame is over, so no widget is inside its own call and no ImGui frame is open.
            // The manual-clock idle branch above never gets here, because no frame is open there either
            CoreManager::GetWidgetManager().ApplyPendingMode();
#endif
            LogEvent::Flush();
            control.EndFrame();
        }
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
        _controlServer = eastl::make_unique<WebSocketServer>(CoreManager::GetControlService());
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
    }

    void Application::StopApplication() {
        _isRunning = false;
    }

}  // namespace BECore
