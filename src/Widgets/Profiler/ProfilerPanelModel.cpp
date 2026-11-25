#include "ProfilerPanelModel.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerToggle.h>
#include <BECore/Profiler/Ring/FrozenTrace.h>
#include <BECore/Profiler/Ring/TimelineView.h>
#include <CoreSDL/SDLEvents.h>
#include <EASTL/algorithm.h>
#include <Generated/ProfilerPanelModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>

namespace BECore {

    namespace {

        constexpr StringView kRecord = "record";
        constexpr StringView kRestart = "restart";
        constexpr StringView kWrite = "write";
        constexpr StringView kWriteFrames = "writeFrames";

        constexpr int32_t kBudgetChoices = 3;
        constexpr int32_t kBarModes = 2;
        constexpr int32_t kZoneWindows = 3;
        constexpr int32_t kSpikeModes = 2;

        /// The longest prefix of @p text within @p limit bytes that does not end inside a UTF-8 sequence
        StringView CutAtBoundary(StringView text, size_t limit) {
            if (text.size() <= limit) {
                return text;
            }
            constexpr unsigned char kContinuationMask = 0xC0;
            constexpr unsigned char kContinuation = 0x80;
            size_t end = limit;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & kContinuationMask) == kContinuation) {
                --end;
            }
            return text.substr(0, end);
        }

        /// Fills @p labels from @p texts
        template <size_t N>
        void FillLabels(Vector<String>& labels, const StringView (&texts)[N]) {
            labels.clear();
            for (const StringView text : texts) {
                labels.push_back(String(text));
            }
        }

        /// A number a control wrote may be anything (a typed value, a NaN): the range wins
        float ClampFloat(float value, float low, float high) {
            if (!(value >= low)) {
                return low;
            }
            return value > high ? high : value;
        }

        /// A combo index that falls outside its list goes back to the first entry
        int32_t ClampIndex(int32_t index, int32_t count) {
            return index < 0 || index >= count ? 0 : index;
        }

    }  // namespace

    ProfilerPanelModel::ProfilerPanelModel() {
        constexpr StringView kBudgetTexts[] = {"16.6 ms (60 fps)", "33.3 ms (30 fps)", "Custom"};
        constexpr StringView kBarModeTexts[] = {"Frame time", "Stage split"};
        constexpr StringView kZoneWindowTexts[] = {"Selected frame", "Last 60 frames", "All frames in the ring"};
        constexpr StringView kSpikeModeTexts[] = {"Over a time (ms)", "Over the median (times)"};
        FillLabels(_view.budgetLabels, kBudgetTexts);
        FillLabels(_view.barModeLabels, kBarModeTexts);
        FillLabels(_view.zoneWindowLabels, kZoneWindowTexts);
        FillLabels(_view.spikeModeLabels, kSpikeModeTexts);
        _view.zonesRecorded = BE_PROFILER != 0;
        _view.allocTracked = AllocTracker::kEnabled;
    }

    ProfilerPanelModel::~ProfilerPanelModel() {
        // A widget can die in the middle of the run (a mode switch), and the ring must not keep recording for a panel that is gone
        if (_ringRequested) {
            Profiler::SetRingEnabled(false);
        }
    }

    void ProfilerPanelModel::Initialize() {
        Subscribe<SDLEvents::SDLEventWrapper, &ProfilerPanelModel::OnSDLEvent>(this);
    }

    TypedRef ProfilerPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void ProfilerPanelModel::SetFrameStats(const FrameStatsCollector* stats) {
        _stats = stats;
    }

    void ProfilerPanelModel::SetFrameRing(const FrameRingBackend* ring) {
        _ring = ring;
    }

    void ProfilerPanelModel::SetPausesAllocCounting(bool pauses) {
        _pausesAllocCounting = pauses;
        if (!pauses) {
            _allocPause.reset();
        }
    }

    const FrameStatsCollector& ProfilerPanelModel::GetStats() const {
        return _stats != nullptr ? *_stats : CoreManager::GetFrameStats();
    }

    const FrameRingBackend* ProfilerPanelModel::GetRing() const {
        if (_ring != nullptr) {
            return _ring;
        }
        if constexpr (BE_PROFILER != 0) {
            return &Profiler::GetFrameRing();
        }
        return nullptr;
    }

    void ProfilerPanelModel::OnSDLEvent(const SDLEvents::SDLEventWrapper& event) {
        const SDL_KeyboardEvent& key = event.event.key;
        if (event.event.type == SDL_EVENT_KEY_DOWN && IsProfilerToggle(key.key, key.mod, key.repeat)) {
            EditorCommandEvent::Emit(EditorCommandIds::kViewProfiler);
        }
    }

    void ProfilerPanelModel::OnOpenChanged(bool open) {
        _open = open;
        SyncRing();
        if (!open) {
            _allocPause.reset();  // a frame that never reached its EndUpdate must not leave the counts paused
        }
    }

    void ProfilerPanelModel::SyncRing() {
        const bool wanted = _open && BE_PROFILER != 0;
        if (wanted != _ringRequested) {
            Profiler::SetRingEnabled(wanted);
            _ringRequested = wanted;
        }
    }

    void ProfilerPanelModel::Update() {
        if (_pausesAllocCounting && !_allocPause) {
            // The panel (history, lanes, zone statistics, ImGui's own memory) is out of the counts; other panels stay counted
            _allocPause.emplace();
        }
        ClampControls();
        SyncSpikeSettings();
        Capture();
        RefreshView();
    }

    void ProfilerPanelModel::EndUpdate() {
        _allocPause.reset();
    }

    void ProfilerPanelModel::ClampControls() {
        _view.budget = ClampIndex(_view.budget, kBudgetChoices);
        _view.barMode = ClampIndex(_view.barMode, kBarModes);
        _view.zoneWindow = ClampIndex(_view.zoneWindow, kZoneWindows);
        _view.spikeMode = ClampIndex(_view.spikeMode, kSpikeModes);
        _view.customBudgetMs = ClampFloat(_view.customBudgetMs, ProfilerPanelLimits::kMinCustomBudgetMs, ProfilerPanelLimits::kMaxCustomBudgetMs);
        _view.spikeThresholdMs = ClampFloat(_view.spikeThresholdMs, ProfilerPanelLimits::kMinThresholdMs, ProfilerPanelLimits::kMaxThresholdMs);
        _view.spikeFactor = ClampFloat(_view.spikeFactor, ProfilerPanelLimits::kMinFactor, ProfilerPanelLimits::kMaxFactor);
        _view.spikeKeep = eastl::clamp(_view.spikeKeep, ProfilerPanelLimits::kMinKeep, ProfilerPanelLimits::kMaxKeep);
        if (_view.zoneFilter.size() > ProfilerPanelLimits::kMaxFilterBytes) {
            const StringView cut = CutAtBoundary(StringView(_view.zoneFilter), ProfilerPanelLimits::kMaxFilterBytes);
            _view.zoneFilter.resize(cut.size());
        }
    }

    void ProfilerPanelModel::SyncSpikeSettings() {
        SpikeSettings settings = _spikes.GetSettings();
        const auto mode = static_cast<SpikeMode>(_view.spikeMode);
        // Compared as the floats the controls write, so the doubles of the capture never look changed by their own rounding
        const bool changed = mode != settings.mode || static_cast<float>(settings.thresholdMs) != _view.spikeThresholdMs || static_cast<float>(settings.medianFactor) != _view.spikeFactor ||
                             static_cast<int32_t>(settings.keep) != _view.spikeKeep;
        if (!changed) {
            return;
        }
        settings.mode = mode;
        settings.thresholdMs = static_cast<double>(_view.spikeThresholdMs);
        settings.medianFactor = static_cast<double>(_view.spikeFactor);
        settings.keep = static_cast<size_t>(_view.spikeKeep);
        _spikes.SetSettings(settings);
    }

    void ProfilerPanelModel::Capture() {
        if (const FrameRingBackend* ring = GetRing()) {
            ring->Snapshot(_fresh);
            _spikes.Update(_fresh);  // also while paused: the frames go on
        }
        if (_view.paused) {
            return;
        }
        GetStats().GetHistoryInto(FrameStatsCollector::kFrameHistory, _history);
        eastl::swap(_snapshot, _fresh);
        ++_snapshotVersion;
        if (_selectedFrame && (_snapshot.frames.empty() || *_selectedFrame < _snapshot.frames.front().index)) {
            _selectedFrame.reset();  // the ring has overwritten it
        }
    }

    void ProfilerPanelModel::RefreshView() {
        _view.hasFrames = !_history.empty();
        _view.spikeOpened = _openedSpike.has_value();
        if (_view.spikeOpened && (!_openedTextBuilt || _openedTextFor != _openedSpike)) {
            _openedTextBuilt = true;
            _openedTextFor = _openedSpike;
            _view.openedText = Format("Frozen frame {} (the Live button of the spikes goes back)", *_openedSpike);
        }
        const size_t spikeCount = _spikes.GetSpikes().size();
        _view.hasSpikes = spikeCount != 0;
        if (!_spikeTextBuilt || spikeCount != _spikeTextCount) {
            _spikeTextBuilt = true;
            _spikeTextCount = spikeCount;
            _view.spikeText = Format("{} frozen", spikeCount);
        }
        if constexpr (BE_PROFILER != 0) {
            _view.recording = Profiler::GetPrimaryBackend() == &Profiler::GetChromeTrace();
        }
    }

    void ProfilerPanelModel::SelectFrame(uint64_t frameIndex) {
        ShowLive();
        _selectedFrame = frameIndex;
    }

    void ProfilerPanelModel::OpenSpike(size_t spikeIndex) {
        const Vector<FrozenFrame>& spikes = _spikes.GetSpikes();
        if (spikeIndex >= spikes.size()) {
            return;
        }
        _spikeSnapshot = ToSnapshot(spikes[spikeIndex]);
        _openedSpike = spikes[spikeIndex].frameIndex;
        _timelineRequest.reset = true;
        _timelineRequest.mark.reset();
    }

    void ProfilerPanelModel::ShowLive() {
        if (_openedSpike) {
            _openedSpike.reset();
            _spikeSnapshot = RingSnapshot();
            _timelineRequest.reset = true;
            _timelineRequest.mark.reset();
        }
    }

    void ProfilerPanelModel::ShowWorstCall(const ProfileSite& site) {
        const Optional<ZoneHit> hit = FindWorstCall(_snapshot, &site);
        if (!hit) {
            return;
        }
        const ZoneRecord& zone = _snapshot.threads[hit->threadIndex].zones[hit->zoneIndex];
        const RingFrame* frame = FrameAt(_snapshot, zone.beginTicks);
        if (frame == nullptr) {
            return;
        }
        ShowLive();
        _selectedFrame = frame->index;
        _timelineRequest.mark = zone;
    }

    ProfilerTimelineRequest ProfilerPanelModel::TakeTimelineRequest() {
        ProfilerTimelineRequest request = _timelineRequest;
        _timelineRequest = ProfilerTimelineRequest();
        return request;
    }

    bool ProfilerPanelModel::RunCommand(StringView id, StringView argument) {
        const auto is = [id](PoolString command) { return id == command.ToStringView(); };
        if (is(EditorCommandIds::kProfilerTrace)) {
            RunTrace(argument);
            return true;
        }
        if (is(EditorCommandIds::kProfilerSpikeLive)) {
            ShowLive();
            return true;
        }
        if (is(EditorCommandIds::kProfilerSpikeClear)) {
            _spikes.Clear();
            ShowLive();
            return true;
        }
        if (is(EditorCommandIds::kProfilerResetZoneAllocs)) {
            AllocTracker::ResetZoneStats();
            return true;
        }
        return false;
    }

    void ProfilerPanelModel::RunTrace(StringView argument) {
        if (argument == kWriteFrames) {
            WriteFrozen();
            return;
        }
        if constexpr (BE_PROFILER != 0) {
            ChromeTraceBackend& recorder = Profiler::GetChromeTrace();
            const bool recording = Profiler::GetPrimaryBackend() == &recorder;
            if (argument == kRecord && !recording) {
                recorder.MarkTraceStart();
                Profiler::SetBackend(&recorder);
            } else if (argument == kRestart && recording) {
                recorder.MarkTraceStart();
            } else if (argument == kWrite && recording) {
                const auto written = recorder.WriteFile(_view.tracePath);
                _view.traceMessage = written ? Format("Written {} (dropped events: {})", _view.tracePath, recorder.GetDroppedCount()) : written.error();
            }
            _view.recording = Profiler::GetPrimaryBackend() == &recorder;
        }
    }

    void ProfilerPanelModel::WriteFrozen() {
        Vector<FrozenFrame> frames = _spikes.GetSpikes();
        if (frames.empty()) {
            // Nothing is frozen: the frame the view is on, the selected one or the newest
            const RingFrame* frame = nullptr;
            if (!_snapshot.frames.empty()) {
                const Optional<size_t> position = FramePosition(_snapshot, _selectedFrame);
                frame = position ? &_snapshot.frames[*position] : &_snapshot.frames.back();
            }
            if (frame == nullptr) {
                _view.traceMessage = "No frame in the ring to write";
                return;
            }
            frames.push_back(FreezeFrame(_snapshot, *frame, 0.0));
        }
        const auto written = WriteFrozenTraceFile(_view.tracePath, frames);
        _view.traceMessage = written ? Format("Written {} ({} frame(s))", _view.tracePath, frames.size()) : written.error();
    }

}  // namespace BECore
