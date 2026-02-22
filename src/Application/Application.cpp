#include "Application.h"

#include <BECore/Application/ApplicationFabric.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>

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
        ApplicationEvents::ApplicationCleanUpEvent::Emit();
        CoreManager::GetInstance().OnApplicationDeinit({});
    }

    void Application::StopApplication() {
        _isRunning = false;
    }

}  // namespace BECore
