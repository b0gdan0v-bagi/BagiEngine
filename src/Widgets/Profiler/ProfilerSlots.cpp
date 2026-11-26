#include "ProfilerSlots.h"

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <Widgets/Profiler/ProfilerPanelModel.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr StringView kFramesSlotName = "profilerFrames";
        constexpr StringView kTimelineSlotName = "profilerTimeline";
        constexpr StringView kZonesSlotName = "profilerZones";
        constexpr StringView kSpikesSlotName = "profilerSpikes";
        constexpr StringView kMemorySlotName = "profilerMemory";
        constexpr StringView kZoneAllocsSlotName = "profilerZoneAllocs";

        constexpr int kZoneRows = 50;
        constexpr int kZoneAllocColumns = 5;
        constexpr float kZoneAllocsHeight = 200.0f;

    }  // namespace

    void ProfilerSlots::Register() {
        PanelSlots& slots = PanelSlots::GetInstance();
        _framesSlot = slots.Register(kFramesSlotName, [this] { DrawFrames(); });
        _timelineSlot = slots.Register(kTimelineSlotName, [this] { DrawTimeline(); });
        _zonesSlot = slots.Register(kZonesSlotName, [this] { DrawZones(); });
        _spikesSlot = slots.Register(kSpikesSlotName, [this] { DrawSpikes(); });
        _memorySlot = slots.Register(kMemorySlotName, [this] { DrawMemory(); });
        _zoneAllocsSlot = slots.Register(kZoneAllocsSlotName, [this] { DrawZoneAllocs(); });
    }

    void ProfilerSlots::DrawFrames() {
        const Vector<FrameStats>& history = _model.GetHistory();
        if (const Optional<uint64_t> clicked = _framePlot.DrawBars(history, _model.GetSnapshot(), _model.GetSelectedFrame(), PlotSettingsOf(_model.GetView()))) {
            _model.SelectFrame(*clicked);
        }
        if (history.empty()) {
            return;
        }
        // The overlay's text: one place says what a frame cost
        FormatOverlayLinesTo(history.back(), AllocTracker::GetTotals(), AllocTracker::kEnabled, [](StringView line) { ImGui::TextUnformatted(line.data(), line.data() + line.size()); });
    }

    void ProfilerSlots::DrawTimeline() {
        // What the model changed since the last draw: a spike opened or closed (the view starts again), a zone to mark
        const ProfilerTimelineRequest request = _model.TakeTimelineRequest();
        if (request.reset) {
            _timeline.Reset();
        }
        if (request.mark) {
            _timeline.ShowZone(*request.mark);
        }
        if (_model.GetOpenedSpike()) {
            _timeline.Draw(_model.GetSpikeSnapshot(), std::nullopt);
        } else {
            _timeline.Draw(_model.GetSnapshot(), _model.GetSelectedFrame());
        }
    }

    void ProfilerSlots::DrawZones() {
        const ProfilerPanelView& view = _model.GetView();
        if (const ProfileSite* site = _zoneTable.DrawTable(_model.GetSnapshot(), _model.GetSnapshotVersion(), _model.GetSelectedFrame(), view.zoneWindow, view.zoneFilter)) {
            _model.ShowWorstCall(*site);
        }
    }

    void ProfilerSlots::DrawSpikes() {
        if (const Optional<size_t> opened = _spikePanel.DrawList(_model.GetSpikes(), _model.GetOpenedSpike())) {
            _model.OpenSpike(*opened);
        }
    }

    void ProfilerSlots::DrawMemory() {
        const AllocTotals totals = AllocTracker::GetTotals();
        ImGui::Text("Allocations %llu (%s), frees %llu", static_cast<unsigned long long>(totals.count), FormatBytes(totals.bytes).c_str(), static_cast<unsigned long long>(totals.frees));
        ImGui::Text("Live %s, peak %s, largest %s", FormatBytes(totals.liveBytes).c_str(), FormatBytes(totals.peakLiveBytes).c_str(), FormatBytes(totals.largest).c_str());
        ImGui::Text("Allocations in no-alloc scopes: %llu", static_cast<unsigned long long>(totals.noAllocViolations));
    }

    void ProfilerSlots::DrawZoneAllocs() {
        if (!ImGui::BeginTable("##zoneAllocs", kZoneAllocColumns, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0.0f, kZoneAllocsHeight))) {
            return;
        }
        ImGui::TableSetupColumn("Zone");
        ImGui::TableSetupColumn("Calls");
        ImGui::TableSetupColumn("Allocations");
        ImGui::TableSetupColumn("Bytes");
        ImGui::TableSetupColumn("Where");
        ImGui::TableHeadersRow();
        int rows = 0;
        for (const ZoneAllocStats& stats : AllocTracker::GetZoneStats()) {
            if (rows++ == kZoneRows) {
                break;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(stats.site->name);
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(stats.calls));
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(stats.count));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatBytes(stats.bytes).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s:%u", stats.site->file, stats.site->line);
        }
        ImGui::EndTable();
    }

}  // namespace BECore
