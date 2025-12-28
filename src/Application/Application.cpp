#include "Application.h"

#include <Application/ApplicationSDLFabric.h>
#include <Core/Config/XmlConfig.h>
#include <Core/Events/Events.h>
#include <Core/Events/SDLEventsProvider.h>
#include <Core/FileSystem/FileSystem.h>

namespace Core {

    bool Application::Initialize() {

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

        _eventManager.Subscribe<QuitEvent, &Application::StopApplication>(this);

        _widgetManager.CreateWidgets(config);

        _isRunning = true;
        return _isRunning;
    }

    void Application::Run() {

        while (_isRunning) {

            _eventManager.Emit(NewFrameEvent{});
            _eventManager.ProcessEvents();
            _widgetManager.UpdateAll();
            _eventManager.Emit(SetRenderDrawColorEvent{20, 20, 100, 255});
            _eventManager.Emit(RenderClearEvent{});

            _widgetManager.DrawAll();

            _eventManager.Emit(RenderPresentEvent{});
        }
    }

    void Application::Cleanup() {
        _eventManager.Emit(ApplicationCleanUpEvent{});

        if (_window) {
            _window.Reset();
        }
    }

    void Application::SetMainWindow(IntrusivePtr<IMainWindow> window) {
        _window = std::move(window);
    }

    void Application::StopApplication() {
        _isRunning = false;
    }

}  // namespace Core