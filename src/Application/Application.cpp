#include "Application.h"

#include <Core/Config/XmlConfig.h>
#include <Core/Events/Events.h>
#include <Core/Events/SDLEventsProvider.h>
#include <Core/FileSystem/FileSystem.h>
#include <Core/MainWindow/SDLMainWindow.h>
#include <Core/Utils/New.h>
#include <Widgets/ImGuiWidget.h>
#include <imgui.h>

namespace Core {

    bool Application::Initialize() {

        // Инициализация файловой системы
        FileSystem::GetInstance().Initialize();

        // Загрузка конфига из XML через виртуальный путь
        XmlConfig config;
        std::string type = "SDL3";
        std::string configPath;

        if (config.LoadFromVirtualPath("config/ApplicationConfig.xml")) {
            type = config.Get<std::string>("root.type", "SDL3");
            configPath = config.Get<std::string>("root.path", "");
        } else {
            return false;
        }

        if (type != "SDL3") {
            return false;
        }

        _window = Core::New<SDLMainWindow>();
        if (!_window->Initialize(configPath)) {
            _window.Reset();
            SDL_Quit();
            return false;
        }

        auto sdlEventsProvider = Core::New<SDLEventsProvider>();
        if (!sdlEventsProvider->Initialize()) {
            return false;
        }
        _eventManager.RegisterProvider(sdlEventsProvider);
        _eventManager.Subscribe<QuitEvent, &Application::StopApplication>(this);

        _widgetManager.CreateWidgets(config);

        _isRunning = true;
        return true;
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
        SDL_Quit();
    }

    void Application::StopApplication() {
        _isRunning = false;
    }

}  // namespace Core