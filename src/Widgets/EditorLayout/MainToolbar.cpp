#include "MainToolbar.h"

#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr ImGuiWindowFlags kBarWindowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        constexpr size_t kButtonCount = 3;

        constexpr const char* kPlayTooltip = "Play: the editor does not run the game from here yet";
        constexpr const char* kStopTooltip = "Stop: the editor does not run the game from here yet";
        constexpr const char* kPauseTooltip = "Pause or resume: the editor does not run the game from here yet";
        constexpr const char* kStepTooltip = "Step one frame while paused: the editor does not run the game from here yet";

        /// Fills the button with the accent while the scope lives
        class ScopedAccentButton {
        public:
            ScopedAccentButton(bool active, const ImVec4& normal, const ImVec4& hovered, const ImVec4& pressed) : _active(active) {
                if (_active) {
                    ImGui::PushStyleColor(ImGuiCol_Button, normal);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, pressed);
                }
            }
            ~ScopedAccentButton() {
                if (_active) {
                    ImGui::PopStyleColor(3);
                }
            }
            ScopedAccentButton(const ScopedAccentButton&) = delete;
            ScopedAccentButton& operator=(const ScopedAccentButton&) = delete;

        private:
            bool _active;
        };

    }  // namespace

    float MainToolbar::GetHeight() {
        return ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
    }

    void MainToolbar::Draw(EditorPlayModel& model) {
        const ImGuiPalette& palette = GetImGuiPalette();
        const ImGuiStyle& style = ImGui::GetStyle();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.panelBg);
        const bool visible = ImGui::BeginChild("##MainToolbar", ImVec2(0.0f, GetHeight()), ImGuiChildFlags_AlwaysUseWindowPadding, kBarWindowFlags);
        ImGui::PopStyleColor();
        if (visible) {
            const float side = ImGui::GetFrameHeight();
            const float groupWidth = side * static_cast<float>(kButtonCount) + style.ItemSpacing.x * static_cast<float>(kButtonCount - 1);
            const float freeWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + eastl::max(0.0f, (freeWidth - groupWidth) * 0.5f));

            const EditorPlayState state = model.GetState();
            const bool stopped = state == EditorPlayState::Stopped;
            {
                const ScopedAccentButton accent(stopped, palette.accent, palette.accentHover, palette.accentActive);
                if (IconButton(stopped ? ICON_LC_PLAY : ICON_LC_SQUARE, stopped ? kPlayTooltip : kStopTooltip, "play")) {
                    model.PressPlay();
                }
            }
            ImGui::SameLine();
            {
                ImGui::BeginDisabled(!model.CanPause());
                const ScopedAccentButton muted(state == EditorPlayState::Paused, palette.accentMuted, palette.accentHover, palette.accentActive);
                if (IconButton(ICON_LC_PAUSE, kPauseTooltip, "pause")) {
                    model.PressPause();
                }
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            {
                ImGui::BeginDisabled(!model.CanStep());
                if (IconButton(ICON_LC_STEP_FORWARD, kStepTooltip, "step")) {
                    model.PressStep();
                }
                ImGui::EndDisabled();
            }
        }
        ImGui::EndChild();
    }

}  // namespace BECore
