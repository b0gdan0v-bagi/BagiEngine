#include "MainToolbar.h"

#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/PanelBars.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr ImGuiWindowFlags kBarWindowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        constexpr StringView kBarAsset = "MainToolbarPanel";

    }  // namespace

    float MainToolbar::GetHeight() {
        return ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
    }

    void MainToolbar::Draw() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, GetImGuiPalette().panelBg);
        const bool visible = ImGui::BeginChild("##MainToolbar", ImVec2(0.0f, GetHeight()), ImGuiChildFlags_AlwaysUseWindowPadding, kBarWindowFlags);
        ImGui::PopStyleColor();
        if (visible) {
            PanelBarsRegistry::Draw(kBarAsset);
        }
        ImGui::EndChild();
    }

}  // namespace BECore
