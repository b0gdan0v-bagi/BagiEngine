#include "ImGuiManager.h"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

ImGuiManager::~ImGuiManager() {
    Shutdown();
}

bool ImGuiManager::Initialize(SDL_Window* window, SDL_Renderer* renderer) {
    if (_isInitialized) {
        return true;
    }

    if (window == nullptr || renderer == nullptr) {
        return false;
    }

    _window = window;
    _renderer = renderer;

    // Настройка контекста ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Включить навигацию с клавиатуры
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Включить навигацию с геймпада

    // Настройка стиля
    ImGui::StyleColorsDark();

    // Инициализация платформенных бэкендов
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    _isInitialized = true;
    return true;
}

void ImGuiManager::Shutdown() {
    if (!_isInitialized) {
        return;
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    _isInitialized = false;
    _window = nullptr;
    _renderer = nullptr;
}

void ImGuiManager::NewFrame() {
    if (!_isInitialized) {
        return;
    }

    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::ProcessEvent(const SDL_Event* event) {
    if (!_isInitialized) {
        return;
    }

    ImGui_ImplSDL3_ProcessEvent(event);
}

void ImGuiManager::Render() {
    if (!_isInitialized) {
        return;
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), _renderer);
}

