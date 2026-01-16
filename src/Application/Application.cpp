#include "Application.h"

#include <Core/Application/ApplicationFabric.h>
#include <Core/Assert/AssertMacros.h>
#include <Core/Assert/AssertHandlers.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Core/FileSystem/FileSystem.h>
#include <Core/GameManager/CoreManager.h>
#include <Core/Tests/TestManager.h>

namespace Core {

    bool Application::Initialize(PassKey<ApplicationMainAccess>) {

        _isRunning = true;
        ApplicationEvents::QuitEvent::Subscribe<&Application::StopApplication>(this);
        FileSystem::GetInstance().Initialize();

        InitializeAssertHandlers();

        TestManager::GetInstance().RunAllTests();

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