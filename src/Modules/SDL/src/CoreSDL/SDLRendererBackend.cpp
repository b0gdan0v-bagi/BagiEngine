#include "SDLRendererBackend.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <CoreSDL/SDLMainWindow.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Generated/SDLRendererBackend.gen.hpp>
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>

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
        Subscribe<RenderEvents::ImGuiInitEvent, &SDLRendererBackend::OnImGuiInit>(this);
        Subscribe<RenderEvents::ImGuiShutdownEvent, &SDLRendererBackend::OnImGuiShutdown>(this);
        Subscribe<RenderEvents::ImGuiNewFrameEvent, &SDLRendererBackend::OnImGuiNewFrame>(this);
        Subscribe<RenderEvents::ImGuiRenderEvent, &SDLRendererBackend::OnImGuiRender>(this);
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &SDLRendererBackend::Destroy>(this);

        return true;
    }

    void SDLRendererBackend::Destroy() {
        if (_renderer) {
            SDL_DestroyRenderer(_renderer);
            _renderer = nullptr;
        }
    }

    void SDLRendererBackend::BeginFrame() {}

    void SDLRendererBackend::EndFrame() {}

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

    void SDLRendererBackend::OnImGuiInit() {
        if (!_renderer) {
            return;
        }
        const auto& window = CoreManager::GetMainWindow();
        if (!window) {
            return;
        }
        auto* sdlWindow = dynamic_cast<SDLMainWindow*>(window.Get());
        if (!sdlWindow) {
            return;
        }
        ImGui_ImplSDL3_InitForSDLRenderer(sdlWindow->GetSDLWindow(), _renderer);
        ImGui_ImplSDLRenderer3_Init(_renderer);
    }

    void SDLRendererBackend::OnImGuiShutdown() {
        ImGui_ImplSDLRenderer3_Shutdown();
    }

    void SDLRendererBackend::OnImGuiNewFrame() {
        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplSDLRenderer3_NewFrame();
    }

    void SDLRendererBackend::OnImGuiRender() {
        if (_renderer) {
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), _renderer);
        }
    }

}  // namespace BECore
