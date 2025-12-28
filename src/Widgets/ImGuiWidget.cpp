#include "ImGuiWidget.h"

#include "Core/MainWindow/SDLMainWindow.h"
#include "Core/MainWindow/SDLRendererHolder.h"

#include <Core/Events/Events.h>
#include <Application/Application.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

namespace Core {

    bool ImGuiWidget::Initialize() {
        if (_isInitialized) {
            return true;
        }

        SDL_Window* window = GetSDLWindow();
        SDL_Renderer* renderer = GetSDLRenderer();

        if (window == nullptr || renderer == nullptr) {
            return false;
        }

        // ????????? ????????? ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // ???????? ????????? ? ??????????
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // ???????? ????????? ? ????????

        // ????????? ?????
        ImGui::StyleColorsDark();

        // ????????????? ????????????? ????????
        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        Application::GetInstance().GetEventManager().Subscribe<SDLEventWrapper, &ImGuiWidget::OnSDLEvent>(this);
        Application::GetInstance().GetEventManager().Subscribe<NewFrameEvent, &ImGuiWidget::OnNewFrame>(this);
        Application::GetInstance().GetEventManager().Subscribe<ApplicationCleanUpEvent, &ImGuiWidget::Destroy>(this);

        _isInitialized = true;
        return true;
    }

    void ImGuiWidget::Draw() {
        if (!_isInitialized) {
            return;
        }

        if (ImGui::Begin("Debug Widget")) {
            if (ImGui::Button("Quit")) {
                Application::GetInstance().GetEventManager().Emit(QuitEvent{});
            }
        }
        ImGui::End();

        if (auto* renderer = GetSDLRenderer()) {
            ImGui::Render();
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        }
    }

    SDL_Window* ImGuiWidget::GetSDLWindow() {
        const auto& window = Application::GetInstance().GetMainWindow();
        if (!window) {
            return nullptr;
        }

        return dynamic_cast<SDLMainWindow*>(window.Get())->GetSDLWindow();
    }

    SDL_Renderer* ImGuiWidget::GetSDLRenderer() {
        const auto& window = Application::GetInstance().GetMainWindow();
        if (!window) {
            return nullptr;
        }
        if (const auto& rendererHolder = window->GetRenderer()) {
            return dynamic_cast<SDLRendererHolder*>(rendererHolder.Get())->GetRenderer();
        }
        return nullptr;
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

    void ImGuiWidget::OnSDLEvent(const SDLEventWrapper& event) const {
        if (!_isInitialized) {
            return;
        }

        ImGui_ImplSDL3_ProcessEvent(&event.event);
    }

}  // namespace Core

