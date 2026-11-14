#pragma once

#include <BECore/Profiler/Ring/ZoneStats.h>

namespace BECore {

    /**
     * @brief The zone table of the Profiler panel: the statistics of every zone site over a window of frames, with sortable columns, a
     * filter box and a "Timeline" button per row that asks the panel to show the site's longest call.
     *
     * The rows are kept as members and made again only when the snapshot (@c version), the window, the filter or the sort changed, so a
     * paused view costs nothing per frame. Editor only.
     */
    class ProfilerZoneTablePanel {
    public:
        static constexpr uint64_t kRecentFrames = 60;

        /**
         * @param snapshot the ring's snapshot; @p version changes whenever the snapshot does
         * @param selectedFrame a RingFrame::index; "the selected frame" is the newest frame when none is selected
         * @return The site whose "Timeline" button was pressed this frame, nullptr otherwise
         */
        [[nodiscard]] const ProfileSite* Draw(const RingSnapshot& snapshot, uint64_t version, Optional<uint64_t> selectedFrame);

    private:
        enum class Window : int { SelectedFrame, RecentFrames, AllFrames };

        struct StatsKey {
            uint64_t version = 0;
            Window window = Window::SelectedFrame;
            uint64_t firstFrame = 0;
            uint64_t lastFrame = 0;
            bool valid = false;
            bool operator==(const StatsKey&) const = default;
        };

        struct RowsKey {
            uint64_t statsVersion = 0;
            ZoneStatColumn column = ZoneStatColumn::Total;
            bool ascending = false;
            String filter;
            bool valid = false;
            bool operator==(const RowsKey&) const = default;
        };

        void RefreshRows(const RingSnapshot& snapshot, uint64_t version, Optional<uint64_t> selectedFrame);

        Window _window = Window::SelectedFrame;
        ZoneStatColumn _sortColumn = ZoneStatColumn::Total;
        bool _sortAscending = false;
        char _filter[64] = "";
        StatsKey _statsKey;
        RowsKey _rowsKey;
        uint64_t _statsVersion = 0;  // counts the times the statistics were computed, so the rows know theirs is stale
        eastl::vector<ZoneStatRow> _stats;
        eastl::vector<ZoneStatRow> _rows;  // _stats filtered and sorted
    };

}  // namespace BECore
