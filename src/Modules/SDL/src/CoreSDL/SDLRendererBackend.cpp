#include "SDLRendererBackend.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <CoreSDL/SDLMainWindow.h>

#include <SDL3/SDL.h>

#include <Generated/SDLRendererBackend.gen.hpp>

namespace BECore {

    bool SDLRendererBackend::Initialize(IMainWindow& window) {
        auto* sdlWindow = dynamic_cast<SDLMainWindow*>(&window);
        if (!sdlWindow) {
            return false;
        }

        _renderer = SDL_CreateRenderer(sdlWindow->GetSDLWindow(), nullptr);
        if (!_renderer) {
            return false;
        }

        Subscribe<RenderEvents::RenderClearEvent, &SDLRendererBackend::OnRenderClear>(this);
        Subscribe<RenderEvents::RenderPresentEvent, &SDLRendererBackend::OnRenderPresent>(this);
        Subscribe<RenderEvents::SetRenderDrawColorEvent, &SDLRendererBackend::OnSetRenderDrawColor>(this);
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &SDLRendererBackend::Destroy>(this);

        return true;
    }

    void SDLRendererBackend::Destroy() {
        if (_renderer) {
            SDL_DestroyRenderer(_renderer);
            _renderer = nullptr;
        }
    }

    void SDLRendererBackend::BeginFrame() {
    }

    void SDLRendererBackend::EndFrame() {
    }

    void SDLRendererBackend::Clear(const Color& color) {
        if (_renderer) {
            SDL_SetRenderDrawColor(_renderer, color.r, color.g, color.b, color.a);
            SDL_RenderClear(_renderer);
        }
    }

    void SDLRendererBackend::Present() {
        if (_renderer) {
            SDL_RenderPresent(_renderer);
        }
    }

    void SDLRendererBackend::OnRenderClear() {
        if (_renderer) {
            SDL_RenderClear(_renderer);
        }
    }

    void SDLRendererBackend::OnRenderPresent() {
        if (_renderer) {
            SDL_RenderPresent(_renderer);
        }
    }

    void SDLRendererBackend::OnSetRenderDrawColor(const RenderEvents::SetRenderDrawColorEvent& event) {
        if (_renderer) {
            SDL_SetRenderDrawColor(_renderer, event.color.r, event.color.g, event.color.b, event.color.a);
        }
    }

}  // namespace BECore
