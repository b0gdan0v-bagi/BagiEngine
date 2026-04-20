#include "Application.h"

#include <BECore/Application/ApplicationFabric.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Games/GameManager.h>

namespace BECore {

    bool Application::Initialize() {

        _isRunning = true;
        Subscribe<ApplicationEvents::QuitEvent, &Application::StopApplication>(this);

        CoreManager::GetInstance().OnApplicationPreInit({});

        LOG_INFO("Game started");

        if (!ApplicationFabric::GetInstance().Create()) {
            return false;
        }

        CoreManager::GetInstance().OnApplicationInit({});

        // Boot the game layer once core systems (config, scenes, widgets) are up.
        GameManager::GetInstance().Initialize();

        return _isRunning;
    }

    void Application::Run() const {

        while (_isRunning) {

            RenderEvents::NewFrameEvent::Emit();
            CoreManager::GetInstance().OnGameCycle({});
            RenderEvents::RenderPresentEvent::Emit();
            LogEvent::Flush();
        }
        GetInstance().Cleanup();
    }

    void Application::Cleanup() {
        GameManager::GetInstance().Shutdown();
        ApplicationEvents::ApplicationCleanUpEvent::Emit();
        CoreManager::GetInstance().OnApplicationDeinit({});
    }

    void Application::StopApplication() {
        _isRunning = false;
    }

}  // namespace BECore
