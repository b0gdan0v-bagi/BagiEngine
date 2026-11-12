#include "EditorControls.h"

#include <imgui.h>

namespace BECore {

    namespace {

        /// Enough for an icon, a word and an id
        constexpr size_t kLabelSize = 128;

        void ShowTooltip(const char* tooltip) {
            if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", tooltip);
            }
        }

    }  // namespace

    bool IconButton(const char* icon, const char* tooltip, const char* id) {
        char label[kLabelSize];
        snprintf(label, sizeof(label), "%s##%s", icon, id);
        const float side = ImGui::GetFrameHeight();
        const bool clicked = ImGui::Button(label, ImVec2(side, side));
        ShowTooltip(tooltip);
        return clicked;
    }

    bool IconTextButton(const char* icon, const char* text, const char* tooltip, float width) {
        char label[kLabelSize];
        snprintf(label, sizeof(label), "%s %s", icon, text);
        const bool clicked = ImGui::Button(label, ImVec2(width, 0.0f));
        ShowTooltip(tooltip);
        return clicked;
    }

}  // namespace BECore
