#include "Application.h"

#include <BECore/Application/ApplicationFabric.h>
#include <BECore/Application/RunMode.h>
#include <BECore/Control/ControlSettings.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Games/GameManager.h>
#include <RemoteControl/WebSocketServer.h>

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

        CoreManager::GetInstance().OnApplicationPreInit({});

        LOG_INFO("Game started");

        // Before the window exists: a flag that cannot be read should not cost a window and a loaded scene.
        // Nothing flushes the log before the first frame, so an error that stops the start-up flushes it itself.
        const auto controlSettings = ResolveControlSettings(_arguments, CoreManager::GetConfigManager().GetConfig("ApplicationConfig"_intern));
        if (!controlSettings) {
            LOG_ERROR("Control API: {}"_format(controlSettings.error().message));
            LogEvent::Flush();
            return false;
        }

        // Before the first frame: the loop reads it from the start
        CoreManager::GetControlService().SetManualClock(controlSettings->manualClock);

        // The editor draws its viewport into an offscreen target, which the software renderer of a headless run does not have
        const auto& applicationConfig = CoreManager::GetConfigManager().GetConfig("ApplicationConfig"_intern);
        if (controlSettings->headless && ReadRunMode(applicationConfig) == RunMode::Editor) {
            LOG_ERROR("Headless: the Editor run mode cannot run without a window; set mode=\"Game\" in ApplicationConfig.xml");
            LogEvent::Flush();
            return false;
        }

        ApplicationOptions options;
        options.headless = controlSettings->headless;
        if (!ApplicationFabric::GetInstance().Create(options)) {
            return false;
        }

        CoreManager::GetInstance().OnApplicationInit({});

        // Boot the game layer once core systems (config, scenes, widgets) are up.
        GameManager::GetInstance().Initialize();

        // Last, so that the methods of the game layer are registered before the first client can ask for them
        if (controlSettings->enabled && !StartControlServer(controlSettings->port)) {
            return false;
        }

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
            LogEvent::Flush();
            control.EndFrame();
        }
        Cleanup();
    }

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

    void Application::Cleanup() {
        // The transport stops before ControlService::Shutdown (OnApplicationDeinit), as Control/CLAUDE.md asks
        if (_controlServer) {
            _controlServer->Stop();
            _controlServer.reset();
        }
        GameManager::GetInstance().Shutdown();
        // The glyph pages are GPU textures: they go before the renderer's own cleanup destroys the device
        CoreManager::GetFontAtlasLibrary().Clear();
        ApplicationEvents::ApplicationCleanUpEvent::Emit();
        CoreManager::GetInstance().OnApplicationDeinit({});
    }

    void Application::StopApplication() {
        _isRunning = false;
    }

}  // namespace BECore
