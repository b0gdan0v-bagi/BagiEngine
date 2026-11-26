#include "PoolStringsSlots.h"

#include <EASTL/algorithm.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PoolStrings/PoolStringsPanelModel.h>
#include <Widgets/PoolStrings/PoolStringsText.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr StringView kBlocksSlotName = "poolStringBlocks";
        constexpr StringView kContentsSlotName = "poolStringContents";
        constexpr StringView kGrowthSlotName = "poolStringGrowth";
        constexpr StringView kZonesSlotName = "poolStringZones";

        constexpr float kPlotHeightEm = 3.0f;
        constexpr float kPlotBarGap = 1.0f;
        /// A frame that added little beside the largest one is still seen
        constexpr float kMinBarHeight = 2.0f;

        constexpr int kContentColumns = 4;
        constexpr size_t kContentRowsShown = 20;
        constexpr size_t kMaxCellTextBytes = 200;
        constexpr size_t kMaxTooltipBytes = 2000;
        constexpr float kTooltipWidthEm = 40.0f;
        constexpr float kLengthWidthEm = 5.0f;
        constexpr float kHashWidthEm = 10.0f;
        constexpr float kSourceWidthEm = 10.0f;

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

        void SortedColumn(const char* label, float widthEm, PoolStringSortKey key) {
            ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed, widthEm * ImGui::GetFontSize(), static_cast<ImGuiID>(+key));
        }

        /// A longer part of the string than its cell shows, its line breaks too
        void TextTooltip(StringView text) {
            if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) || !ImGui::BeginTooltip()) {
                return;
            }
            const StringView shown = PoolStringPrefix(text, kMaxTooltipBytes);
            ImGui::PushTextWrapPos(kTooltipWidthEm * ImGui::GetFontSize());
            ImGui::TextUnformatted(shown.data(), shown.data() + shown.size());
            if (shown.size() < text.size()) {
                ImGui::TextDisabled("%zu bytes in all", text.size());
            }
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        /// The height of a table of text rows that shows @p rows of them below its header
        float TableHeight(size_t rows) {
            const float rowHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
            return rowHeight * static_cast<float>(rows + 1);
        }

    }  // namespace

    void PoolStringsSlots::Register() {
        PanelSlots& slots = PanelSlots::GetInstance();
        _blocksSlot = slots.Register(kBlocksSlotName, [this] { DrawBlocks(); });
        _contentsSlot = slots.Register(kContentsSlotName, [this] { DrawContents(); });
        _growthSlot = slots.Register(kGrowthSlotName, [this] { DrawGrowth(); });
        _zonesSlot = slots.Register(kZonesSlotName, [this] { DrawZones(); });
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

    void PoolStringsSlots::DrawContents() {
        _drawnContentRows = 0;
        if (!ImGui::BeginTable("##poolContents", kContentColumns, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable,
                               ImVec2(0.0f, TableHeight(kContentRowsShown)))) {
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 0.0f, static_cast<ImGuiID>(+PoolStringSortKey::Text));
        SortedColumn("Length", kLengthWidthEm, PoolStringSortKey::Length);
        SortedColumn("Hash", kHashWidthEm, PoolStringSortKey::Hash);
        SortedColumn("Source", kSourceWidthEm, PoolStringSortKey::Source);
        ImGui::TableHeadersRow();
        if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
            _model.SetSort(static_cast<PoolStringSortKey>(specs->Specs[0].ColumnUserID), specs->Specs[0].SortDirection == ImGuiSortDirection_Descending);
        }

        const Vector<PoolStringRow>& strings = _model.GetSnapshot().strings;
        const Vector<uint32_t>& matches = _model.GetMatches();  // after the specs, so a click on a header sorts this frame
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(matches.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const PoolStringRow& row = strings[matches[static_cast<size_t>(i)]];
                const StringView text = row.text.ToStringView();
                ImGui::TableNextRow();
                TextCell(PoolStringLine(text, kMaxCellTextBytes));
                TextTooltip(text);
                ImGui::TableNextColumn();
                ImGui::Text("%zu", text.size());
                ImGui::TableNextColumn();
                {
                    const ScopedMonoFont mono;
                    ImGui::Text("%016llx", static_cast<unsigned long long>(row.text.HashValue()));
                }
                TextCell(_model.SourceOf(row));
                ++_drawnContentRows;
            }
        }
        ImGui::EndTable();
    }

    void PoolStringsSlots::DrawGrowth() {
        _drawnGrowthBars = 0;
        DrawGrowthPlot("##poolGrowthStrings", "Runtime strings per frame", &PoolGrowthFrame::runtimeStrings);
        DrawGrowthPlot("##poolGrowthBytes", "Runtime bytes per frame", &PoolGrowthFrame::runtimeBytes);
    }

    void PoolStringsSlots::DrawGrowthPlot(const char* id, const char* title, uint64_t PoolGrowthFrame::* value) {
        const Span<const PoolGrowthFrame> frames = _model.GetGrowth();
        uint64_t most = 0;
        for (const PoolGrowthFrame& frame : frames) {
            most = eastl::max(most, frame.*value);
        }
        ImGui::Text("%s (most: %llu)", title, static_cast<unsigned long long>(most));

        const ImVec2 size(eastl::max(ImGui::GetContentRegionAvail().x, 1.0f), kPlotHeightEm * ImGui::GetFontSize());
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, size);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float bottom = origin.y + size.y;
        drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, bottom), ImGui::GetColorU32(ImGuiCol_FrameBg));

        // A place for every frame the monitor can keep, the newest at the right, so a bar does not change its width while the history fills
        const float step = size.x / static_cast<float>(PoolStringMonitor::kFrameHistory);
        const size_t firstPlace = PoolStringMonitor::kFrameHistory - frames.size();
        const ImU32 color = ImGui::ColorConvertFloat4ToU32(GetImGuiPalette().warning);
        for (size_t i = 0; i < frames.size(); ++i) {
            const uint64_t amount = frames[i].*value;
            if (amount == 0) {
                continue;
            }
            const float x = origin.x + static_cast<float>(firstPlace + i) * step;
            const float height = eastl::max(static_cast<float>(static_cast<double>(amount) / static_cast<double>(most)) * size.y, kMinBarHeight);
            drawList->AddRectFilled(ImVec2(x, bottom - height), ImVec2(x + eastl::max(step - kPlotBarGap, 1.0f), bottom), color);
            ++_drawnGrowthBars;
        }

        if (!ImGui::IsItemHovered() || step <= 0.0f) {
            return;
        }
        const size_t place = static_cast<size_t>(eastl::max(ImGui::GetMousePos().x - origin.x, 0.0f) / step);
        if (place >= firstPlace && place - firstPlace < frames.size()) {
            const PoolGrowthFrame& frame = frames[place - firstPlace];
            ImGui::SetTooltip("Frame %llu\nRuntime strings: %llu (%llu bytes)\nOther strings: %llu", static_cast<unsigned long long>(frame.frame),
                              static_cast<unsigned long long>(frame.runtimeStrings), static_cast<unsigned long long>(frame.runtimeBytes), static_cast<unsigned long long>(frame.otherStrings));
        }
    }

    void PoolStringsSlots::DrawZones() {
        _drawnRecordRows = 0;
        const Vector<PoolStringRecord>& records = _model.GetRecords();
        for (const PoolPanelZone& zone : _model.GetZones()) {
            // The id is the zone's, so a node stays open while the order and the numbers change
            const bool open = ImGui::TreeNodeEx(static_cast<const void*>(zone.group.zone), ImGuiTreeNodeFlags_SpanAvailWidth, "%s", zone.label.c_str());
            if (!zone.location.empty()) {
                ImGui::SetItemTooltip("%s", zone.location.c_str());
            }
            if (!open) {
                continue;
            }
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(zone.group.count));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const PoolStringRecord& record = records[zone.group.first + static_cast<size_t>(i)];
                    const StringView text = record.text.ToStringView();
                    const StringView line = PoolStringLine(text, kMaxCellTextBytes);
                    ImGui::Text("frame %llu: %.*s", static_cast<unsigned long long>(record.frame), static_cast<int>(line.size()), line.data());
                    TextTooltip(text);
                    ++_drawnRecordRows;
                }
            }
            ImGui::TreePop();
        }
    }

}  // namespace BECore
