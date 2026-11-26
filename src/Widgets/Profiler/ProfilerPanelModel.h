#pragma once

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Ring/FrameRingBackend.h>
#include <BECore/Profiler/Ring/RingSnapshot.h>
#include <BECore/Profiler/Ring/SpikeCapture.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/Profiler/ProfilerSlots.h>
#include <cstdint>

namespace BECore {

    namespace SDLEvents {
        struct SDLEventWrapper;
    }

    /// The ranges the Profiler's number fields are written in: `ProfilerPanel.xml` says the same, and the model clamps what a control wrote to them
    struct ProfilerPanelLimits {
        static constexpr float kMinCustomBudgetMs = 1.0f;
        static constexpr float kMaxCustomBudgetMs = 1000.0f;
        static constexpr float kMinThresholdMs = 1.0f;
        static constexpr float kMaxThresholdMs = 10000.0f;
        static constexpr float kMinFactor = 1.1f;
        static constexpr float kMaxFactor = 100.0f;
        static constexpr int32_t kMinKeep = 1;
        static constexpr int32_t kMaxKeep = 64;
        /// The zone filter keeps its old buffer's room (a 64 byte field with its terminator)
        static constexpr size_t kMaxFilterBytes = 63;
    };

    /**
     * @brief What `ProfilerPanel.xml` binds to: the controls' state and the texts the model makes
     *
     * The first block is written by the panel's controls with no command and read by the model in its next `Update` (clamped to `ProfilerPanelLimits`, the
     * combo indices to their label lists). The second block is written by the model; the label lists are filled once.
     */
    struct ProfilerPanelView {
        BE_CLASS(ProfilerPanelView)

        BE_REFLECT_FIELD bool paused = false;
        BE_REFLECT_FIELD bool workOnly = false;
        /// An index into `budgetLabels`: 60 fps, 30 fps, custom
        BE_REFLECT_FIELD int32_t budget = 0;
        BE_REFLECT_FIELD float customBudgetMs = 20.0f;
        /// An index into `barModeLabels`
        BE_REFLECT_FIELD int32_t barMode = 0;
        /// An index into `zoneWindowLabels`
        BE_REFLECT_FIELD int32_t zoneWindow = 0;
        BE_REFLECT_FIELD String zoneFilter;
        /// An index into `spikeModeLabels`, the values of `SpikeMode`
        BE_REFLECT_FIELD int32_t spikeMode = 0;
        BE_REFLECT_FIELD float spikeThresholdMs = static_cast<float>(SpikeSettings::kDefaultThresholdMs);
        BE_REFLECT_FIELD float spikeFactor = static_cast<float>(SpikeSettings::kDefaultMedianFactor);
        BE_REFLECT_FIELD int32_t spikeKeep = static_cast<int32_t>(SpikeSettings::kDefaultKeep);
        BE_REFLECT_FIELD String tracePath = "trace.json";

        BE_REFLECT_FIELD Vector<String> budgetLabels;
        BE_REFLECT_FIELD Vector<String> barModeLabels;
        BE_REFLECT_FIELD Vector<String> zoneWindowLabels;
        BE_REFLECT_FIELD Vector<String> spikeModeLabels;
        BE_REFLECT_FIELD bool hasFrames = false;
        /// This build records zones (ENGINE_PROFILER)
        BE_REFLECT_FIELD bool zonesRecorded = false;
        /// This build counts allocations (ENGINE_PROFILER_ALLOC)
        BE_REFLECT_FIELD bool allocTracked = false;
        /// The editor's host can give the user a Chrome trace file (`profiler.trace` is offered)
        BE_REFLECT_FIELD bool traceFileSupported = true;
        /// The Chrome trace recorder is the primary backend
        BE_REFLECT_FIELD bool recording = false;
        BE_REFLECT_FIELD bool spikeOpened = false;
        BE_REFLECT_FIELD String openedText;
        BE_REFLECT_FIELD bool hasSpikes = false;
        BE_REFLECT_FIELD String spikeText;
        BE_REFLECT_FIELD String traceMessage;
    };

    /// What the timeline slot has to do to its panel after the model changed what it shows; taken once per draw
    struct ProfilerTimelineRequest {
        bool reset = false;         ///< forget the view and follow again (a spike was opened, or the live ring is back)
        Optional<ZoneRecord> mark;  ///< show this call and mark it, after the reset
    };

    /**
     * @brief The model of the Profiler panel asset: the controls' state, the frame history and the ring's snapshot, the spike capture and the Chrome
     * trace commands, with no ImGui
     *
     * Each frame the panel is open `Update` takes the collector's history and one snapshot of the frame ring together (not while Pause is on: the view
     * keeps its copy) and the capture judges every fresh snapshot, paused or not. The ring runs only while the panel is open: `OnOpenChanged` switches it,
     * and the destructor switches it off, since a widget can die in the middle of the run (a mode switch). The panel's own work (history, lanes, ImGui's
     * memory) is out of the allocation counts: `Update` pauses them and `EndUpdate` resumes them, which is why a closed pair is never left half open.
     *
     * The model is alive while the panel is closed, so it reads Ctrl+Shift+P from the raw SDL event (it works while a text field has the keyboard) and
     * sends `view.profiler`, which the asset's `command` turns into the open flag. Commands (`EditorCommandIds`, run only while the panel is open):
     * `profiler.trace` takes the literal argument `record`, `restart`, `write` or `writeFrames`, `profiler.spikeLive`, `profiler.spikeClear` and
     * `profiler.resetZoneAllocs` take none; an unknown or empty argument does nothing.
     *
     * The slots (`ProfilerSlots`, registered in `Initialize`) read the history, the snapshot and the capture from here and report their clicks back through `SelectFrame`,
     * `OpenSpike`, `ShowLive` and `ShowWorstCall`.
     */
    class ProfilerPanelModel : public IPanelModel, public SubscriptionHolder {
        BE_CLASS(ProfilerPanelModel)
    public:
        ProfilerPanelModel();
        ~ProfilerPanelModel() override;

