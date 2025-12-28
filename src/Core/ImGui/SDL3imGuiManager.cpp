#include "SDL3imGuiManager.h"

#include "Application/Application.h"
#include "Core/Events/EventManager.h"
#include "Core/Events/Events.h"
#include "Core/MainWindow/SDLMainWindow.h"
#include "Core/MainWindow/SDLRendererHolder.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

namespace Core {

    bool SDL3imGuiManager::Initialize() {
        if (_isInitialized) {
            return true;
        }

        SDL_Window* window = GetSDLWindow();
        SDL_Renderer* renderer = GetSDLRenderer();

        if (window == nullptr || renderer == nullptr) {
            return false;
        }

        // Настройка контекста ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Включить навигацию с клавиатуры
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Включить навигацию с геймпада

        // Настройка стиля
        ImGui::StyleColorsDark();

        // Инициализация платформенных бэкендов
        ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);

        Application::GetInstance().GetEventManager().Subscribe<SDLEventWrapper, &SDL3imGuiManager::OnSDLEvent>(this);

        _isInitialized = true;
        return true;
    }

    void SDL3imGuiManager::Destroy() {
        if (!_isInitialized) {
            return;
        }

        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        _isInitialized = false;
    }

    void SDL3imGuiManager::NewFrame() {
        if (!_isInitialized) {
            return;
        }

        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();
    }

    void SDL3imGuiManager::OnSDLEvent(const SDLEventWrapper& event) const {
        if (!_isInitialized) {
            return;
        }

        ImGui_ImplSDL3_ProcessEvent(&event.event);
    }

    void SDL3imGuiManager::Render() {
        if (!_isInitialized) {
            return;
        }

        if (auto* renderer = GetSDLRenderer()) {
            ImGui::Render();
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        }
    }

    SDL_Window* SDL3imGuiManager::GetSDLWindow() {

        const auto& window = Application::GetInstance().GetMainWindow();
        if (!window) {
            return nullptr;
        }

        return dynamic_cast<SDLMainWindow*>(window.Get())->GetSDLWindow();
    }

    SDL_Renderer* SDL3imGuiManager::GetSDLRenderer() {
        const auto& window = Application::GetInstance().GetMainWindow();
        if (!window) {
            return nullptr;
        }
        if (const auto& rendererHolder = window->GetRenderer()) {
            return dynamic_cast<SDLRendererHolder*>(rendererHolder.Get())->GetRenderer();
        }
        return nullptr;
    }

}  // namespace Core