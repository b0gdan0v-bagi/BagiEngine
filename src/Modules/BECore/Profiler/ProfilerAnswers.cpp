#include "ProfilerAnswers.h"

#include <Generated/ProfilerAnswers.gen.hpp>

namespace BECore {

    namespace {

        /// A profiler name is a literal that may be missing
        String TextOf(const char* text) {
            return text != nullptr ? String(text) : String();
        }

    }  // namespace

    ProfilerAllocTotals ProfilerAllocTotals::Read(const AllocTotals& totals) {
        ProfilerAllocTotals answer;
        answer.bytes = totals.bytes;
        answer.count = totals.count;
        answer.frees = totals.frees;
        answer.largest = totals.largest;
        answer.liveBytes = totals.liveBytes;
        answer.noAllocViolations = totals.noAllocViolations;
        answer.peakLiveBytes = totals.peakLiveBytes;
        return answer;
    }

    ProfilerAllocName ProfilerAllocName::Read(const AllocSiteStats& stats) {
        ProfilerAllocName answer;
        answer.allocs = stats.count;
        answer.bytes = stats.bytes;
        answer.name = TextOf(stats.name);
        answer.zone = TextOf(stats.ZoneLabel());
        return answer;
    }

    ProfilerAllocSizes ProfilerAllocSizes::Read(const AllocSizeStats& stats) {
        ProfilerAllocSizes answer;
        answer.allocs = stats.allocs;
        for (uint32_t bucket = 0; bucket < kSizeBuckets; ++bucket) {
            if (stats.buckets[bucket] != 0) {
                ProfilerAllocSizeBucket row;
                row.allocs = stats.buckets[bucket];
                row.upTo = SizeBucketBound(bucket);
                answer.buckets.push_back(row);
            }
        }
        answer.zone = TextOf(stats.ZoneLabel());
        return answer;
    }

    ProfilerAllocZone ProfilerAllocZone::Read(const ZoneAllocStats& stats) {
        ProfilerAllocZone answer;
        answer.allocs = stats.count;
        answer.bytes = stats.bytes;
        answer.calls = stats.calls;
        answer.file = TextOf(stats.site->file);
        answer.line = stats.site->line;
        answer.name = TextOf(stats.site->name);
        return answer;
    }

    ProfilerArenaRow ProfilerArenaRow::Read(const ArenaStatsEntry& entry) {
        ProfilerArenaRow row;
        row.capacity = entry.capacity;
        row.chunks = entry.chunks;
        row.name = PoolString::Intern(entry.name);
        row.overflows = entry.overflows;
        row.peak = entry.peak;
        row.used = entry.used;
        return row;
    }

    ProfilerPoolRow ProfilerPoolRow::Read(const PoolStatsEntry& entry) {
        ProfilerPoolRow row;
        row.allocations = entry.allocations;
        row.blockSize = entry.blockSize;
        row.live = entry.live;
        row.name = PoolString::Intern(entry.name);
        row.pages = entry.pages;
        row.peak = entry.peak;
        return row;
    }

    ProfilerTagRow ProfilerTagRow::Read(const AllocTagTotals& totals, MemTag tag) {
        ProfilerTagRow row;
        row.count = totals.count;
        row.frees = totals.frees;
        row.liveBytes = totals.liveBytes;
        row.peakLiveBytes = totals.peakLiveBytes;
        row.tag = tag;
        return row;
    }

}  // namespace BECore
