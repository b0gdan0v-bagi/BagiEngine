#include "TouchFlashDraw.h"

#include <Widgets/Feedback/ControlClientLabel.h>
#include <Widgets/ImGuiTheme.h>

namespace BECore {

    namespace {

        /// Of a flash at its strongest: the text under the fill stays readable
        constexpr float kTouchFillOpacity = 0.4f;
        /// Room for a line or a tooltip of a mark; a longer one is cut
        constexpr size_t kMarkTextSize = 192;

    }  // namespace

    void FillRectWithTouchFlash(const ImVec2& min, const ImVec2& max, float alpha) {
        if (alpha <= 0.0f) {
            return;
        }
        ImVec4 color = GetImGuiPalette().accent;
        color.w *= alpha * kTouchFillOpacity;
        ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(color), ImGui::GetStyle().FrameRounding);
    }

    void FillItemWithTouchFlash(float alpha) {
        FillRectWithTouchFlash(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), alpha);
    }

    void DrawTouchMark(const ControlClient& client, StringView field, StringView prefix) {
        Array<char, kControlClientLabelSize> clientBuffer;
        const StringView name = ControlClientLabel(&client, clientBuffer);
        Array<char, kMarkTextSize> text;
        const FormatResult line = FormatToN(text.data(), text.size(), "{}{}changed: {}", prefix, prefix.empty() ? "" : " ", name);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextUnformatted(text.data(), text.data() + (line.truncated ? text.size() : line.size));
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Field: %.*s", static_cast<int>(field.size()), field.data());
        }
    }

}  // namespace BECore
