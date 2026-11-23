#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Types/Types.h>
#include <algorithm>
#include <ostream>

namespace BECore::Tests {

    /// A size bucket of a zone's own allocations: `upTo` is the bucket's largest size in bytes, 0 for the open last bucket
    struct ZoneReportSize {
        uint64_t upTo = 0;
        uint64_t count = 0;
    };

    /// The allocations of a zone under one allocation name
    struct ZoneReportName {
        String name;
        uint64_t allocs = 0;
        uint64_t bytes = 0;
    };

    /**
     * One zone of the report. `calls`, `allocs` and `bytes` are inclusive of the zones inside (`AllocTracker::GetZoneStats`); `selfAllocs`,
     * `sizes` and `names` count only what the zone allocated itself, outside its inner zones (`GetSizeStats`, `GetNameStats`). The null
     * zone, and a zone the zone view does not list, has no calls: its `allocs` and `bytes` are the sums of its name rows.
     */
    struct ZoneReportRow {
        String zone;
        uint64_t calls = 0;
        uint64_t allocs = 0;
        uint64_t bytes = 0;
        uint64_t selfAllocs = 0;
        Vector<ZoneReportSize> sizes;
        Vector<ZoneReportName> names;
    };

    namespace ZoneReportDetails {

        inline constexpr const char* kUnnamed = "(unnamed)";

        inline ZoneReportRow& RowOf(Vector<ZoneReportRow>& rows, Vector<const ProfileSite*>& sites, const ProfileSite* site) {
            for (size_t i = 0; i < sites.size(); ++i) {
                if (sites[i] == site) {
                    return rows[i];
                }
            }
            sites.push_back(site);
            rows.emplace_back();
            rows.back().zone = site != nullptr ? site->name : AllocSiteStats::kNoZone;
            return rows.back();
        }

    }  // namespace ZoneReportDetails

    /**
     * Joins the three per-zone views of the allocation tracker (`GetZoneStats`, `GetSizeStats`, `GetNameStats`) by zone pointer, the
     * zones with the most bytes first. Empty without `AllocTracker::kEnabled`. It reads and allocates: call it after the measure,
     * never inside a zone that is measured; what it allocates itself lands in the `(no zone)` row.
     */
    inline Vector<ZoneReportRow> CollectZoneReport() {
        Vector<ZoneReportRow> rows;
        if constexpr (!AllocTracker::kEnabled) {
            return rows;
        }
        Vector<const ProfileSite*> sites;
        Vector<const ProfileSite*> listed;
        for (const ZoneAllocStats& stats : AllocTracker::GetZoneStats()) {
            ZoneReportRow& row = ZoneReportDetails::RowOf(rows, sites, stats.site);
            row.calls = stats.calls;
            row.allocs = stats.count;
            row.bytes = stats.bytes;
            listed.push_back(stats.site);
        }
        for (const AllocSizeStats& stats : AllocTracker::GetSizeStats()) {
            ZoneReportRow& row = ZoneReportDetails::RowOf(rows, sites, stats.zone);
            row.selfAllocs = stats.allocs;
            for (uint32_t bucket = 0; bucket < kSizeBuckets; ++bucket) {
                if (stats.buckets[bucket] != 0) {
                    row.sizes.push_back({SizeBucketBound(bucket), stats.buckets[bucket]});
                }
            }
        }
        for (const AllocSiteStats& stats : AllocTracker::GetNameStats()) {
            ZoneReportRow& row = ZoneReportDetails::RowOf(rows, sites, stats.zone);
            row.names.push_back({stats.name != nullptr ? stats.name : ZoneReportDetails::kUnnamed, stats.count, stats.bytes});
        }
        for (size_t i = 0; i < rows.size(); ++i) {
            if (std::find(listed.begin(), listed.end(), sites[i]) == listed.end()) {
                for (const ZoneReportName& name : rows[i].names) {
                    rows[i].allocs += name.allocs;
                    rows[i].bytes += name.bytes;
                }
            }
        }
        std::sort(rows.begin(), rows.end(), [](const ZoneReportRow& a, const ZoneReportRow& b) { return a.bytes != b.bytes ? a.bytes > b.bytes : a.zone < b.zone; });
        return rows;
    }

    /// The row of the zone @p zone in @p rows, or null
    inline const ZoneReportRow* FindZoneRow(const Vector<ZoneReportRow>& rows, StringView zone) {
        for (const ZoneReportRow& row : rows) {
            if (row.zone == zone) {
                return &row;
            }
        }
        return nullptr;
    }

    /**
     * Writes the report of the case @p caseName: per zone one `AssetLoadZone` line (the inclusive figures), an `AssetLoadZoneSizes` line
     * when the zone allocated itself (the bucket's largest size in bytes, `inf` for the open last bucket, then the count) and one
     * `AssetLoadZoneNames` line for each of its first @p topNames allocation names. Without the tracker the one `skipped` line.
     * The zone names are quoted because they hold spaces.
     */
    inline void PrintZoneReport(std::ostream& out, StringView caseName, size_t topNames) {
        const auto write = [&out](const String& line) { out << line.c_str() << "\n"; };
        if constexpr (!AllocTracker::kEnabled) {
            write(Format("AssetLoadZone case={} skipped=\"allocations are counted only with BE_PROFILER_ALLOC\"", caseName));
            return;
        }
        for (const ZoneReportRow& row : CollectZoneReport()) {
            write(Format("AssetLoadZone case={} zone=\"{}\" calls={} allocs={} bytes={} selfAllocs={}", caseName, row.zone, row.calls, row.allocs, row.bytes, row.selfAllocs));
            if (row.selfAllocs != 0) {
                String sizes;
                for (const ZoneReportSize& size : row.sizes) {
                    if (!sizes.empty()) {
                        sizes.append(",");
                    }
                    sizes.append(size.upTo != 0 ? Format("{}:{}", size.upTo, size.count) : Format("inf:{}", size.count));
                }
                write(Format("AssetLoadZoneSizes case={} zone=\"{}\" sizes={}", caseName, row.zone, sizes));
            }
            const size_t shown = std::min(topNames, row.names.size());
            for (size_t i = 0; i < shown; ++i) {
                const ZoneReportName& name = row.names[i];
                write(Format("AssetLoadZoneNames case={} zone=\"{}\" name=\"{}\" allocs={} bytes={}", caseName, row.zone, name.name, name.allocs, name.bytes));
            }
        }
    }

}  // namespace BECore::Tests
