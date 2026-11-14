#pragma once

#include <EASTL/array.h>
#include <Stress/StressRecorder.h>

namespace BECore::Stress {

    /// The count the ramp starts at (the app shows it from its first frame: `StressGame` puts it into the state)
    inline constexpr uint32_t kRampStart = 100;
    /// A step is this many frames that are thrown away (the nodes, textures and glyphs of the new count are built in them) ...
    inline constexpr uint32_t kSettleFrames = 10;
    /// ... and then this many frames that are measured
    inline constexpr uint32_t kMeasureFrames = 30;
    /// While a step passes the count grows by this factor (and at least by 1)
    inline constexpr double kRampGrowth = 1.5;
    /// The bisection stops when the bracket is no wider than this part of the last passing count (and no narrower than 1)
    inline constexpr double kRampPrecision = 0.02;
    /// The frame time a step must keep: a frame that fits 60 fps (1000 / 60 = 16.67, rounded down)
    inline constexpr double kRampBudgetMs = 16.6;
    /// The part of the measured frames that must fit the budget: the 95th percentile, nearest rank (of 30 frames the 29th: one slow frame is forgiven, two are not)
    inline constexpr double kRampPercentile = 0.95;
    /// How many times the final re-measure may fail and step the result down before the ramp gives up on verifying it
    inline constexpr uint32_t kMaxStepDowns = 3;
    /// The ramp ends after this many frames whatever it is doing, so a stuck run cannot hang a benchmark
    inline constexpr uint32_t kMaxRampFrames = 20000;

    /// What the app must show after a frame
    struct RampNext {
        uint32_t count = 0;  // the count to show from now on (the same as before while a step is still running)
        bool done = false;   // the ramp is over: GetResult() has the answer
    };

    struct RampResult {
        uint32_t objectsAt60fps = 0;  // the largest count whose frames fit the budget; 0 when even kRampStart does not
        bool capped = false;          // kMaxCount fit: the machine is faster than the app can ask
        bool aborted = false;         // kMaxRampFrames went first: objectsAt60fps is the best count found so far
        bool verified = false;        // the count passed its own re-measure at the end
        uint32_t steps = 0;           // how many steps (settle + measure) ran
        uint32_t frames = 0;          // how many frames the ramp took
    };

    /**
     * @brief The search for "objects at 60 fps": the largest count whose frames fit the budget. Pure: no clock, no scene; the caller feeds it
     *        the measure of every frame and shows the count it returns.
     *
     * The algorithm, exactly (the constants above):
     * 1. Start at `kRampStart`. A **step** is `kSettleFrames` discarded frames, then `kMeasureFrames` measured; it **passes** when the
     *    `kRampPercentile` of the measured frames (nearest rank, the function the recorder uses) is at most `kRampBudgetMs`. The p95 and not
     *    one frame: one slow frame (the OS, a glyph page) must not end the ramp; and not the mean: a 60 fps game loses its frame to the tail.
     * 2. While a step passes the count grows by `kRampGrowth` (at least +1). A pass at `kMaxCount` ends the ramp with `capped`.
     * 3. At the first failure with a passing count `lo` behind it (`hi` the failing one) the ramp **bisects**: it measures `lo + (hi - lo) / 2` and
     *    that becomes the new `lo` when it passes and the new `hi` when it fails, until `hi - lo <= max(1, lo * kRampPrecision)`. A failure at
     *    `kRampStart` ends the ramp with 0.
     * 4. The result is `lo`, **measured once more** at the end: noise must not publish a count the frame does not hold. A candidate that fails its
     *    re-measure steps down by `max(1, lo * kRampPrecision)` (one resolution of the search) and is measured again, at most `kMaxStepDowns` times;
     *    when the last one fails too it is published with `verified` false.
     * 5. After `kMaxRampFrames` frames the ramp ends with `aborted`, publishing the best passing count seen.
     *
     * Every count it returns is in `1..kMaxCount`.
     */
    class RampController {
    public:
        /// @p maxFrames is the abort limit (a test lowers it)
        explicit RampController(StressMeasure measure, uint32_t maxFrames = kMaxRampFrames);

        /// The count to show before the first frame
        uint32_t GetCount() const {
            return _count;
        }

        /// Feeds the measure (the work or the cpu time in milliseconds, by `StressMeasure`) of the frame that was just shown with GetCount()
        RampNext OnFrame(double measureMs);

        bool IsDone() const {
            return _phase == Phase::Done;
        }
        const RampResult& GetResult() const {
            return _result;
        }
        StressMeasure GetMeasure() const {
            return _measure;
        }

    private:
        enum class Phase : uint8_t { Growing, Bisecting, Verifying, Done };

        /// A step has just ended: moves on according to its verdict
        void OnStepEnd(bool passed);
        /// Looks at the bracket (`_lo`, `_hi`): measures its middle, or starts verifying `_lo` when it is narrow enough
        void Narrow();
        void Finish(uint32_t count, bool verified);
        /// Shows @p count from the next frame and starts its settle frames
        void Show(uint32_t count);

        StressMeasure _measure;
        uint32_t _maxFrames;
        Phase _phase = Phase::Growing;
        uint32_t _count = kRampStart;
        uint32_t _lo = 0;  // the largest count that passed, 0 for none
        uint32_t _hi = 0;  // the smallest count that failed after a pass, 0 for none
        uint32_t _stepDown = 1;
        uint32_t _stepDowns = 0;
        uint32_t _settleLeft = kSettleFrames;
        uint32_t _measured = 0;
        Array<double, kMeasureFrames> _window{};
        RampResult _result;
    };

    /// The run the line describes
    struct RampRunContext {
        StressScene scene = StressScene::Sprites;
        bool interleave = false;
        uint32_t atlases = 0;
        bool manualClock = false;
        bool allocTracking = false;
    };

    /**
     * @brief The one JSON line of a ramp, no newline, keys sorted: `scene`, `mode` ("ramp"), `objectsAt60fps`, `budgetMs`, `measure`, `capped`,
     *        `aborted`, `verified`, `steps`, `frames`, `settleFrames`, `measureFrames`, `interleave`, `atlases`, `manualClock`, `allocTracking`.
     */
    String FormatRampLine(const RampResult& result, StressMeasure measure, const RampRunContext& context);

}  // namespace BECore::Stress
