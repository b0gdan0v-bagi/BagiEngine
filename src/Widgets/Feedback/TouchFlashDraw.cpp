#include "TouchFlashDraw.h"

#include <Widgets/ImGuiTheme.h>

namespace BECore {

    namespace {

        /// Of a flash at its strongest: the text under the fill stays readable
        constexpr float kTouchFillOpacity = 0.4f;

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

}  // namespace BECore
