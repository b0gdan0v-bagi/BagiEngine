#include "Application.h"

#include <Core/Application/ApplicationFabric.h>
#include <Core/Events/ApplicationEvents.h>
#include <Core/Events/EventsQueueRegistry.h>
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

        _widgetManager.CreateWidgets();

        return _isRunning;
    }

    void Application::Run(PassKey<ApplicationMainAccess>) const {

        while (_isRunning) {

            RenderEvents::NewFrameEvent::Emit();
            _eventsProviderManager.ProcessEvents();
            _widgetManager.UpdateAll();
            EventsQueueRegistry::UpdateAll();
            _widgetManager.DrawAll();

            RenderEvents::RenderPresentEvent::Emit();
        }
    }

    void Application::Cleanup(PassKey<ApplicationMainAccess>) {
        ApplicationEvents::ApplicationCleanUpEvent::Emit();
    }

    void Application::SetMainWindow(IntrusivePtr<IMainWindow> window, PassKey<ApplicationMainAccess>) {
        _window = std::move(window);
    }

    void Application::StopApplication() {
        _isRunning = false;
    }

}  // namespace Core