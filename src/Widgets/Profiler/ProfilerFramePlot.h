#pragma once

#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/Ring/RingSnapshot.h>
#include <BECore/Profiler/Ring/ZoneStats.h>

namespace BECore {

    /**
     * @brief The frame time bars of the Profiler panel: one bar per frame of the collector's history, colored by the budget, with the
     * budget lines; a click on a bar picks the frame of the ring that the bar stands for.
     *
     * The bars show the frame time or its stage split (ExclusiveStageMs), and the counters a frame carries (FrameStats::counters) can be
     * picked and drawn as line graphs over the same frames.
     *
     * Editor only. The budget (16.6 ms, 33.3 ms or a custom value) is a field of the plot for the run, not saved.
     */
    class ProfilerFramePlot {
    public:
        static constexpr double kBudget60Ms = 1000.0 / 60.0;
        static constexpr double kBudget30Ms = 1000.0 / 30.0;

        /**
         * @brief Draws the budget controls and the bars. @p history is the collector's history (oldest first) and @p snapshot the ring
         * taken together with it; @p selectedFrame (a RingFrame::index) is outlined.
         * @return The RingFrame::index of the frame that was clicked, none when the click missed or the bar has no frame in the ring
         */
        [[nodiscard]] std::optional<uint64_t> Draw(const eastl::vector<FrameStats>& history, const RingSnapshot& snapshot, std::optional<uint64_t> selectedFrame);

        [[nodiscard]] double GetBudgetMs() const;

        /**
         * @brief The position in RingSnapshot::frames of the frame that bar @p barIndex of @p barCount stands for, none when the ring is
         * shorter than the history (it was switched on later) or the bar is out of range.
         *
         * The newest bar is the newest ring frame, and the rest count back from it. That holds because Application::RunFrame closes the
         * collector's frame (EndFrame) and then marks the ring's frame (BE_PROFILE_FRAME), once each per frame, and an idle pass of a manual
         * clock does neither; the panel takes the history and the snapshot in the same Update, so both end at the same frame.
         */
        [[nodiscard]] static std::optional<size_t> RingFramePosition(size_t barIndex, size_t barCount, size_t ringFrameCount);

    private:
        enum class Budget : int { Fps60, Fps30, Custom };
        enum class BarMode : int { FrameTime, StageSplit };

        void DrawStageLegend() const;
        void DrawCounters(const eastl::vector<FrameStats>& history, const RingSnapshot& snapshot, std::optional<uint64_t> selectedFrame);

        Budget _budget = Budget::Fps60;
        float _customBudgetMs = 20.0f;
        BarMode _barMode = BarMode::FrameTime;

        // Kept as members so a steady panel allocates nothing: the names are rebuilt when the newest frame number moves
        eastl::vector<PoolString> _counterNames;
        eastl::vector<PoolString> _pickedCounters;
        eastl::vector<float> _counterValues;
        uint64_t _counterNamesFrame = 0;
    };

}  // namespace BECore
