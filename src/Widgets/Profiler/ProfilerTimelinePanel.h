#pragma once

#include <BECore/Profiler/Ring/TimelineView.h>

struct ImVec2;

namespace BECore {

    /**
     * @brief The timeline of the Profiler panel: one lane per thread, the zones as bars at the row of their depth, wheel zoom around the
     * mouse, drag to pan, a tooltip on the zone under the mouse. The geometry is TimelineView's (tested without ImGui); this draws it.
     *
     * It shows a RingSnapshot it is handed, so the live ring and a frozen spike go through the same code. With no frame selected it
     * follows the newest frame; zooming or panning stops that until "Follow newest" or "Fit". Editor only.
     */
    class ProfilerTimelinePanel {
    public:
        /// The source of the snapshot changed (live, a frozen frame): forget the view and follow the newest frame again
        void Reset();

        /// Marks @p zone with an outline and fits the view to its frame (the caller has made that frame the selected one)
        void ShowZone(const ZoneRecord& zone);

        /// @p selectedFrame is a RingFrame::index of @p snapshot, none for the newest frame
        void Draw(const RingSnapshot& snapshot, Optional<uint64_t> selectedFrame);

    private:
        void DrawToolbar(const RingSnapshot& snapshot, const RingFrame& frame, bool hasSelection);
        void DrawCanvas(const RingSnapshot& snapshot, const RingFrame& frame);
        void DrawRuler(const RingSnapshot& snapshot, const RingFrame& frame, const ImVec2& origin, float width, float gridHeight);
        void FitFrame(const RingFrame& frame);

        TimelineView _view;
        eastl::vector<TimelineLane> _lanes;
        bool _follow = true;
        bool _fitRequested = false;
        Optional<uint64_t> _shownSelection;
        const ProfileSite* _markedSite = nullptr;  // the call ShowZone marked: its site and begin tick
        uint64_t _markedBeginTicks = 0;
    };

}  // namespace BECore
