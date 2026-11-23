#include "ZoneStats.h"

#include <BECore/Memory/ScratchArena.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <bit>
#include <cctype>

namespace BECore {

    namespace {

        constexpr uint64_t kP95Numerator = 95;
        constexpr uint64_t kPercentDenominator = 100;

        /// The index of the p95 value among @p count sorted values (nearest rank, in integers so 20 calls give rank 19 exactly)
        size_t P95Index(size_t count) {
            const uint64_t rank = (kP95Numerator * count + kPercentDenominator - 1) / kPercentDenominator;
            return static_cast<size_t>(rank - 1);
        }

        bool ContainsIgnoreCase(StringView haystack, StringView needle) {
            if (needle.size() > haystack.size()) {
                return false;
            }
            for (size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
                size_t k = 0;
                while (k < needle.size() && std::tolower(static_cast<unsigned char>(haystack[start + k])) == std::tolower(static_cast<unsigned char>(needle[k]))) {
                    ++k;
                }
                if (k == needle.size()) {
                    return true;
                }
            }
            return false;
        }

        double ColumnValue(const ZoneStatRow& row, ZoneStatColumn column) {
            switch (column) {
                case ZoneStatColumn::Calls:
                    return static_cast<double>(row.calls);
                case ZoneStatColumn::Total:
                    return row.totalMs;
                case ZoneStatColumn::Self:
                    return row.selfMs;
                case ZoneStatColumn::Mean:
                    return row.meanMs;
                case ZoneStatColumn::Max:
                    return row.maxMs;
                case ZoneStatColumn::P95:
                    return row.p95Ms;
                case ZoneStatColumn::Allocations:
                    return static_cast<double>(row.allocCount);
                case ZoneStatColumn::Name:
                    break;
            }
            return 0.0;
        }

        constexpr uint32_t kFirstSiteSlots = 256;                    // a power of two
        constexpr uint32_t kSiteAlignmentBits = 3;                   // a ProfileSite is at least 8-aligned, so the low bits of its address never differ
        constexpr uint64_t kHashMultiplier = 0x9E3779B97F4A7C15ULL;  // Fibonacci hashing
        constexpr uint32_t kHashShift = 32;

        struct CallOfRow {
            uint32_t row = 0;
            double durationMs = 0.0;
        };

        /// Open addressing over the scratch arena: the row of a site, found without a heap map. A row index is stored plus one, so zero is empty
        struct SiteSlot {
            const ProfileSite* site = nullptr;
            uint32_t rowPlusOne = 0;
        };

        struct SiteTable {
            SiteSlot* slots = nullptr;
            size_t mask = 0;
        };

        size_t SlotOf(const SiteTable& table, const ProfileSite* site) {
            const uint64_t hash = (static_cast<uint64_t>(std::bit_cast<uintptr_t>(site)) >> kSiteAlignmentBits) * kHashMultiplier;
            size_t slot = static_cast<size_t>(hash >> kHashShift) & table.mask;
            while (table.slots[slot].site != nullptr && table.slots[slot].site != site) {
                slot = (slot + 1) & table.mask;
            }
            return slot;
        }

        /// The row of @p site, added to @p rows when it is new; the table doubles (from @p scratch) when half full
        uint32_t RowOfSite(SiteTable& table, ScratchScope& scratch, Vector<ZoneStatRow>& rows, const ProfileSite* site) {
            const size_t found = SlotOf(table, site);
            if (table.slots[found].site == site) {
                return table.slots[found].rowPlusOne - 1;
            }
            const auto row = static_cast<uint32_t>(rows.size());
            rows.emplace_back().site = site;
            if ((rows.size() * 2) > table.mask + 1) {
                const size_t capacity = (table.mask + 1) * 2;
                table = SiteTable{.slots = scratch.Allocate<SiteSlot>(capacity), .mask = capacity - 1};
                eastl::fill(table.slots, table.slots + capacity, SiteSlot{});
                for (uint32_t i = 0; i < rows.size(); ++i) {
                    table.slots[SlotOf(table, rows[i].site)] = SiteSlot{rows[i].site, i + 1};
                }
            } else {
                table.slots[found] = SiteSlot{site, row + 1};
            }
            return row;
        }

    }  // namespace

