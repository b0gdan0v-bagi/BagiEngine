#pragma once

#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Types/Types.h>
#include <Stress/StressSettings.h>
#include <cstddef>
#include <cstdint>

namespace BECore::Stress {

    /// Where the time of a frame went, in milliseconds: the stages of one frame, or a statistic of each over many
    struct StageTimes {
        double updateMs = 0.0;
        double drawMs = 0.0;    // the SceneDraw stage without the submissions inside it
        double submitMs = 0.0;  // the batcher's submissions to the backend
        double presentMs = 0.0;
        double waitMs = 0.0;  // the backend's wait for the GPU and the swapchain
        double gpuMs = 0.0;   // 0: no value (the backend measures none, or has none for this frame), so no statistic counts it
    };

    /// The mean of the stage times of the frames added so far, `gpuMs` over those that have one; allocates nothing
    class StageSum {
    public:
        void Add(const StageTimes& frame);
        /// 0 for a quantity without a frame
        StageTimes Mean() const;

    private:
        StageTimes _sum;
        uint32_t _frames = 0;
        uint32_t _gpuFrames = 0;
    };

    /// What one measured frame cost (a `FrameStats` of the collector, reduced to what the JSON line reports)
    struct StressSample : StageTimes {
        double cpuMs = 0.0;    // from BeginFrame to EndFrame
        double workMs = 0.0;   // cpuMs without Present and Wait: headless the present is the software renderer's copy, in a window the two wait for the display
        double frameMs = 0.0;  // end to end, from the end of the frame before (on a manual clock it includes the idle between steps)
        uint32_t drawCalls = 0;
        uint64_t triangles = 0;
        uint32_t textureSwitches = 0;
        uint64_t allocCount = 0;  // 0 without ENGINE_PROFILER_ALLOC
        uint64_t allocBytes = 0;
    };

    /// The sample of @p stats; `workMs` is `cpuMs` minus the Present and Wait stages and `drawMs` is SceneDraw minus Submit, neither below 0
    StressSample SampleOf(const FrameStats& stats);

    /**
     * @brief The nearest-rank percentile of values sorted ascending: `sorted[ceil(fraction * n) - 1]`, the smallest value that at least
     *        @p fraction of the values are not above. One value is every percentile, so no percentile is ever invented between two frames
     *        (a linear interpolation would report a frame time no frame had). 0 for no values. The product is nudged down by a part in a
     *        billion so that 0.5 * 100 is rank 50, not 51, whatever the rounding of the product.
     *
     * The recorder (p50, p99) and the ramp's step verdict (p95 of its measured frames) both use this one function.
     */
    double NearestRank(Span<const double> sorted, double fraction);

    /// What a run measured: percentiles of the times and means
    struct StressSummary {
        uint32_t frames = 0;
        double frameMsP50 = 0.0;
        double frameMsP99 = 0.0;
        double cpuMsP50 = 0.0;
        double cpuMsP99 = 0.0;
        double workMsP50 = 0.0;
        double workMsP99 = 0.0;
        double cpuMs = 0.0;  // the means from here on
        double workMs = 0.0;
        double drawCalls = 0.0;
        double triangles = 0.0;
        double textureSwitches = 0.0;
        double allocsPerFrame = 0.0;
        double allocBytesPerFrame = 0.0;
        StageTimes stages;     // means
        StageTimes stagesP50;  // medians, `gpuMs` of the frames that have one
        double gpuMsP99 = 0.0;
    };

    /**
     * @brief Keeps the samples of the measured frames in a buffer of a fixed capacity and summarizes them.
     *
     * `Reserve(frames)` when the run starts; `Add` after that allocates nothing, and a sample past the capacity is ignored (never grown: a
     * measurement must not allocate in the frames it measures). Pure: no clock, no scene.
     */
    class StressRecorder {
    public:
        /// Empties the recorder and makes room for @p frames samples
        void Reserve(uint32_t frames);
        /// False (and nothing kept) when the recorder is full
        bool Add(const StressSample& sample);

        size_t Size() const {
            return _samples.size();
        }
        /// How many samples it takes: what `Reserve` was given
        size_t Capacity() const {
            return _limit;
        }

        StressSummary Summarize() const;

    private:
        Vector<StressSample> _samples;
        size_t _limit = 0;
    };

    /// The run the line describes (what the summary alone does not say)
    struct StressRunContext {
        StressScene scene = StressScene::Sprites;
        uint32_t count = 0;
        uint32_t warmup = 0;
        bool interleave = false;
        uint32_t atlases = 0;
        bool batching = true;      // the renderer batched 2D quads during the run
        bool manualClock = false;  // `frameMs` of a manual-clock run includes the idle between steps: it is reported, nothing gates on it
        bool allocTracking = false;
    };

    /// A number as a plain decimal (`12.3457`, never `1e-05`, `nan` or `inf`: a value that is not finite is 0)
    String FormatNumber(double value);

    /**
     * @brief The one JSON line of a fixed run, no newline: the keys sorted (as `headless_frames.py` writes with `sort_keys`), numbers as plain
     *        decimals; the keys are listed in `CLAUDE.md`, "Fixed mode". Written by hand because the line has a fixed set of keys of plain numbers: it needs no writer state and no escaping.
     */
    String FormatJsonLine(const StressSummary& summary, const StressRunContext& context);

}  // namespace BECore::Stress