        void Initialize() override;
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        void EndUpdate() override;
        void OnOpenChanged(bool open) override;
        bool RunCommand(StringView id, StringView argument) override;

        /// Reads this collector instead of `CoreManager::GetFrameStats()` (a test); it must outlive the model. Null goes back to the engine's
        void SetFrameStats(const FrameStatsCollector* stats);

        /// Snapshots this ring instead of the process's, also in a build that records no zones (a test); it must outlive the model. Null goes back
        void SetFrameRing(const FrameRingBackend* ring);

        /// Whether `Update` holds the allocation counts paused until `EndUpdate` (default). A test that checks a frame allocates nothing turns it off,
        /// since the paused work would not be counted either way
        void SetPausesAllocCounting(bool pauses);

        [[nodiscard]] const ProfilerPanelView& GetView() const {
            return _view;
        }

        /// The collector's history as of the last unpaused `Update`, oldest first
        [[nodiscard]] const Vector<FrameStats>& GetHistory() const {
            return _history;
        }

        /// The ring as of the last unpaused `Update`; the zone table's rows are stale when `GetSnapshotVersion` moved
        [[nodiscard]] const RingSnapshot& GetSnapshot() const {
            return _snapshot;
        }

        [[nodiscard]] uint64_t GetSnapshotVersion() const {
            return _snapshotVersion;
        }

        /// A `RingFrame::index`
        [[nodiscard]] Optional<uint64_t> GetSelectedFrame() const {
            return _selectedFrame;
        }

        [[nodiscard]] const SpikeCapture& GetSpikes() const {
            return _spikes;
        }

        /// The `frameIndex` of the spike the timeline shows, none: the live ring
        [[nodiscard]] Optional<uint64_t> GetOpenedSpike() const {
            return _openedSpike;
        }

        /// The opened spike as a snapshot; empty while the timeline is live
        [[nodiscard]] const RingSnapshot& GetSpikeSnapshot() const {
            return _spikeSnapshot;
        }

        /// A click on a bar: the live ring again, with @p frameIndex (a `RingFrame::index`) selected
        void SelectFrame(uint64_t frameIndex);

        /// The timeline shows the spike at @p spikeIndex of `GetSpikes().GetSpikes()`; an index out of range is ignored
        void OpenSpike(size_t spikeIndex);

        /// The timeline shows the live ring again
        void ShowLive();

        /// The zone table's "Timeline" button: selects the frame of the site's longest call in the live ring and marks that call
        void ShowWorstCall(const ProfileSite& site);

        /// What the timeline panel has to do since the last call; clears it
        [[nodiscard]] ProfilerTimelineRequest TakeTimelineRequest();

    private:
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event);

        void ClampControls();
        void SyncSpikeSettings();
        /// Makes the ring run exactly while the panel is open (and zones are recorded in this build)
        void SyncRing();
        void Capture();
        void RefreshView();
        void RunTrace(StringView argument);
        /// Writes the frozen spikes, or the frame the view is on when none is frozen, to `tracePath`
        void WriteFrozen();
        [[nodiscard]] const FrameStatsCollector& GetStats() const;
        [[nodiscard]] const FrameRingBackend* GetRing() const;

        ProfilerPanelView _view;
        ProfilerSlots _slots{*this};
        bool _open = false;
        bool _ringRequested = false;  // what this model asked of Profiler::SetRingEnabled
        bool _pausesAllocCounting = true;
        Optional<ScopedAllocCountersPause> _allocPause;
        const FrameStatsCollector* _stats = nullptr;
        const FrameRingBackend* _ring = nullptr;
        Vector<FrameStats> _history;
        RingSnapshot _snapshot;  // what the panels show
        // The ring is copied into this one and swapped with _snapshot, so both keep the capacity of their vectors and a steady frame allocates
        // nothing for the copy; while paused it is refilled and the spikes still see it, _snapshot stays
        RingSnapshot _fresh;
        uint64_t _snapshotVersion = 0;
        Optional<uint64_t> _selectedFrame;
        SpikeCapture _spikes;
        Optional<uint64_t> _openedSpike;
        RingSnapshot _spikeSnapshot;
        ProfilerTimelineRequest _timelineRequest;
        // The texts are made again only when what they show moved
        Optional<uint64_t> _openedTextFor;
        bool _openedTextBuilt = false;
        size_t _spikeTextCount = 0;
        bool _spikeTextBuilt = false;
    };

}  // namespace BECore
