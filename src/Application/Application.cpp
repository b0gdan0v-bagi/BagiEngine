#include "Application.h"

#include <BECore/Application/ApplicationFabric.h>
#include <BECore/Assert/AssertMacros.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <BECore/GameManager/CoreManager.h>

namespace BECore {

    bool Application::Initialize(PassKey<ApplicationMainAccess>) {

        _isRunning = true;
        ApplicationEvents::QuitEvent::Subscribe<&Application::StopApplication>(this);

        CoreManager::GetInstance().OnApplicationPreInit({});

        if (!ApplicationFabric::GetInstance().Create()) {
            return false;
        }

        CoreManager::GetInstance().OnApplicationInit({});

        return _isRunning;
    }

    void Application::Run(PassKey<ApplicationMainAccess>) const {

        while (_isRunning) {

            RenderEvents::NewFrameEvent::Emit();
            CoreManager::GetInstance().OnGameCycle({});
            RenderEvents::RenderPresentEvent::Emit();
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