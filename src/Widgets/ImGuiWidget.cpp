#include "ImGuiWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <CoreSDL/SDLEvents.h>
#include <CoreSDL/SDLMainWindow.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Generated/ImGuiWidget.gen.hpp>
#include <Widgets/ImGuiTheme.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>

namespace BECore {

    ImGuiWidget::~ImGuiWidget() {
        Destroy();
    }

    bool ImGuiWidget::Initialize(IDeserializer& /*deserializer*/) {
        BE_PROFILE_SCOPE("ImGuiWidget::Initialize");
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

        ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);

        _fonts = LoadImGuiFonts(io, _fontBuffers);
        SetActiveImGuiFonts(_fonts);
        _appliedWindowScale = 0.0f;

        RenderEvents::ImGuiInitEvent::Emit();

        Subscribe<SDLEvents::SDLEventWrapper, &ImGuiWidget::OnSDLEvent>(this);
        Subscribe<RenderEvents::NewFrameEvent, &ImGuiWidget::OnNewFrame>(this);
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &ImGuiWidget::Destroy>(this);

        _isInitialized = true;
        return true;
    }

    void ImGuiWidget::Draw() {
        BE_PROFILE_SCOPE("ImGuiWidget::Draw");
        if (!_isInitialized) {
            return;
        }

        ImGui::Render();
        // ImGui's draws reach the GPU through its own backend, past IRenderer's counters: the frame statistics get them as named counters
        if (const ImDrawData* drawData = ImGui::GetDrawData()) {
            int commands = 0;
            for (const ImDrawList* list : drawData->CmdLists) {
                commands += list->CmdBuffer.Size;
            }
            FrameStatsCollector& frameStats = CoreManager::GetFrameStats();
            frameStats.SetCounter("ImGui draw calls"_intern, static_cast<double>(commands));
            frameStats.SetCounter("ImGui triangles"_intern, static_cast<double>(drawData->TotalIdxCount / 3));
        }
        RenderEvents::ImGuiRenderEvent::Emit();

        const ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    void ImGuiWidget::Update() {}

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

        // The Vulkan backend frees ImGui's descriptor pool and pipeline on ImGuiShutdownEvent: the GPU must be done with them
        if (const auto& renderer = CoreManager::GetRenderer()) {
            renderer->WaitIdle();
        }
        RenderEvents::ImGuiShutdownEvent::Emit();

        ImGui_ImplSDL3_Shutdown();
        SetActiveImGuiFonts({});
        ImGui::DestroyContext();
        _fonts = {};
        _fontBuffers = {};

        _isInitialized = false;
    }

    void ImGuiWidget::ApplyWindowScale() {
        const auto& window = CoreManager::GetMainWindow();
        if (!window) {
            return;
        }
        const float scale = FontScaleFor(window->GetDisplayScale(), window->GetPixelDensity());
        if (scale == _appliedWindowScale) {
            return;
        }
        _appliedWindowScale = scale;
        ImGuiStyle& style = ImGui::GetStyle();
        style.FontScaleDpi = scale;
        ApplyImGuiTheme(style, scale);
    }

    void ImGuiWidget::OnNewFrame() {
        BE_PROFILE_SCOPE("ImGuiWidget::NewFrame");
        if (!_isInitialized) {
            return;
        }

        RenderEvents::ImGuiNewFrameEvent::Emit();
        ApplyWindowScale();
        ImGui::NewFrame();
    }

    void ImGuiWidget::OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const {
        if (!_isInitialized) {
            return;
        }

        ImGui_ImplSDL3_ProcessEvent(&event.event);
    }

}  // namespace BECore
