#include "ProfilerWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerToggle.h>
#include <CoreSDL/SDLEvents.h>
#include <Generated/ProfilerWidget.gen.hpp>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr size_t kPlotFrames = FrameStatsCollector::kFrameHistory;
        constexpr float kPlotHeight = 80.0f;
        constexpr float kPlotMaxMs = 1000.0f / 30.0f;
        constexpr int kZoneRows = 50;

    }  // namespace

    bool ProfilerWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorCommandEvent, &ProfilerWidget::OnCommand>(this);
        Subscribe<SDLEvents::SDLEventWrapper, &ProfilerWidget::OnSDLEvent>(this);
        return true;
    }

    void ProfilerWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewProfiler) {
            _open = !_open;
        }
    }

    void ProfilerWidget::OnSDLEvent(const SDLEvents::SDLEventWrapper& event) {
        const SDL_KeyboardEvent& key = event.event.key;
        if (event.event.type == SDL_EVENT_KEY_DOWN && IsProfilerToggle(key.key, key.mod, key.repeat)) {
            _open = !_open;
        }
    }

    void ProfilerWidget::Draw() {}

    void ProfilerWidget::Update() {
        if (!_open) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Profiler", &_open)) {
            ImGui::End();
            return;
        }
        DrawFrames();
        DrawMemory();
        DrawZoneAllocs();
        DrawTrace();
        ImGui::End();
    }

    void ProfilerWidget::DrawFrames() {
        const FrameStatsCollector& collector = CoreManager::GetFrameStats();
        const FrameStats* last = collector.GetLast();
        if (last == nullptr) {
            ImGui::TextUnformatted("No frame yet");
            return;
        }
        const eastl::vector<FrameStats> history = collector.GetHistory(kPlotFrames);
        eastl::vector<float> frameMs;
        frameMs.reserve(history.size());
        for (const FrameStats& frame : history) {
            frameMs.push_back(static_cast<float>(frame.frameMs));
        }
        ImGui::PlotLines("##frameMs", frameMs.data(), static_cast<int>(frameMs.size()), 0, "frame ms", 0.0f, kPlotMaxMs, ImVec2(-1.0f, kPlotHeight));
        // The overlay's text: one place says what a frame cost
        for (const eastl::string& line : FormatOverlayLines(*last, AllocTracker::GetTotals(), AllocTracker::kEnabled)) {
            ImGui::TextUnformatted(line.c_str());
        }
    }

    void ProfilerWidget::DrawMemory() {
        if (!ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        if (!AllocTracker::kEnabled) {
            ImGui::TextUnformatted("Allocations are counted with ENGINE_PROFILER_ALLOC=ON (preset macos-profile)");
            return;
        }
        const AllocTotals totals = AllocTracker::GetTotals();
        ImGui::Text("Allocations %llu (%s), frees %llu", static_cast<unsigned long long>(totals.count), FormatBytes(totals.bytes).c_str(), static_cast<unsigned long long>(totals.frees));
        ImGui::Text("Live %s, peak %s, largest %s", FormatBytes(totals.liveBytes).c_str(), FormatBytes(totals.peakLiveBytes).c_str(), FormatBytes(totals.largest).c_str());
        ImGui::Text("Allocations in no-alloc scopes: %llu", static_cast<unsigned long long>(totals.noAllocViolations));
    }

    void ProfilerWidget::DrawZoneAllocs() {
        if (!AllocTracker::kEnabled || !ImGui::CollapsingHeader("Allocations per zone")) {
            return;
        }
        if (ImGui::Button("Reset")) {
            AllocTracker::ResetZoneStats();
        }
        constexpr int kColumns = 5;
        if (!ImGui::BeginTable("##zoneAllocs", kColumns, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 200.0f))) {
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

    void ProfilerWidget::DrawTrace() {
        if (!ImGui::CollapsingHeader("Chrome trace")) {
            return;
        }
        if constexpr (BE_PROFILER == 0) {
            ImGui::TextUnformatted("Zones are recorded with ENGINE_PROFILER=ON (preset macos-profile)");
            return;
        }
        ChromeTraceBackend& recorder = Profiler::GetChromeTrace();
        const bool recording = Profiler::GetPrimaryBackend() == &recorder;
        if (!recording && ImGui::Button("Record")) {
            recorder.MarkTraceStart();
            Profiler::SetBackend(&recorder);
        }
        if (recording && ImGui::Button("Restart")) {
            recorder.MarkTraceStart();
        }
        ImGui::InputText("File", _tracePath, sizeof(_tracePath));
        if (recording && ImGui::Button("Write")) {
            const auto written = recorder.WriteFile(_tracePath);
            _traceMessage = written ? Format("Written {} (dropped events: {})", _tracePath, recorder.GetDroppedCount()) : written.error();
        }
        if (!_traceMessage.empty()) {
            ImGui::TextWrapped("%s", _traceMessage.c_str());
        }
    }

}  // namespace BECore
