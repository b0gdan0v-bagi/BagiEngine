#include "PoolStringsSlots.h"

#include <EASTL/algorithm.h>
#include <Widgets/PoolStrings/PoolStringsPanelModel.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr StringView kBlocksSlotName = "poolStringBlocks";

        constexpr int kBlockColumns = 6;
        /// Rows the blocks table shows before it scrolls
        constexpr size_t kBlockRowsShown = 8;
        constexpr float kKindWidthEm = 5.0f;
        constexpr float kFileWidthEm = 12.0f;
        constexpr float kUsageWidthEm = 10.0f;
        constexpr float kDeadWidthEm = 6.0f;

        void TextCell(StringView text) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
        }

        void FixedColumn(const char* label, float widthEm) {
            ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed, widthEm * ImGui::GetFontSize());
        }

        /// The height of a table of text rows that shows @p rows of them below its header
        float TableHeight(size_t rows) {
            const float rowHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
            return rowHeight * static_cast<float>(rows + 1);
        }

    }  // namespace

    void PoolStringsSlots::Register() {
        _blocksSlot = PanelSlots::GetInstance().Register(kBlocksSlotName, [this] { DrawBlocks(); });
    }

    void PoolStringsSlots::DrawBlocks() {
        const Vector<PoolPanelBlock>& blocks = _model.GetBlocks();
        _drawnBlockRows = 0;
        const float height = TableHeight(eastl::min(blocks.size(), kBlockRowsShown));
        if (!ImGui::BeginTable("##poolBlocks", kBlockColumns, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0.0f, height))) {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        FixedColumn("Kind", kKindWidthEm);
        FixedColumn("File", kFileWidthEm);
        ImGui::TableSetupColumn("Fill", ImGuiTableColumnFlags_WidthStretch);
        FixedColumn("Used / capacity", kUsageWidthEm);
        FixedColumn("Dead strings", kDeadWidthEm);
        FixedColumn("Dead bytes", kDeadWidthEm);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(blocks.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const PoolPanelBlock& block = blocks[static_cast<size_t>(i)];
                ImGui::TableNextRow();
                TextCell(block.kind);
                TextCell(block.file);
                if (!block.source.empty()) {
                    ImGui::SetItemTooltip("%s", block.source.c_str());
                }
                ImGui::TableNextColumn();
                ImGui::ProgressBar(block.fill, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight()));
                TextCell(block.usage);
                TextCell(block.deadStrings);
                TextCell(block.deadBytes);
                ++_drawnBlockRows;
            }
        }
        ImGui::EndTable();
    }

}  // namespace BECore
