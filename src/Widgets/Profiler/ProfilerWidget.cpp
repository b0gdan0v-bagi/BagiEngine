#include "ProfilerWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerToggle.h>
#include <BECore/Profiler/Ring/FrameRingBackend.h>
#include <CoreSDL/SDLEvents.h>
#include <Generated/ProfilerWidget.gen.hpp>
#include <imgui.h>

namespace BECore {

    namespace {

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

    ProfilerWidget::~ProfilerWidget() {
        // A widget can die in the middle of the run (a mode switch), and the ring must not keep recording for a panel that is gone
        if (_ringRequested) {
            Profiler::SetRingEnabled(false);
        }
    }

    void ProfilerWidget::Draw() {}

    void ProfilerWidget::SyncRing() {
        const bool wanted = _open && BE_PROFILER != 0;
        if (wanted != _ringRequested) {
            Profiler::SetRingEnabled(wanted);
            _ringRequested = wanted;
        }
    }

    void ProfilerWidget::Capture() {
        _history = CoreManager::GetFrameStats().GetHistory(FrameStatsCollector::kFrameHistory);
        if constexpr (BE_PROFILER != 0) {
            _snapshot = Profiler::GetFrameRing().Snapshot();
        }
        if (_selectedFrame && (_snapshot.frames.empty() || *_selectedFrame < _snapshot.frames.front().index)) {
            _selectedFrame.reset();  // the ring has overwritten it
        }
    }

    void ProfilerWidget::Update() {
        SyncRing();
        if (!_open) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Profiler", &_open)) {
            if (!_paused) {
                Capture();
            }
            DrawFrames();
            DrawTimeline();
            DrawMemory();
            DrawZoneAllocs();
            DrawTrace();
        }
        ImGui::End();
        SyncRing();  // the close button of the window
    }

    void ProfilerWidget::DrawFrames() {
        if (_history.empty()) {
            ImGui::TextUnformatted("No frame yet");
            return;
        }
        ImGui::Checkbox("Pause", &_paused);
        ImGui::SameLine();
        ImGui::TextDisabled("(click a bar to open the frame)");
        if (const std::optional<uint64_t> clicked = _framePlot.Draw(_history, _snapshot, _selectedFrame)) {
            _selectedFrame = clicked;
        }
        // The overlay's text: one place says what a frame cost
        for (const eastl::string& line : FormatOverlayLines(_history.back(), AllocTracker::GetTotals(), AllocTracker::kEnabled)) {
            ImGui::TextUnformatted(line.c_str());
        }
    }

    void ProfilerWidget::DrawTimeline() {
        if (!ImGui::CollapsingHeader("Timeline", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        if constexpr (BE_PROFILER == 0) {
            ImGui::TextUnformatted("Zones are recorded with ENGINE_PROFILER=ON (preset macos-profile-editor)");
            return;
        }
        _timeline.Draw(_snapshot, _selectedFrame);
    }

    void ProfilerWidget::DrawMemory() {
        if (!ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        if (!AllocTracker::kEnabled) {
            ImGui::TextUnformatted("Allocations are counted with ENGINE_PROFILER_ALLOC=ON (preset macos-profile-editor)");
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
            ImGui::TextUnformatted("Zones are recorded with ENGINE_PROFILER=ON (preset macos-profile-editor)");
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
