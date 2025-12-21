#include "Application.h"
#include "Core/MainWindow/SDLMainWindow.h"
#include "Core/Utils/New.h"

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

    _isRunning = true;
    return true;
}

void Application::Run() {
    // 3. Главный цикл
    SDL_Event event;

    while (_isRunning) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                _isRunning = false;
            }
        }

        // Заливка экрана цветом (RGBA — Красный, Зеленый, Синий, Альфа)
        _window->SetRenderDrawColor(20, 20, 100, 255); // Темно-синий
        _window->RenderClear();

        // Показываем результат
        _window->RenderPresent();
    }
}

void Application::Cleanup() {
    // 4. Завершение
    if (_window) {
        SDLMainWindow* sdlWindow = static_cast<SDLMainWindow*>(_window.Get());
        sdlWindow->DestroyRenderer();
        _window->Destroy();
        _window.Reset();
    }
    SDL_Quit();
}

