#include "ExportSlots.h"

#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Export/ExportPanelModel.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr StringView kOutputSlotName = "exportOutput";
        /// Closer than this to the end of the list counts as at the bottom
        constexpr float kBottomSlack = 1.0f;

        ImVec4 ColorOf(ExportLineKind kind) {
            const ImGuiPalette& palette = GetImGuiPalette();
            switch (kind) {
                case ExportLineKind::Warning:
                    return palette.warning;
                case ExportLineKind::Error:
                    return palette.error;
                default:
                    return palette.text;
            }
        }

    }  // namespace

    void ExportSlots::Register() {
        _outputSlot = PanelSlots::GetInstance().Register(kOutputSlotName, [this] { DrawOutput(); });
    }

    void ExportSlots::DrawOutput() {
        // The list is drawn whenever the panel is, so it is the one to say where the pointer is
        // A held control does not hide the panel from the pointer: a click must not read as leaving it and coming back
        _model.NotePointerOver(ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
        const ExportLog& log = _model.GetLog();
        const bool errorsOnly = _model.GetView().errorsOnly;
        if (errorsOnly && _errorRevision != log.GetRevision()) {
            CollectErrors();
        }
        const size_t count = errorsOnly ? _errorLines.size() : log.GetLineCount();

        ImGui::BeginChild("##exportOutput", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        if (count == 0) {
            EmptyState(ICON_LC_SCROLL_TEXT, log.GetLineCount() == 0 ? "The output of the export shows here" : "No error in the output");
            ImGui::EndChild();
            return;
        }

        // The state of the last frame: the list grows below the view, so this frame's lines are not in the scroll range yet
        const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - kBottomSlack;
        if (log.GetDroppedCount() != 0) {
            ImGui::TextDisabled("%zu earlier lines are not kept", log.GetDroppedCount());
        }
        {
            const ScopedMonoFont mono;
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(count));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const size_t shown = static_cast<size_t>(i);
                    DrawLine(errorsOnly ? _errorLines[shown] : shown);
                }
            }
            clipper.End();
        }
        if (atBottom) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    void ExportSlots::CollectErrors() {
        const ExportLog& log = _model.GetLog();
        _errorRevision = log.GetRevision();
        _errorLines.clear();
        for (size_t i = 0; i < log.GetLineCount(); ++i) {
            if (log.GetLine(i).kind == ExportLineKind::Error) {
                _errorLines.push_back(static_cast<uint32_t>(i));
            }
        }
    }

    void ExportSlots::DrawLine(size_t index) {
        const ExportLog& log = _model.GetLog();
        const ExportLine& line = log.GetLine(index);
        // The number of the line in the whole output, so its id stays while older lines are dropped
        ImGui::PushID(static_cast<int>(log.GetDroppedCount() + index));
        // A selectable under the text gives the row a hover highlight and a place for the menu
        const ImVec2 rowStart = ImGui::GetCursorPos();
        ImGui::Selectable("##line", false, ImGuiSelectableFlags_AllowOverlap);
        if (ImGui::BeginPopupContextItem("##lineMenu")) {
            if (ImGui::MenuItem(ICON_LC_COPY " Copy line")) {
                ImGui::SetClipboardText(line.text.c_str());
            }
            ImGui::EndPopup();
        }
        ImGui::SetCursorPos(rowStart);
        // Never a format string: the line is what a tool wrote
        ImGui::PushStyleColor(ImGuiCol_Text, ColorOf(line.kind));
        ImGui::TextUnformatted(line.text.c_str(), line.text.c_str() + line.text.size());
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

}  // namespace BECore
