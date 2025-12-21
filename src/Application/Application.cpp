#include "Application.h"
#include "Core/MainWindow/SDLMainWindow.h"
#include "Core/ImGui/ImGuiManager.h"
#include "Core/Utils/New.h"
#include <imgui.h>

Application::~Application() {
    Cleanup();
}

bool Application::Initialize() {

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return false;
    }

    _window = IntrusivePtr<IMainWindow>(Core::New<SDLMainWindow>());
    if (!_window->Create("My SDL3 Window", 800, 600)) {
        _window.Reset();
        SDL_Quit();
        return false;
    }

    if (!_window->CreateRenderer()) {
        _window->Destroy();
        _window.Reset();
        SDL_Quit();
        return false;
    }

    // Инициализация ImGui
    SDLMainWindow* sdlWindow = static_cast<SDLMainWindow*>(_window.Get());
    _imguiManager = new ImGuiManager();
    if (!_imguiManager->Initialize(sdlWindow->GetSDLWindow(), sdlWindow->GetRenderer())) {
        delete _imguiManager;
        _imguiManager = nullptr;
        _window->DestroyRenderer();
        _window->Destroy();
        _window.Reset();
        SDL_Quit();
        return false;
    }

    _isRunning = true;
    return true;
}

void Application::Run() {
    // 3. Главный цикл
    SDL_Event event;

    while (_isRunning) {
        // Начало нового кадра ImGui
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
        _window->SetRenderDrawColor(20, 20, 100, 255); // Темно-синий
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
        _imguiManager->Shutdown();
        delete _imguiManager;
        _imguiManager = nullptr;
    }

    if (_window) {
        SDLMainWindow* sdlWindow = static_cast<SDLMainWindow*>(_window.Get());
        sdlWindow->DestroyRenderer();
        _window->Destroy();
        _window.Reset();
    }
    SDL_Quit();
}

