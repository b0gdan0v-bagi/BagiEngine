#include "DefaultMarkDraw.h"

#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/ComponentDefaults.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        constexpr float kBarWidth = 2.0f;
        /// From the bar's left edge to the field
        constexpr float kBarOffset = 4.0f;
        constexpr size_t kLabelSize = 96;

        /// While it lives a widget may be placed anywhere in the window: the layout is afterwards as it was, so the widget takes no room. A plain
        /// `SetCursorScreenPos` back is not enough: after the last field of a window ImGui reports a cursor that no item followed
        class ScopedFloatingWidget {
        public:
            ScopedFloatingWidget() : _window(ImGui::GetCurrentWindow()), _layout(_window->DC) {}
            ~ScopedFloatingWidget() {
                ImGuiWindowTempData& now = _window->DC;
                now.CursorPos = _layout.CursorPos;
                now.CursorPosPrevLine = _layout.CursorPosPrevLine;
                now.CursorMaxPos = _layout.CursorMaxPos;
                now.IdealMaxPos = _layout.IdealMaxPos;
                now.CurrLineSize = _layout.CurrLineSize;
                now.PrevLineSize = _layout.PrevLineSize;
                now.CurrLineTextBaseOffset = _layout.CurrLineTextBaseOffset;
                now.PrevLineTextBaseOffset = _layout.PrevLineTextBaseOffset;
                now.IsSameLine = _layout.IsSameLine;
                now.IsSetPos = _layout.IsSetPos;
            }
            ScopedFloatingWidget(const ScopedFloatingWidget&) = delete;
            ScopedFloatingWidget& operator=(const ScopedFloatingWidget&) = delete;

        private:
            struct Layout {
                explicit Layout(const ImGuiWindowTempData& data)
                    : CursorPos(data.CursorPos), CursorPosPrevLine(data.CursorPosPrevLine), CursorMaxPos(data.CursorMaxPos), IdealMaxPos(data.IdealMaxPos), CurrLineSize(data.CurrLineSize),
                      PrevLineSize(data.PrevLineSize), CurrLineTextBaseOffset(data.CurrLineTextBaseOffset), PrevLineTextBaseOffset(data.PrevLineTextBaseOffset), IsSameLine(data.IsSameLine),
                      IsSetPos(data.IsSetPos) {}

                ImVec2 CursorPos;
                ImVec2 CursorPosPrevLine;
                ImVec2 CursorMaxPos;
                ImVec2 IdealMaxPos;
                ImVec2 CurrLineSize;
                ImVec2 PrevLineSize;
                float CurrLineTextBaseOffset;
                float PrevLineTextBaseOffset;
                bool IsSameLine;
                bool IsSetPos;
            };

            ImGuiWindow* _window;
            Layout _layout;
        };

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
