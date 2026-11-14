#pragma once

#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Ring/RingSnapshot.h>
#include <BECore/Profiler/Ring/SpikeCapture.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/Profiler/ProfilerFramePlot.h>
#include <Widgets/Profiler/ProfilerSpikePanel.h>
#include <Widgets/Profiler/ProfilerTimelinePanel.h>
#include <Widgets/Profiler/ProfilerZoneTablePanel.h>

namespace BECore {

    namespace SDLEvents {
        struct SDLEventWrapper;
    }

    /**
     * @brief The editor's Profiler panel: the frame bars with the budget, the timeline of a frame's zones, the zone table, the spikes,
     * the last frame's statistics, the heap, the allocations per zone and the Chrome trace controls.
     *
     * Hidden at start; Ctrl+Shift+P (read from the raw SDL event, Profiler/ProfilerToggle.h) or the command `view.profiler` shows and
     * hides it, and its close button hides it. Editor only (src/Widgets/Profiler/ is in the editor folders of src/CMakeLists.txt).
     *
     * The frame ring (Profiler::SetRingEnabled) runs only while the panel is open and the build records zones; the panel switches it off
     * when it closes and when it is destroyed. Each frame it is open the panel takes the collector's history and one snapshot of the
     * ring together (not while Pause is on: the view keeps its copy), and the sub-panels draw from those copies. The spike capture judges
     * every fresh snapshot, paused or not, and the timeline shows either the live ring or one frozen spike.
     */
    class ProfilerWidget : public IWidget {
        BE_CLASS(ProfilerWidget)
    public:
        ProfilerWidget() = default;
        ~ProfilerWidget() override;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        void OnCommand(const EditorCommandEvent& event);
        void OnSDLEvent(const SDLEvents::SDLEventWrapper& event);

        /// Makes the ring run exactly while the panel is open (and zones are recorded in this build)
        void SyncRing();
        void Capture();
        /// The timeline shows @p spikeIndex of the capture, or the live ring again
        void OpenSpike(size_t spikeIndex);
        void ShowLive();
        /// The zone table's "Timeline" button: opens the frame of the site's longest call in the live ring and marks that call
        void ShowWorstCall(const ProfileSite& site);

        void DrawFrames();
        void DrawTimeline();
        void DrawZoneTable();
        void DrawSpikes();
        void DrawMemory();
        void DrawZoneAllocs();
        void DrawTrace();
        /// Writes the frozen spikes, or the frame the view is on when none is frozen, to _tracePath
        void WriteFrozen();

        BE_REFLECT_FIELD bool _open = false;
        bool _paused = false;
        bool _ringRequested = false;  // what this widget asked of Profiler::SetRingEnabled
        eastl::vector<FrameStats> _history;
        RingSnapshot _snapshot;
        uint64_t _snapshotVersion = 0;      // counts the snapshots taken, so the zone table knows when its rows are stale
        Optional<uint64_t> _selectedFrame;  // a RingFrame::index
        SpikeCapture _spikes;
        Optional<uint64_t> _openedSpike;  // the frameIndex of the spike the timeline shows, none: the live ring
        RingSnapshot _spikeSnapshot;      // that spike as a snapshot
        ProfilerFramePlot _framePlot;
        ProfilerTimelinePanel _timeline;
        ProfilerZoneTablePanel _zoneTable;
        ProfilerSpikePanel _spikePanel;
        eastl::string _traceMessage;
        char _tracePath[256] = "trace.json";
    };

}  // namespace BECore
