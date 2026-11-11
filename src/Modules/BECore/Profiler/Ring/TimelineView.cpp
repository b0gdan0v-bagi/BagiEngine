#include "TimelineView.h"

#include <BECore/Profiler/Profiler.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr double kNanosecondsPerMicrosecond = 1000.0;
        constexpr double kOnePixel = 1.0;

        bool Overlaps(const ZoneRecord& zone, double beginTicks, double endTicks) {
            return static_cast<double>(zone.beginTicks)<endTicks&& static_cast<double>(zone.endTicks)> beginTicks;
        }

    }  // namespace

    double TimelineView::MinSpan() const {
        return eastl::max(1.0, std::ceil(kMinSpanMicroseconds * kNanosecondsPerMicrosecond / _nanosecondsPerTick));
    }

    void TimelineView::SetExtent(uint64_t beginTicks, uint64_t endTicks, double nanosecondsPerTick) {
        _extentBegin = static_cast<double>(beginTicks);
        _extentEnd = static_cast<double>(eastl::max(beginTicks, endTicks));
        _nanosecondsPerTick = nanosecondsPerTick > 0.0 ? nanosecondsPerTick : 1.0;
        ClampToExtent();
    }

    void TimelineView::ClampToExtent() {
        const double extentSpan = _extentEnd - _extentBegin;
        if (extentSpan <= 0.0) {
            _span = eastl::max(_span, MinSpan());
            return;
        }
        _span = eastl::clamp(_span, MinSpan(), eastl::max(extentSpan, MinSpan()));
        _begin = eastl::clamp(_begin, _extentBegin, eastl::max(_extentBegin, _extentEnd - _span));
    }

    void TimelineView::FitTo(uint64_t beginTicks, uint64_t endTicks) {
        _begin = static_cast<double>(beginTicks);
        _span = eastl::max(static_cast<double>(eastl::max(beginTicks, endTicks) - beginTicks), MinSpan());
    }

    void TimelineView::ZoomAt(double anchorTick, double factor) {
        const double fraction = (anchorTick - _begin) / _span;
        _span *= factor;
        _begin = anchorTick - fraction * _span;
        ClampToExtent();
    }

    void TimelineView::PanByTicks(double deltaTicks) {
        _begin += deltaTicks;
        ClampToExtent();
    }

    float TimelineView::TickToX(double tick, float widthPixels) const {
        return static_cast<float>((tick - _begin) / _span * static_cast<double>(widthPixels));
    }

    double TimelineView::XToTick(float x, float widthPixels) const {
        return _begin + static_cast<double>(x) / static_cast<double>(widthPixels) * _span;
    }

    double TimelineView::PixelsPerTick(float widthPixels) const {
        return static_cast<double>(widthPixels) / _span;
    }

    TickRange SnapshotExtent(const RingSnapshot& snapshot) {
        if (!snapshot.frames.empty()) {
            return {snapshot.frames.front().beginTicks, snapshot.frames.back().endTicks};
        }
        TickRange range;
        bool any = false;
        for (const RingThread& thread : snapshot.threads) {
            for (const ZoneRecord& zone : thread.zones) {
                range.begin = any ? eastl::min(range.begin, zone.beginTicks) : zone.beginTicks;
                range.end = any ? eastl::max(range.end, zone.endTicks) : zone.endTicks;
                any = true;
            }
        }
        return range;
    }

    eastl::vector<TimelineLane> LayoutLanes(const RingSnapshot& snapshot, double beginTicks, double endTicks) {
        eastl::vector<TimelineLane> lanes;
        for (size_t i = 0; i < snapshot.threads.size(); ++i) {
            uint32_t deepest = 0;
            bool any = false;
            for (const ZoneRecord& zone : snapshot.threads[i].zones) {
                if (Overlaps(zone, beginTicks, endTicks)) {
                    deepest = eastl::max(deepest, zone.depth);
                    any = true;
                }
            }
            if (any) {
                lanes.push_back({i, deepest + 1});
            }
        }
        eastl::sort(lanes.begin(), lanes.end(), [&snapshot](const TimelineLane& a, const TimelineLane& b) {
            const RingThread& left = snapshot.threads[a.threadIndex];
            const RingThread& right = snapshot.threads[b.threadIndex];
            return left.sortIndex != right.sortIndex ? left.sortIndex < right.sortIndex : left.threadId < right.threadId;
        });
        static_assert(Profiler::kNoSortIndex == 0xFFFFFFFFu, "threads without a sort index sort last by the plain comparison above");
        return lanes;
    }

    float LaneTop(const eastl::vector<TimelineLane>& lanes, size_t laneIndex, float rowHeight, float laneGap) {
        float top = 0.0f;
        for (size_t i = 0; i < laneIndex && i < lanes.size(); ++i) {
            top += static_cast<float>(lanes[i].rows) * rowHeight + laneGap;
        }
        return top;
    }

    bool IsVisible(const ZoneRecord& zone, double pixelsPerTick) {
        return static_cast<double>(zone.endTicks - zone.beginTicks) * pixelsPerTick >= kOnePixel;
    }

    std::optional<ZoneHit> HitTest(const eastl::vector<TimelineLane>& lanes, const RingSnapshot& snapshot, const TimelineView& view, float x, float y, float rowHeight, float widthPixels,
                                   float laneGap) {
        if (y < 0.0f || rowHeight <= 0.0f || widthPixels <= 0.0f) {
            return std::nullopt;
        }
        for (size_t laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
            const TimelineLane& lane = lanes[laneIndex];
            const float top = LaneTop(lanes, laneIndex, rowHeight, laneGap);
            const float height = static_cast<float>(lane.rows) * rowHeight;
            if (y >= top + height) {
                continue;
            }
            if (y < top) {
                return std::nullopt;  // in the gap above this lane
            }
            const auto row = static_cast<uint32_t>((y - top) / rowHeight);
            const double tick = view.XToTick(x, widthPixels);
            const double pixelsPerTick = view.PixelsPerTick(widthPixels);
            const eastl::vector<ZoneRecord>& zones = snapshot.threads[lane.threadIndex].zones;
            for (size_t zoneIndex = 0; zoneIndex < zones.size(); ++zoneIndex) {
                const ZoneRecord& zone = zones[zoneIndex];
                if (zone.depth == row && static_cast<double>(zone.beginTicks) <= tick && tick <= static_cast<double>(zone.endTicks) && IsVisible(zone, pixelsPerTick)) {
                    return ZoneHit{lane.threadIndex, zoneIndex};
                }
            }
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<ZoneHit> FindWorstCall(const RingSnapshot& snapshot, const ProfileSite* site) {
        std::optional<ZoneHit> worst;
        uint64_t worstTicks = 0;
        for (size_t t = 0; t < snapshot.threads.size(); ++t) {
            const eastl::vector<ZoneRecord>& zones = snapshot.threads[t].zones;
            for (size_t z = 0; z < zones.size(); ++z) {
                const uint64_t ticks = zones[z].endTicks - zones[z].beginTicks;
                if (zones[z].site == site && (!worst || ticks > worstTicks)) {
                    worst = ZoneHit{t, z};
                    worstTicks = ticks;
                }
            }
        }
        return worst;
    }

    const RingFrame* FrameAt(const RingSnapshot& snapshot, uint64_t tick) {
        for (const RingFrame& frame : snapshot.frames) {
            if (tick >= frame.beginTicks && tick < frame.endTicks) {
                return &frame;
            }
        }
        return nullptr;
    }

}  // namespace BECore
