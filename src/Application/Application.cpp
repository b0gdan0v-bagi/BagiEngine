#include "Application.h"

#include <Application/ApplicationSDLFabric.h>
#include <Core/Config/XmlConfig.h>
#include <Core/Events/Events.h>
#include <Core/Events/SDLEventsProvider.h>
#include <Core/FileSystem/FileSystem.h>

namespace Core {

    bool Application::Initialize(PassKey<ApplicationMainAccess>) {

        FileSystem::GetInstance().Initialize();

        XmlConfig config;
        std::string type;

        if (config.LoadFromVirtualPath("config/ApplicationConfig.xml")) {
            type = config.Get<std::string>("root.type");
        } else {
            return false;
        }

        if (type == "SDL3") {
            if (!ApplicationSDLFabric::Create(config)) {
                return false;
            }
        } else {
            return false;
        }

        QuitEvent::Subscribe<&Application::StopApplication>(this);

        _widgetManager.CreateWidgets(config);

        _isRunning = true;
        return _isRunning;
    }

    void Application::Run(PassKey<ApplicationMainAccess>) {

        while (_isRunning) {

            NewFrameEvent::Emit();
            _eventsProviderManager.ProcessEvents();
            _widgetManager.UpdateAll();
            EventsQueueRegistry::UpdateAll();
            SetRenderDrawColorEvent::Emit(Math::Color{20, 20, 100, 255});
            RenderClearEvent::Emit();

            _widgetManager.DrawAll();

            RenderPresentEvent::Emit();
        }
    }

    void Application::Cleanup(PassKey<ApplicationMainAccess>) {
        ApplicationCleanUpEvent::Emit();

        if (_window) {
            _window.Reset();
        }
    }

    void Application::SetMainWindow(IntrusivePtr<IMainWindow> window, PassKey<ApplicationMainAccess>) {
        _window = std::move(window);
    }

    void Application::StopApplication() {
        _isRunning = false;
    }

}  // namespace Core