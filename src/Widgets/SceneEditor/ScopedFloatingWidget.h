#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

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

}  // namespace BECore
