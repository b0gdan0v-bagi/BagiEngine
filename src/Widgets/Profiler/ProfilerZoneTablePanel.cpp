#include "ProfilerZoneTablePanel.h"

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <EASTL/algorithm.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kTableHeight = 240.0f;
        constexpr float kWindowComboWidth = 190.0f;
        constexpr float kFilterWidth = 160.0f;
        constexpr float kNameColumnWidth = 190.0f;
        constexpr float kNumberColumnWidth = 66.0f;
        constexpr float kAllocColumnWidth = 110.0f;
        constexpr float kButtonColumnWidth = 74.0f;
        constexpr int kColumnCount = 10;

        const char* const kWindowLabels = "Selected frame\0"
                                          "Last 60 frames\0"
                                          "All frames in the ring\0";

        void SetupNumberColumn(const char* label, ZoneStatColumn column) {
            ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, kNumberColumnWidth, static_cast<ImGuiID>(+column));
        }

        void NumberCell(double ms) {
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", ms);
        }

    }  // namespace

    void ProfilerZoneTablePanel::RefreshRows(const RingSnapshot& snapshot, uint64_t version, std::optional<uint64_t> selectedFrame) {
        StatsKey key;
        key.version = version;
        key.window = _window;
        key.valid = true;
        if (!snapshot.frames.empty()) {
            const uint64_t front = snapshot.frames.front().index;
            const uint64_t newest = snapshot.frames.back().index;
            switch (_window) {
                case Window::SelectedFrame:
                    key.firstFrame = key.lastFrame = selectedFrame && *selectedFrame >= front && *selectedFrame <= newest ? *selectedFrame : newest;
                    break;
                case Window::RecentFrames:
                    key.firstFrame = newest - eastl::min(newest - front, kRecentFrames - 1);
                    key.lastFrame = newest;
                    break;
                case Window::AllFrames:
                    key.firstFrame = front;
                    key.lastFrame = newest;
                    break;
            }
        }
        if (!(key == _statsKey)) {
            _statsKey = key;
            _stats = snapshot.frames.empty() ? eastl::vector<ZoneStatRow>() : ComputeZoneStats(snapshot, key.firstFrame, key.lastFrame);
            ++_statsVersion;
        }

        RowsKey rowsKey;
        rowsKey.statsVersion = _statsVersion;
        rowsKey.column = _sortColumn;
        rowsKey.ascending = _sortAscending;
        rowsKey.filter = _filter;
        rowsKey.valid = true;
        if (!(rowsKey == _rowsKey)) {
            _rowsKey = rowsKey;
            _rows = FilterZoneStats(_stats, _filter);
            SortZoneStats(_rows, _sortColumn, _sortAscending);
        }
    }

    const ProfileSite* ProfilerZoneTablePanel::Draw(const RingSnapshot& snapshot, uint64_t version, std::optional<uint64_t> selectedFrame) {
        int window = static_cast<int>(_window);
        ImGui::SetNextItemWidth(kWindowComboWidth);
        if (ImGui::Combo("Window", &window, kWindowLabels)) {
            _window = static_cast<Window>(window);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(kFilterWidth);
        ImGui::InputTextWithHint("##zoneFilter", "filter by name or file", _filter, sizeof(_filter));
        RefreshRows(snapshot, version, selectedFrame);

        const ProfileSite* shown = nullptr;
        if (!ImGui::BeginTable("##zoneStats", kColumnCount,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable,
                               ImVec2(0.0f, kTableHeight))) {
            return shown;
        }
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Zone", ImGuiTableColumnFlags_WidthFixed, kNameColumnWidth, static_cast<ImGuiID>(+ZoneStatColumn::Name));
        SetupNumberColumn("Calls", ZoneStatColumn::Calls);
        // The default sort is Total, the heaviest first
        ImGui::TableSetupColumn("Total ms", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, kNumberColumnWidth,
                                static_cast<ImGuiID>(+ZoneStatColumn::Total));
        SetupNumberColumn("Self ms", ZoneStatColumn::Self);
        SetupNumberColumn("Mean ms", ZoneStatColumn::Mean);
        SetupNumberColumn("Max ms", ZoneStatColumn::Max);
        SetupNumberColumn("p95 ms", ZoneStatColumn::P95);
        ImGui::TableSetupColumn("Allocations", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, kAllocColumnWidth, static_cast<ImGuiID>(+ZoneStatColumn::Allocations));
        ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, kNameColumnWidth);
        ImGui::TableSetupColumn("##show", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, kButtonColumnWidth);
        ImGui::TableHeadersRow();
        if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
            _sortColumn = static_cast<ZoneStatColumn>(specs->Specs[0].ColumnUserID);
            _sortAscending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(_rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const ZoneStatRow& row = _rows[static_cast<size_t>(i)];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.site->name);
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(row.calls));
                NumberCell(row.totalMs);
                NumberCell(row.selfMs);
                NumberCell(row.meanMs);
                NumberCell(row.maxMs);
                NumberCell(row.p95Ms);
                ImGui::TableNextColumn();
                if (AllocTracker::kEnabled) {
                    ImGui::Text("%llu (%s)", static_cast<unsigned long long>(row.allocCount), FormatBytes(row.allocBytes).c_str());
                } else {
                    ImGui::TextDisabled("n/a");
                }
                ImGui::TableNextColumn();
                ImGui::Text("%s:%u", row.site->file, row.site->line);
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Timeline")) {
                    shown = row.site;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
        return shown;
    }

}  // namespace BECore
