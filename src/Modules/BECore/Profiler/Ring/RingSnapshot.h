#pragma once

#include <BECore/Profiler/ProfileSite.h>

namespace BECore {

    /// One finished zone of a thread as the ring recorded it; allocations are inclusive of the zones inside it
    struct ZoneRecord {
        const ProfileSite* site = nullptr;
        uint64_t beginTicks = 0;
        uint64_t endTicks = 0;
        uint64_t allocCount = 0;
        uint64_t allocBytes = 0;
        uint32_t depth = 0;  // 0: no zone of this thread around it
    };

    /// The zones of one thread, sorted by begin tick (a parent before its children when they begin on the same tick)
    struct RingThread {
        uint32_t threadId = 0;
        eastl::string name;      // from Profiler::GetThreadName when the snapshot was taken, empty when the thread has none
        uint32_t sortIndex = 0;  // Profiler::GetThreadSortIndex, Profiler::kNoSortIndex when none
        eastl::vector<ZoneRecord> zones;
    };

    /// The interval between two frame marks. @c index counts the marks since the ring was made, so a frame keeps its number while older ones fall out
    struct RingFrame {
        uint64_t index = 0;
        uint64_t beginTicks = 0;
        uint64_t endTicks = 0;
    };

    /**
     * @brief A copy of what the ring held at one moment: plain data, no atomics, safe to keep and to hand to the panels.
     *
     * Frame k is the interval between mark k-1 and mark k; a zone belongs to the frame its begin tick falls in (a zone that
     * begins in one frame and ends in the next stays with the first). Zones still running are not in it.
     */
    struct RingSnapshot {
        eastl::vector<RingThread> threads;
        eastl::vector<RingFrame> frames;  // oldest first, consecutive indexes
        double nanosecondsPerTick = 1.0;
        uint64_t dropped = 0;  // zones the ring could not record (too many threads, too deep)

        [[nodiscard]] double TicksToMs(uint64_t ticks) const {
            return static_cast<double>(ticks) * nanosecondsPerTick / kNanosecondsPerMillisecond;
        }

        [[nodiscard]] double DurationMs(const ZoneRecord& zone) const {
            return TicksToMs(zone.endTicks - zone.beginTicks);
        }

        static constexpr double kNanosecondsPerMillisecond = 1.0e6;
    };

}  // namespace BECore
