#include "ImGuiWidget.h"

#include <BECore/Control/ControlService.h>
#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Time/Time.h>
#include <CoreSDL/SDLEvents.h>
#include <CoreSDL/SDLMainWindow.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Generated/ImGuiWidget.gen.hpp>
#include <Widgets/ImGuiLayoutText.h>
#include <Widgets/ImGuiTheme.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        /// A dock layout of a few dozen windows is some KB; a larger file is not ours and is left to ImGui
        constexpr size_t kMaxIniBytes = size_t{1} << 20;

        /// Settings are keyed by the hash of a window's name, and the Config Editor became `Config Editor###ConfigEditor` when it turned into a panel asset: an
        /// ini of the old build would float it (and keep it floating, ImGui writes the new section back). So the ini is renamed in memory before ImGui reads it.
        void MigrateLegacyIniWindowNames(const Path& iniPath) {
            if (!GetFileInfo(iniPath)) {
                return;
            }
            const std::expected<String, String> text = ReadTextFile(iniPath, kMaxIniBytes);
            if (!text) {
                LOG_WARNING("ImGui: cannot read '{}': {}; the layout is left as it is", iniPath.View(), text.error());
                return;
            }
            const String migrated = MigrateLegacyWindowNames(*text);
            if (migrated == *text) {
                return;
            }
            // NewFrame loads the file itself only while SettingsLoaded is false, and asserts if settings were loaded by hand without the flag
            ImGui::LoadIniSettingsFromMemory(migrated.c_str(), migrated.size());
            ImGui::GetCurrentContext()->SettingsLoaded = true;
        }

    }  // namespace

    float ImGuiFrameSeconds(bool manualClock, double clockDelta, float backendDelta) {
        if (!manualClock) {
            return backendDelta;
        }
        return std::max(static_cast<float>(clockDelta), kMinImGuiFrameSeconds);
    }

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
        _iniFilePath = CoreManager::GetFileSystem().GetRootPath() / "config" / "imgui.ini";
        io.IniFilename = _iniFilePath.CStr();
        MigrateLegacyIniWindowNames(_iniFilePath);
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
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = ImGuiFrameSeconds(CoreManager::GetControlService().IsManualClock(), CoreManager::GetTime().GetDeltaTime(), io.DeltaTime);
        ImGui::NewFrame();
    }

    void ImGuiWidget::OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const {
        if (!_isInitialized) {
            return;
        }

        ImGui_ImplSDL3_ProcessEvent(&event.event);
    }

}  // namespace BECore
