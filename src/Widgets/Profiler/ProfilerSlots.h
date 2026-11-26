#pragma once

#include <Widgets/PanelAssets/PanelSlots.h>
#include <Widgets/Profiler/ProfilerFramePlot.h>
#include <Widgets/Profiler/ProfilerSpikePanel.h>
#include <Widgets/Profiler/ProfilerTimelinePanel.h>
#include <Widgets/Profiler/ProfilerZoneTablePanel.h>

namespace BECore {

    class ProfilerPanelModel;

    /**
     * @brief The drawers of `ProfilerPanel.xml` that the format cannot say: the frame bars, the timeline, the zone table, the list of frozen
     * frames, the heap totals and the table of allocations per zone
     *
     * Owned by `ProfilerPanelModel`, which registers them in its `Initialize` and takes them back when it dies. The controls around them (budget, bar
     * mode, window, filter, the spike settings and buttons) are the asset's and reach a slot through the model's view; a slot reports its clicks back
     * to the model and keeps only what is drawn state (a zone table's rows and sort, the timeline's zoom, the counters picked). A slot draws what its
     * section holds and checks nothing about the build: the asset hides a section that has nothing to show.
     */
    class ProfilerSlots {
    public:
        explicit ProfilerSlots(ProfilerPanelModel& model) : _model(model) {}

        /// Registers the six slots in the global `PanelSlots`; the handles end with this object
        void Register();

    private:
        void DrawFrames();
        void DrawTimeline();
        void DrawZones();
        void DrawSpikes();
        void DrawMemory();
        void DrawZoneAllocs();

        ProfilerPanelModel& _model;
        ProfilerFramePlot _framePlot;
        ProfilerTimelinePanel _timeline;
        ProfilerZoneTablePanel _zoneTable;
        ProfilerSpikePanel _spikePanel;
        PanelSlotHandle _framesSlot;
        PanelSlotHandle _timelineSlot;
        PanelSlotHandle _zonesSlot;
        PanelSlotHandle _spikesSlot;
        PanelSlotHandle _memorySlot;
        PanelSlotHandle _zoneAllocsSlot;
    };

}  // namespace BECore
