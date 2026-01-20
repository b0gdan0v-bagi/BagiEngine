#include "ImGuiWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <CoreSDL/SDLMainWindow.h>

#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <CoreSDL/SDLEvents.h>
#include <Application/Application.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <Generated/ImGuiWidget.gen.hpp>

namespace BECore {

    bool ImGuiWidget::Initialize(const XmlNode& node) {
        if (_isInitialized) {
            return true;
        }

        SDL_Window* window = GetSDLWindow();
        SDL_Renderer* renderer = GetSDLRenderer();

        if (window == nullptr || renderer == nullptr) {
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        ImGui::StyleColorsDark();
        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        Subscribe<SDLEvents::SDLEventWrapper, &ImGuiWidget::OnSDLEvent>(this);
        Subscribe<RenderEvents::NewFrameEvent, &ImGuiWidget::OnNewFrame>(this);
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &ImGuiWidget::Destroy>(this);

        _isInitialized = true;
        return true;
    }

    void ImGuiWidget::Draw() {
        if (auto* renderer = GetSDLRenderer()) {
            ImGui::Render();
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        }
    }

    void ImGuiWidget::Update() {
        if (!_isInitialized) {
            return;
        }

        if (ImGui::Begin("Debug Widget")) {
            if (ImGui::Button("Quit")) {
                ApplicationEvents::QuitEvent::Emit();
            }
        }
        ImGui::End();
    }

    SDL_Window* ImGuiWidget::GetSDLWindow() {
        const auto& window = CoreManager::GetMainWindow();
        if (!window) {
            return nullptr;
        }

        return dynamic_cast<SDLMainWindow*>(window.Get())->GetSDLWindow();
    }

    SDL_Renderer* ImGuiWidget::GetSDLRenderer() {
        const auto& window = CoreManager::GetMainWindow();
        if (!window) {
            return nullptr;
        }

        return dynamic_cast<SDLMainWindow*>(window.Get())->GetSDLRenderer();
    }

    void ImGuiWidget::Destroy() {
        if (!_isInitialized) {
            return;
        }

        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        _isInitialized = false;
    }

    void ImGuiWidget::OnNewFrame() const {
        if (!_isInitialized) {
            return;
        }

        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiWidget::OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const {
        if (!_isInitialized) {
            return;
        }

        ImGui_ImplSDL3_ProcessEvent(&event.event);
    }

}  // namespace BECore

