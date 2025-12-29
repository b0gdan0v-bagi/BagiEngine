#include "Application.h"

#include <Application/ApplicationSDLFabric.h>
#include <Core/Config/XmlConfig.h>
#include <Core/Events/ApplicationEvents.h>
#include <Core/Events/RenderEvents.h>
#include <Core/FileSystem/FileSystem.h>

namespace Core {

    bool Application::Initialize(PassKey<ApplicationMainAccess>) {

        FileSystem::GetInstance().Initialize();

        XmlConfig config;
        ApplicationSystemType type;

        if (config.LoadFromVirtualPath("config/ApplicationConfig.xml")) {
            type = config.Get<ApplicationSystemType>("root.type");
        } else {
            return false;
        }

        switch (type) {
            case ApplicationSystemType::SDL3:
                if (!ApplicationSDLFabric::Create(config)) {
                    return false;
                }
                break;
            default:
                return false;
        }

        ApplicationEvents::QuitEvent::Subscribe<&Application::StopApplication>(this);

        _widgetManager.CreateWidgets(config);

        _isRunning = true;
        return _isRunning;
    }

    void Application::Run(PassKey<ApplicationMainAccess>) const {

        while (_isRunning) {

            RenderEvents::NewFrameEvent::Emit();
            _eventsProviderManager.ProcessEvents();
            _widgetManager.UpdateAll();
            EventsQueueRegistry::UpdateAll();
            RenderEvents::SetRenderDrawColorEvent::Emit(Math::Color{20, 20, 100, 255});
            RenderEvents::RenderClearEvent::Emit();

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