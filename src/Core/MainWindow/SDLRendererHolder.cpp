#include "SDLRendererHolder.h"

#include <Core/MainWindow/SDLMainWindow.h>

namespace Core {

    SDLRendererHolder::SDLRendererHolder() = default;

    SDLRendererHolder::~SDLRendererHolder() = default;

    bool SDLRendererHolder::Create(const IMainWindow& window) {

        if (auto* mainWindowHolder = dynamic_cast<const SDLMainWindow*>(&window)) {
            if (auto* SDLWindow = mainWindowHolder->GetSDLWindow()) {
                _renderer = SDL_CreateRenderer(SDLWindow, nullptr);
            }
        }
        return false;
    }

    void SDLRendererHolder::Destroy() {
        if (_renderer != nullptr) {
            SDL_DestroyRenderer(_renderer);
            _renderer = nullptr;
        }
    }

    void SDLRendererHolder::SetRenderDrawColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
        if (_renderer != nullptr) {
            SDL_SetRenderDrawColor(_renderer, r, g, b, a);
        }
    }

    void SDLRendererHolder::RenderClear() {
        if (_renderer != nullptr) {
            SDL_RenderClear(_renderer);
        }
    }

    void SDLRendererHolder::RenderPresent() {
        if (_renderer != nullptr) {
            SDL_RenderPresent(_renderer);
        }
    }
}  // namespace Core
