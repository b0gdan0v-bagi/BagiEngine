#include "Application.h"

#include <Core/Application/ApplicationFabric.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Core/FileSystem/FileSystem.h>
#include <Core/GameManager/CoreManager.h>
#include <Core/PoolString/PoolStringTest.h>
#include <Core/Format/FormatTest.h>

namespace Core {

    bool Application::Initialize(PassKey<ApplicationMainAccess>) {

        Tests::PoolStringTest();
        Tests::FormatTest();

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