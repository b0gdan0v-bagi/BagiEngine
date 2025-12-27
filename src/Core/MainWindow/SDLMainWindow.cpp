#include "SDLMainWindow.h"

#include <Core/MainWindow/SDLRendererHolder.h>

namespace Core {

    SDLMainWindow::SDLMainWindow() = default;

    SDLMainWindow::~SDLMainWindow() = default;

    bool SDLMainWindow::Create(const char* title, int width, int height, unsigned int flags) {

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return false;
        }

        if (_window != nullptr) {
            // Окно уже создано
            return false;
        }

        _window = SDL_CreateWindow(title, width, height, flags);
        if (_window == nullptr) {
            return false;
        }

        auto rendererHolder = Core::New<SDLRendererHolder>();
        if (!rendererHolder) {
            return false;
        }
        rendererHolder->Create(*this);
        _renderer = rendererHolder;

        _width = width;
        _height = height;
        return true;
    }

    void SDLMainWindow::Destroy() {
        if (_window != nullptr) {
            SDL_DestroyWindow(_window);
            _window = nullptr;
        }
        if (_renderer) {
            _renderer->Destroy();
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

    void SDLMainWindow::SetRenderDrawColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
        if (_renderer) {
            _renderer->SetRenderDrawColor(r, g, b, a);
        }
    }

    void SDLMainWindow::RenderClear() {
        if (_renderer) {
            _renderer->RenderClear();
        }
    }

    void SDLMainWindow::RenderPresent() {
        if (_renderer) {
            _renderer->RenderPresent();
        }
    }
}  // namespace Core
