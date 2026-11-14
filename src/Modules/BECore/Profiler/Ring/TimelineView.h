#pragma once

#include <BECore/Profiler/Ring/RingSnapshot.h>

namespace BECore {

    /// One row of the lane list: a thread with something to draw in the visible range
    struct TimelineLane {
        size_t threadIndex = 0;  // into RingSnapshot::threads
        uint32_t rows = 1;       // deepest zone depth in the range plus one
    };

    /// A zone found by its place: the thread and the index into that thread's zones
    struct ZoneHit {
        size_t threadIndex = 0;
        size_t zoneIndex = 0;
    };

    struct TickRange {
        uint64_t begin = 0;
        uint64_t end = 0;
    };

    /**
     * @brief The visible tick range of the profiler's timeline and the geometry the panel draws and hit-tests with; no ImGui, no clock.
     *
     * Ticks are the ProfilerClock ticks of a RingSnapshot. The range is kept as doubles so a zoom can be any factor. SetExtent gives
     * the data the view may show (the ring's ticks) and the tick length; zoom and pan are clamped to it, and a zoom stops at
     * kMinSpanMicroseconds. FitTo is not clamped to the extent: it shows exactly the range asked for (a frame) down to the minimum span.
     */
    class TimelineView {
    public:
        static constexpr double kMinSpanMicroseconds = 5.0;

        /// Without an extent the view has none to clamp to and shows what FitTo gave it
        void SetExtent(uint64_t beginTicks, uint64_t endTicks, double nanosecondsPerTick);

        void FitTo(uint64_t beginTicks, uint64_t endTicks);

        /// Scales the visible span by @p factor (below 1 zooms in) keeping @p anchorTick at the same fraction of the width
        void ZoomAt(double anchorTick, double factor);

        /// Moves the range by @p deltaTicks (positive: later), keeping its span inside the extent
        void PanByTicks(double deltaTicks);

        [[nodiscard]] double BeginTicks() const {
            return _begin;
        }
        [[nodiscard]] double EndTicks() const {
            return _begin + _span;
        }
        [[nodiscard]] double SpanTicks() const {
            return _span;
        }

        [[nodiscard]] float TickToX(double tick, float widthPixels) const;
        [[nodiscard]] double XToTick(float x, float widthPixels) const;
        [[nodiscard]] double PixelsPerTick(float widthPixels) const;

    private:
        [[nodiscard]] double MinSpan() const;
        void ClampToExtent();

        double _begin = 0.0;
        double _span = 1.0;
        double _extentBegin = 0.0;
        double _extentEnd = 0.0;  // equal to the begin: no extent
        double _nanosecondsPerTick = 1.0;
    };

    /// The ticks of the ring: from the first frame's begin to the last one's end, else from the first zone to the last (empty snapshot: 0, 0)
    [[nodiscard]] TickRange SnapshotExtent(const RingSnapshot& snapshot);

    /**
     * @brief The threads that have a zone overlapping [beginTicks, endTicks), in the order of the contract: by sort index
     * (threads without one, Profiler::kNoSortIndex, after those with), then by thread id.
     */
    [[nodiscard]] eastl::vector<TimelineLane> LayoutLanes(const RingSnapshot& snapshot, double beginTicks, double endTicks);

    /// The y of the top of lane @p laneIndex when the lanes are stacked from y = 0, each `rows * rowHeight` high with @p laneGap between
    [[nodiscard]] float LaneTop(const eastl::vector<TimelineLane>& lanes, size_t laneIndex, float rowHeight, float laneGap);

    /// The one-pixel rule: a zone narrower than a pixel is not drawn (and not hit); the panel merges nothing
    [[nodiscard]] bool IsVisible(const ZoneRecord& zone, double pixelsPerTick);

    /**
     * @brief The zone under the point (@p x, @p y) of the lane area (y = 0 is the top of the first lane), or none. The row of a zone
     * is its depth; a point in a gap, below the last row or on a zone that is not visible hits nothing.
     */
    [[nodiscard]] Optional<ZoneHit> HitTest(const eastl::vector<TimelineLane>& lanes, const RingSnapshot& snapshot, const TimelineView& view, float x, float y, float rowHeight, float widthPixels,
                                            float laneGap = 0.0f);

    /// The longest call of @p site on any thread (the first one on a tie)
    [[nodiscard]] Optional<ZoneHit> FindWorstCall(const RingSnapshot& snapshot, const ProfileSite* site);

    /// The frame whose interval holds @p tick, nullptr when none does
    [[nodiscard]] const RingFrame* FrameAt(const RingSnapshot& snapshot, uint64_t tick);

    /// The position in RingSnapshot::frames of the frame numbered @p frameIndex (RingFrame::index), none when there is none or the ring no longer holds it
    [[nodiscard]] Optional<size_t> FramePosition(const RingSnapshot& snapshot, Optional<uint64_t> frameIndex);

}  // namespace BECore
