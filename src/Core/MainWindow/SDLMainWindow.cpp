#include "SDLMainWindow.h"

SDLMainWindow::SDLMainWindow() = default;

SDLMainWindow::~SDLMainWindow() = default;

bool SDLMainWindow::Create(const char* title, int width, int height, unsigned int flags) {
    if (_window != nullptr) {
        // Окно уже создано
        return false;
    }

    _window = SDL_CreateWindow(title, width, height, flags);
    if (_window == nullptr) {
        return false;
    }

    _width = width;
    _height = height;
    return true;
}

void SDLMainWindow::Destroy() {
    if (_window != nullptr) {
        SDL_DestroyWindow(_window);
        _window = nullptr;
    }
    _width = 0;
    _height = 0;
}

bool SDLMainWindow::IsValid() const {
    return _window != nullptr;
}

void* SDLMainWindow::GetNativeWindow() const {
    return static_cast<void*>(_window);
}

int SDLMainWindow::GetWidth() const {
    return _width;
}

int SDLMainWindow::GetHeight() const {
    return _height;
}

bool SDLMainWindow::CreateRenderer() {
    if (_renderer != nullptr) {
        // Рендерер уже создан
        return false;
    }

    if (_window == nullptr) {
        // Окно не создано
        return false;
    }

    _renderer = SDL_CreateRenderer(_window, nullptr);
    return _renderer != nullptr;
}

void SDLMainWindow::DestroyRenderer() {
    if (_renderer != nullptr) {
        SDL_DestroyRenderer(_renderer);
        _renderer = nullptr;
    }
}

void SDLMainWindow::SetRenderDrawColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (_renderer != nullptr) {
        SDL_SetRenderDrawColor(_renderer, r, g, b, a);
    }
}

void SDLMainWindow::RenderClear() {
    if (_renderer != nullptr) {
        SDL_RenderClear(_renderer);
    }
}

void SDLMainWindow::RenderPresent() {
    if (_renderer != nullptr) {
        SDL_RenderPresent(_renderer);
    }
}

