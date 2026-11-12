#include "EditorControls.h"

#include <imgui.h>

namespace BECore {

    namespace {

        /// Enough for an icon, a word and an id
        constexpr size_t kLabelSize = 128;

        /// The icon of an empty state, in ImGui units before the global scales
        constexpr float kEmptyStateIconSize = 32.0f;
        /// The text of an empty state wraps at this many ems (font sizes), or at the width that is free when that is less
        constexpr float kEmptyStateTextWidthInEm = 22.0f;

    }  // namespace

    void ItemTooltip(const char* tooltip) {
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tooltip);
        }
    }

    bool IconButton(const char* icon, const char* tooltip, const char* id) {
        char label[kLabelSize];
        snprintf(label, sizeof(label), "%s##%s", icon, id);
        const float side = ImGui::GetFrameHeight();
        const bool clicked = ImGui::Button(label, ImVec2(side, side));
        ItemTooltip(tooltip);
        return clicked;
    }

    bool IconTextButton(const char* icon, const char* text, const char* tooltip, float width) {
        char label[kLabelSize];
        snprintf(label, sizeof(label), "%s %s", icon, text);
        const bool clicked = ImGui::Button(label, ImVec2(width, 0.0f));
        ItemTooltip(tooltip);
        return clicked;
    }

    void EmptyState(const char* icon, const char* text) {
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float wrapWidth = eastl::min(availableWidth, ImGui::GetFontSize() * kEmptyStateTextWidthInEm);
        const ImVec2 textSize = ImGui::CalcTextSize(text, nullptr, false, wrapWidth);
        const float left = ImGui::GetCursorPosX();

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
        ImGui::PushFont(nullptr, kEmptyStateIconSize);
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        ImGui::SetCursorPosX(left + (availableWidth - iconSize.x) * 0.5f);
        ImGui::TextUnformatted(icon);
        ImGui::PopFont();

        // The wrap position is the widest line plus a pixel, so the lines break where CalcTextSize broke them
        const float textLeft = left + (availableWidth - textSize.x) * 0.5f;
        ImGui::SetCursorPosX(textLeft);
        ImGui::PushTextWrapPos(textLeft + textSize.x + 1.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

}  // namespace BECore