    Vector<ZoneStatRow> ComputeZoneStats(const RingSnapshot& snapshot, uint64_t firstFrame, uint64_t lastFrame) {
        Vector<ZoneStatRow> rows;
        ComputeZoneStats(snapshot, firstFrame, lastFrame, rows);
        return rows;
    }

    void ComputeZoneStats(const RingSnapshot& snapshot, uint64_t firstFrame, uint64_t lastFrame, Vector<ZoneStatRow>& rows) {
        rows.clear();

        bool haveWindow = false;
        uint64_t windowBegin = 0;
        uint64_t windowEnd = 0;
        for (const RingFrame& frame : snapshot.frames) {
            if (frame.index < firstFrame || frame.index > lastFrame) {
                continue;
            }
            windowBegin = haveWindow ? eastl::min(windowBegin, frame.beginTicks) : frame.beginTicks;
            windowEnd = haveWindow ? eastl::max(windowEnd, frame.endTicks) : frame.endTicks;
            haveWindow = true;
        }
        if (!haveWindow) {
            return;
        }

        const auto inWindow = [&](const ZoneRecord& zone) { return zone.beginTicks >= windowBegin && zone.beginTicks < windowEnd && zone.site != nullptr; };
        size_t longestThread = 0;
        size_t callCount = 0;
        for (const RingThread& thread : snapshot.threads) {
            longestThread = eastl::max(longestThread, thread.zones.size());
            for (const ZoneRecord& zone : thread.zones) {
                callCount += inWindow(zone) ? 1 : 0;
            }
        }
        if (callCount == 0) {
            return;
        }

        // Every list is sized up front and lives in the scratch arena, so a refill allocates nothing once the arena and `rows` have grown
        ScratchScope scratch;
        auto* childTicks = scratch.Allocate<uint64_t>(longestThread);
        auto* stack = scratch.Allocate<size_t>(longestThread);
        auto* calls = scratch.Allocate<CallOfRow>(callCount);
        SiteTable table{.slots = scratch.Allocate<SiteSlot>(kFirstSiteSlots), .mask = kFirstSiteSlots - 1};
        eastl::fill(table.slots, table.slots + kFirstSiteSlots, SiteSlot{});

        size_t called = 0;
        for (const RingThread& thread : snapshot.threads) {
            const Vector<ZoneRecord>& zones = thread.zones;
            eastl::fill(childTicks, childTicks + zones.size(), uint64_t{0});
            size_t depth = 0;
            for (size_t i = 0; i < zones.size(); ++i) {
                const ZoneRecord& zone = zones[i];
                while (depth > 0 && (zones[stack[depth - 1]].endTicks < zone.beginTicks || zones[stack[depth - 1]].endTicks < zone.endTicks)) {
                    --depth;  // the one on top ends before this one begins or ends: this zone is not inside it
                }
                if (depth > 0) {
                    childTicks[stack[depth - 1]] += zone.endTicks - zone.beginTicks;
                }
                stack[depth++] = i;
            }

            for (size_t i = 0; i < zones.size(); ++i) {
                const ZoneRecord& zone = zones[i];
                if (!inWindow(zone)) {
                    continue;
                }
                const uint32_t rowIndex = RowOfSite(table, scratch, rows, zone.site);
                ZoneStatRow& row = rows[rowIndex];
                const double durationMs = snapshot.DurationMs(zone);
                const uint64_t duration = zone.endTicks - zone.beginTicks;
                const uint64_t selfTicks = duration > childTicks[i] ? duration - childTicks[i] : 0;
                ++row.calls;
                row.totalMs += durationMs;
                row.selfMs += snapshot.TicksToMs(selfTicks);
                row.maxMs = eastl::max(row.maxMs, durationMs);
                row.allocCount += zone.allocCount;
                row.allocBytes += zone.allocBytes;
                calls[called++] = CallOfRow{rowIndex, durationMs};
            }
        }

        // The durations of each row side by side (counting sort by row), then the p95 of each run by selection
        auto* runStart = scratch.Allocate<size_t>(rows.size());
        size_t next = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            runStart[i] = next;
            next += rows[i].calls;
        }
        auto* durations = scratch.Allocate<double>(callCount);
        for (size_t i = 0; i < callCount; ++i) {
            durations[runStart[calls[i].row]++] = calls[i].durationMs;
        }
        for (size_t i = 0; i < rows.size(); ++i) {
            ZoneStatRow& row = rows[i];
            double* end = durations + runStart[i];  // the cursor has moved to the end of the run
            double* begin = end - row.calls;
            row.meanMs = row.totalMs / static_cast<double>(row.calls);
            double* ranked = begin + P95Index(static_cast<size_t>(row.calls));
            eastl::nth_element(begin, ranked, end);
            row.p95Ms = *ranked;
        }
    }

    int CompareZoneStatIdentity(const ZoneStatRow& a, const ZoneStatRow& b) {
        if (const int byName = std::strcmp(a.site->name, b.site->name); byName != 0) {
            return byName;
        }
        if (const int byFile = std::strcmp(a.site->file, b.site->file); byFile != 0) {
            return byFile;
        }
        return a.site->line == b.site->line ? 0 : (a.site->line < b.site->line ? -1 : 1);
    }

    void SortZoneStats(Vector<ZoneStatRow>& rows, ZoneStatColumn column, bool ascending) {
        eastl::sort(rows.begin(), rows.end(), [column, ascending](const ZoneStatRow& a, const ZoneStatRow& b) {
            int order = 0;
            if (column == ZoneStatColumn::Name) {
                order = CompareZoneStatIdentity(a, b);
            } else {
                const double left = ColumnValue(a, column);
                const double right = ColumnValue(b, column);
                order = left < right ? -1 : (left > right ? 1 : 0);
            }
            if (order == 0) {
                return CompareZoneStatIdentity(a, b) < 0;
            }
            return ascending ? order < 0 : order > 0;
        });
    }

    Vector<ZoneStatRow> FilterZoneStats(const Vector<ZoneStatRow>& rows, StringView text) {
        if (text.empty()) {
            return rows;
        }
        Vector<ZoneStatRow> kept;
        for (const ZoneStatRow& row : rows) {
            const String place = Format("{}:{}", row.site->file, row.site->line);
            if (ContainsIgnoreCase(row.site->name, text) || ContainsIgnoreCase(place, text)) {
                kept.push_back(row);
            }
        }
        return kept;
    }

    Array<double, kExclusiveStageCount> ExclusiveStageMs(const FrameStats& frame) {
        const double sceneUpdate = eastl::max(frame.GetStageMs(FrameStage::SceneUpdate), 0.0);
        const double sceneDraw = eastl::max(frame.GetStageMs(FrameStage::SceneDraw), 0.0);
        const double present = eastl::max(frame.GetStageMs(FrameStage::Present), 0.0);
        const double wait = eastl::max(frame.GetStageMs(FrameStage::Wait), 0.0);
        Array<double, kExclusiveStageCount> result{};
        result[+ExclusiveStage::UpdateOnly] = eastl::max(frame.GetStageMs(FrameStage::Update) - sceneUpdate, 0.0);
        result[+ExclusiveStage::SceneUpdateStage] = sceneUpdate;
        result[+ExclusiveStage::RenderOnly] = eastl::max(frame.GetStageMs(FrameStage::Render) - sceneDraw, 0.0);
        result[+ExclusiveStage::SceneDrawStage] = sceneDraw;
        result[+ExclusiveStage::PresentStage] = present;
        result[+ExclusiveStage::OtherStage] = eastl::max(frame.cpuMs - (result[+ExclusiveStage::UpdateOnly] + sceneUpdate + result[+ExclusiveStage::RenderOnly] + sceneDraw + present + wait), 0.0);
        result[+ExclusiveStage::WaitStage] = wait;
        return result;
    }

    double FrameWorkMs(const FrameStats& frame) {
        return eastl::max(frame.cpuMs - eastl::max(frame.GetStageMs(FrameStage::Wait), 0.0), 0.0);
    }

    WorkWaitMean SummarizeWorkWait(const Vector<FrameStats>& history) {
        WorkWaitMean sum;
        size_t counted = 0;
        for (const FrameStats& frame : history) {
            if (frame.paused) {
                continue;
            }
            sum.workMs += FrameWorkMs(frame);
            sum.waitMs += eastl::max(frame.GetStageMs(FrameStage::Wait), 0.0);
            ++counted;
        }
        if (counted == 0) {
            return {};
        }
        sum.workMs /= static_cast<double>(counted);
        sum.waitMs /= static_cast<double>(counted);
        return sum;
    }

}  // namespace BECore
