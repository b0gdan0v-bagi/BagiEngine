#include "Application.h"

#include <Core/ImGui/SDL3imGuiManager.h>
#include <Core/ImGui/IimGuiManager.h>
#include <Core/MainWindow/SDLMainWindow.h>
#include <Core/Utils/New.h>

#include <imgui.h>

namespace Core {

    bool Application::Initialize() {

        _window = Core::New<SDLMainWindow>();
        if (!_window->Create("My SDL3 Window", 800, 600)) {
            _window.Reset();
            SDL_Quit();
            return false;
        }

        auto sdlManager = Core::New<SDL3imGuiManager>();
        sdlManager->Initialize();
        _imguiManager = sdlManager;

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

            // Заливка экрана цветом (RGBA — Красный, Зеленый, Синий, Альфа)
            _window->SetRenderDrawColor(20, 20, 100, 255);  // Темно-синий
            _window->RenderClear();

            // Рендеринг ImGui UI
            if (_imguiManager) {
                // Примеры использования ImGui:
                //
                // 1. Показать демо-окно со всеми виджетами:
                ImGui::ShowDemoWindow();
                //
                // 2. Простое окно:
                // if (ImGui::Begin("My Window")) {
                //     ImGui::Text("Hello, ImGui!");
                //     if (ImGui::Button("Click me")) {
                //         // Обработка нажатия
                //     }
                //     ImGui::End();
                // }

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

}  // namespace Core