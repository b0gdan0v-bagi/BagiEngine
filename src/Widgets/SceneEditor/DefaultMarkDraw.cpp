#include "DefaultMarkDraw.h"

#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/ComponentDefaults.h>
#include <Widgets/SceneEditor/ScopedFloatingWidget.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        constexpr float kBarWidth = 2.0f;
        /// From the bar's left edge to the field
        constexpr float kBarOffset = 4.0f;
        constexpr size_t kLabelSize = 96;

    }  // namespace

    bool DrawDefaultMark(const ComponentFieldDifference& difference, float left, float top, float bottom) {
        ImGui::GetWindowDrawList()->AddRectFilled({left - kBarOffset, top}, {left - kBarOffset + kBarWidth, bottom}, ImGui::GetColorU32(GetImGuiPalette().accent));
        if (!difference.resettable || (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0) {
            return false;
        }

        const ScopedFloatingWidget floating;
        char label[kLabelSize];
        snprintf(label, sizeof(label), "%s##reset_%.*s", ICON_LC_ROTATE_CCW, static_cast<int>(difference.field.size()), difference.field.data());
        const float width = ImGui::GetStyle().IndentSpacing;
        ImGui::SetCursorScreenPos({left - kBarOffset - width, top});
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
        const bool clicked = ImGui::Button(label, {width, ImGui::GetFrameHeight()});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Reset to %.*s", static_cast<int>(difference.defaultText.size()), difference.defaultText.data());
        return clicked;
    }

}  // namespace BECore
