#pragma once

#include <BECore/Memory/ArenaStats.h>
#include <BECore/Memory/MemTag.h>
#include <BECore/Memory/PoolStats.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// `profiler_dump`
    struct ProfilerDumpAnswer {
        BE_CLASS(ProfilerDumpAnswer)

        BE_REFLECT_FIELD uint64_t dropped = 0;
        BE_REFLECT_FIELD String path;
    };

    /// The `totals` of `zone_allocs`
    struct ProfilerAllocTotals {
        BE_CLASS(ProfilerAllocTotals)

        BE_REFLECT_FIELD uint64_t bytes = 0;
        BE_REFLECT_FIELD uint64_t count = 0;
        BE_REFLECT_FIELD uint64_t frees = 0;
        BE_REFLECT_FIELD uint64_t largest = 0;
        BE_REFLECT_FIELD uint64_t liveBytes = 0;
        BE_REFLECT_FIELD uint64_t noAllocViolations = 0;
        BE_REFLECT_FIELD uint64_t peakLiveBytes = 0;

        [[nodiscard]] static ProfilerAllocTotals Read(const AllocTotals& totals);
    };

    /// One zone x allocation name row of `zone_allocs`
    struct ProfilerAllocName {
        BE_CLASS(ProfilerAllocName)

        BE_REFLECT_FIELD uint64_t allocs = 0;
        BE_REFLECT_FIELD uint64_t bytes = 0;
        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD String zone;

        [[nodiscard]] static ProfilerAllocName Read(const AllocSiteStats& stats);
    };

    /// One non-empty bucket of a zone's size histogram in `zone_allocs`: `upTo` is the bucket's largest size in bytes, 0 for the open last bucket
    struct ProfilerAllocSizeBucket {
        BE_CLASS(ProfilerAllocSizeBucket)

        BE_REFLECT_FIELD uint64_t allocs = 0;
        BE_REFLECT_FIELD uint64_t upTo = 0;
    };

    /// One zone's size histogram in `zone_allocs` (Alloc/CLAUDE.md, "Sizes"): the non-empty buckets only
    struct ProfilerAllocSizes {
        BE_CLASS(ProfilerAllocSizes)

        BE_REFLECT_FIELD uint64_t allocs = 0;
        BE_REFLECT_FIELD Vector<ProfilerAllocSizeBucket> buckets;
        BE_REFLECT_FIELD String zone;

        [[nodiscard]] static ProfilerAllocSizes Read(const AllocSizeStats& stats);
    };

    /// One zone row of `zone_allocs`
    struct ProfilerAllocZone {
        BE_CLASS(ProfilerAllocZone)

        BE_REFLECT_FIELD uint64_t allocs = 0;
        BE_REFLECT_FIELD uint64_t bytes = 0;
        BE_REFLECT_FIELD uint64_t calls = 0;
        BE_REFLECT_FIELD String file;
        BE_REFLECT_FIELD uint32_t line = 0;
        BE_REFLECT_FIELD String name;

        [[nodiscard]] static ProfilerAllocZone Read(const ZoneAllocStats& stats);
    };

    /// `zone_allocs`
    struct ZoneAllocsAnswer {
        BE_CLASS(ZoneAllocsAnswer)

        BE_REFLECT_FIELD bool enabled = false;
        BE_REFLECT_FIELD Vector<ProfilerAllocName> names;
        BE_REFLECT_FIELD Vector<ProfilerAllocSizes> sizes;
        BE_REFLECT_FIELD ProfilerAllocTotals totals;
        BE_REFLECT_FIELD Vector<ProfilerAllocZone> zones;
    };

    /// A row of `arenas` in `frame_stats`, written one at a time (`frame_stats` allocates nothing beyond the stats it reads)
    struct ProfilerArenaRow {
        BE_CLASS(ProfilerArenaRow)

        BE_REFLECT_FIELD uint64_t capacity = 0;
        BE_REFLECT_FIELD uint64_t chunks = 0;
        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD uint64_t overflows = 0;
        BE_REFLECT_FIELD uint64_t peak = 0;
        BE_REFLECT_FIELD uint64_t used = 0;

        [[nodiscard]] static ProfilerArenaRow Read(const ArenaStatsEntry& entry);
    };

    /// A row of `pools` in `frame_stats`
    struct ProfilerPoolRow {
        BE_CLASS(ProfilerPoolRow)

        BE_REFLECT_FIELD uint64_t allocations = 0;
        BE_REFLECT_FIELD uint64_t blockSize = 0;
        BE_REFLECT_FIELD uint64_t live = 0;
        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD uint64_t pages = 0;
        BE_REFLECT_FIELD uint64_t peak = 0;

        [[nodiscard]] static ProfilerPoolRow Read(const PoolStatsEntry& entry);
    };

    /// A row of `tags` in `frame_stats`
    struct ProfilerTagRow {
        BE_CLASS(ProfilerTagRow)

        BE_REFLECT_FIELD uint64_t count = 0;
        BE_REFLECT_FIELD uint64_t frees = 0;
        BE_REFLECT_FIELD uint64_t liveBytes = 0;
        BE_REFLECT_FIELD uint64_t peakLiveBytes = 0;
        BE_REFLECT_FIELD MemTag tag = MemTag::Untagged;

        [[nodiscard]] static ProfilerTagRow Read(const AllocTagTotals& totals, MemTag tag);
    };

}  // namespace BECore
