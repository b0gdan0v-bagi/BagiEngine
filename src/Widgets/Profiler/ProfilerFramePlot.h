#pragma once

#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Ring/RingSnapshot.h>
#include <BECore/Profiler/Ring/ZoneStats.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore {

    struct ProfilerPanelView;

    /// The preset budgets of the plot; `Custom` is a field of its own
    CORE_ENUM(ProfilerBudget, uint8_t, Fps60, Fps30, Custom)

    /// What a bar shows: the frame time, or its split into the stages of `ExclusiveStageMs`
    CORE_ENUM(ProfilerBarMode, uint8_t, FrameTime, StageSplit)

    /**
     * @brief The frame time bars of the Profiler panel: one bar per frame of the collector's history, colored by the budget, with the
     * budget lines; a click on a bar picks the frame of the ring that the bar stands for.
     *
     * The bars show the frame time or its stage split (ExclusiveStageMs); "Work only" takes the Wait stage (the backend's wait for the
     * display) out of the bar and of its scale, and a line under the plot gives the mean work and the mean wait. The counters a frame
     * carries (FrameStats::counters) can be picked and drawn as line graphs over the same frames.
     *
     * Editor only. The budget, the bar mode and "Work only" are the panel asset's controls (`ProfilerPanelView`); the plot is handed them as `Settings`.
     */
    class ProfilerFramePlot {
    public:
        static constexpr double kBudget60Ms = 1000.0 / 60.0;
        static constexpr double kBudget30Ms = 1000.0 / 30.0;

        /// How the bars are read; the panel asset's controls write them into the model's view (`PlotSettingsOf`)
        struct Settings {
            double budgetMs = kBudget60Ms;
            ProfilerBarMode barMode = ProfilerBarMode::FrameTime;
            bool workOnly = false;
        };

        /**
         * @brief Draws the bars with the stage legend, the work / wait summary and the counters. @p history is the collector's history (oldest first)
         * and @p snapshot the ring taken together with it; @p selectedFrame (a RingFrame::index) is outlined.
         * @return The RingFrame::index of the frame that was clicked, none when the click missed or the bar has no frame in the ring
         */
        [[nodiscard]] Optional<uint64_t> DrawBars(const Vector<FrameStats>& history, const RingSnapshot& snapshot, Optional<uint64_t> selectedFrame, const Settings& settings);

        /**
         * @brief The position in RingSnapshot::frames of the frame that bar @p barIndex of @p barCount stands for, none when the ring is
         * shorter than the history (it was switched on later) or the bar is out of range.
         *
         * The newest bar is the newest ring frame, and the rest count back from it. That holds because Application::RunFrame closes the
         * collector's frame (EndFrame) and then marks the ring's frame (BE_PROFILE_FRAME), once each per frame, and an idle pass of a manual
         * clock does neither; the panel takes the history and the snapshot in the same Update, so both end at the same frame.
         */
        [[nodiscard]] static Optional<size_t> RingFramePosition(size_t barIndex, size_t barCount, size_t ringFrameCount);

    private:
        void DrawStageLegend(bool workOnly) const;
        void DrawWorkWaitSummary(const Vector<FrameStats>& history);
        void DrawCounters(const Vector<FrameStats>& history, const RingSnapshot& snapshot, Optional<uint64_t> selectedFrame);

        // Kept as members so a steady panel allocates nothing: the names are rebuilt when the newest frame number moves
        Vector<PoolString> _counterNames;
        Vector<PoolString> _pickedCounters;
        Vector<float> _counterValues;
        uint64_t _counterNamesFrame = 0;

        // The summary line, formatted again only when the newest frame moves
        static constexpr size_t kSummaryCapacity = 64;
        Array<char, kSummaryCapacity> _summaryText{};
        uint64_t _summaryFrame = 0;
        size_t _summaryFrameCount = 0;
        bool _summaryBuilt = false;
    };

    /// The budget the view's combo (and its custom field) says, in ms
    [[nodiscard]] double BudgetMs(const ProfilerPanelView& view);

    [[nodiscard]] ProfilerFramePlot::Settings PlotSettingsOf(const ProfilerPanelView& view);

}  // namespace BECore
