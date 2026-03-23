#include "ImGuiWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <CoreSDL/SDLEvents.h>
#include <CoreSDL/SDLMainWindow.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Generated/ImGuiWidget.gen.hpp>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>

namespace BECore {

    bool ImGuiWidget::Initialize(IDeserializer& /*deserializer*/) {
        if (_isInitialized) {
            return true;
        }

        SDL_Window* window = GetSDLWindow();
        if (!window) {
            return false;
        }

        if (!CoreManager::GetRenderer()) {
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        // Store imgui.ini in config/ (project root), so it survives clean builds.
        _iniFilePath = (CoreManager::GetFileSystem().GetRootPath() / "config" / "imgui.ini").string();
        io.IniFilename = _iniFilePath.c_str();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ImGui::StyleColorsDark();

        RenderEvents::ImGuiInitEvent::Emit();

        Subscribe<SDLEvents::SDLEventWrapper, &ImGuiWidget::OnSDLEvent>(this);
        Subscribe<RenderEvents::NewFrameEvent, &ImGuiWidget::OnNewFrame>(this);
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &ImGuiWidget::Destroy>(this);

        _isInitialized = true;
        return true;
    }

    void ImGuiWidget::Draw() {
        if (!_isInitialized) {
            return;
        }

        ImGui::Render();
        RenderEvents::ImGuiRenderEvent::Emit();

        const ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    void ImGuiWidget::Update() {
    }

    SDL_Window* ImGuiWidget::GetSDLWindow() {
        const auto& window = CoreManager::GetMainWindow();
        if (!window) {
            return nullptr;
        }

        auto* sdlWindow = dynamic_cast<SDLMainWindow*>(window.Get());
        if (!sdlWindow) {
            return nullptr;
        }
        return sdlWindow->GetSDLWindow();
    }

    void ImGuiWidget::Destroy() {
        if (!_isInitialized) {
            return;
        }

        RenderEvents::ImGuiShutdownEvent::Emit();

        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        _isInitialized = false;
    }

    void ImGuiWidget::OnNewFrame() const {
        if (!_isInitialized) {
            return;
        }

        RenderEvents::ImGuiNewFrameEvent::Emit();
        ImGui::NewFrame();
    }

    void ImGuiWidget::OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const {
        if (!_isInitialized) {
            return;
        }

        ImGui_ImplSDL3_ProcessEvent(&event.event);
    }

}  // namespace BECore
