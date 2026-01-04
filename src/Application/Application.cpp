#include "Application.h"

#include <Core/Application/ApplicationFabric.h>
#include <Core/Events/ApplicationEvents.h>
#include <Core/Events/RenderEvents.h>
#include <Core/FileSystem/FileSystem.h>

namespace Core {

    bool Application::Initialize(PassKey<ApplicationMainAccess>) {

        _isRunning = true;
        ApplicationEvents::QuitEvent::Subscribe<&Application::StopApplication>(this);

        FileSystem::GetInstance().Initialize();

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

}  // namespace Core