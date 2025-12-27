#include "Application.h"

#include <Core/ImGui/SDL3imGuiManager.h>
#include <Core/ImGui/IimGuiManager.h>
#include <Core/MainWindow/SDLMainWindow.h>
#include <Core/Utils/New.h>
#include <Core/Config/XmlConfig.h>
#include <Core/FileSystem/FileSystem.h>
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

        auto sdlManager = Core::New<SDL3imGuiManager>();
        sdlManager->Initialize();
        _imguiManager = sdlManager;

        _widgetManager.CreateWidgets(config);

        _isRunning = true;
        return true;
    }

    void Application::Run() {

        SDL_Event event;

        while (_isRunning) {
            if (_imguiManager) {
                _imguiManager->NewFrame();
            }

            // Обработка событий
            while (SDL_PollEvent(&event)) {
                // Передача событий в ImGui
                if (_imguiManager) {
                    _imguiManager->ProcessEvent(&event);
                }

                if (event.type == SDL_EVENT_QUIT) {
                    _isRunning = false;
                }
            }

            // Заливка экрана цветом из конфига (RGBA — Красный, Зеленый, Синий, Альфа)
            _window->SetRenderDrawColor(20, 20, 100, 255);
            _window->RenderClear();

            _widgetManager.DrawAll();

            if (_imguiManager) {
                _imguiManager->Render();
            }

            // Показываем результат
            _window->RenderPresent();
        }
    }

    void Application::Cleanup() {
        // 4. Завершение
        if (_imguiManager) {
            _imguiManager->Destroy();
            _imguiManager.Reset();
        }

        if (_window) {
            _window->Destroy();
            _window.Reset();
        }
        SDL_Quit();
    }

    void Application::StopApplication() {
        _isRunning = false;
    }

}  // namespace Core