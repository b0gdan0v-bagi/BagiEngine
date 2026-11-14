#include "ZoneStats.h"

#include <EASTL/sort.h>
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

        /// <0, 0, >0 by name, then file, then line: the order that settles equal values
        int CompareByIdentity(const ZoneStatRow& a, const ZoneStatRow& b) {
            if (const int byName = std::strcmp(a.site->name, b.site->name); byName != 0) {
                return byName;
            }
            if (const int byFile = std::strcmp(a.site->file, b.site->file); byFile != 0) {
                return byFile;
            }
            return a.site->line == b.site->line ? 0 : (a.site->line < b.site->line ? -1 : 1);
        }

    }  // namespace

    eastl::vector<ZoneStatRow> ComputeZoneStats(const RingSnapshot& snapshot, uint64_t firstFrame, uint64_t lastFrame) {
        eastl::vector<ZoneStatRow> rows;

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
            return rows;
        }

        eastl::hash_map<const ProfileSite*, size_t> rowOfSite;
        eastl::vector<eastl::vector<double>> durationsOfRow;
        eastl::vector<uint64_t> childTicks;
        eastl::vector<size_t> stack;

        for (const RingThread& thread : snapshot.threads) {
            const eastl::vector<ZoneRecord>& zones = thread.zones;
            childTicks.assign(zones.size(), 0);
            stack.clear();
            for (size_t i = 0; i < zones.size(); ++i) {
                const ZoneRecord& zone = zones[i];
                while (!stack.empty() && (zones[stack.back()].endTicks < zone.beginTicks || zones[stack.back()].endTicks < zone.endTicks)) {
                    stack.pop_back();  // the one on top ends before this one begins or ends: this zone is not inside it
                }
                if (!stack.empty()) {
                    childTicks[stack.back()] += zone.endTicks - zone.beginTicks;
                }
                stack.push_back(i);
            }

            for (size_t i = 0; i < zones.size(); ++i) {
                const ZoneRecord& zone = zones[i];
                if (zone.beginTicks < windowBegin || zone.beginTicks >= windowEnd || zone.site == nullptr) {
                    continue;
                }
                const auto [it, added] = rowOfSite.try_emplace(zone.site, rows.size());
                if (added) {
                    rows.emplace_back().site = zone.site;
                    durationsOfRow.emplace_back();
                }
                ZoneStatRow& row = rows[it->second];
                const double durationMs = snapshot.DurationMs(zone);
                const uint64_t duration = zone.endTicks - zone.beginTicks;
                const uint64_t selfTicks = duration > childTicks[i] ? duration - childTicks[i] : 0;
                ++row.calls;
                row.totalMs += durationMs;
                row.selfMs += snapshot.TicksToMs(selfTicks);
                row.maxMs = eastl::max(row.maxMs, durationMs);
                row.allocCount += zone.allocCount;
                row.allocBytes += zone.allocBytes;
                durationsOfRow[it->second].push_back(durationMs);
            }
        }

        for (size_t i = 0; i < rows.size(); ++i) {
            eastl::vector<double>& durations = durationsOfRow[i];
            eastl::sort(durations.begin(), durations.end());
            rows[i].meanMs = rows[i].totalMs / static_cast<double>(rows[i].calls);
            rows[i].p95Ms = durations[P95Index(durations.size())];
        }
        return rows;
    }

    void SortZoneStats(eastl::vector<ZoneStatRow>& rows, ZoneStatColumn column, bool ascending) {
        eastl::sort(rows.begin(), rows.end(), [column, ascending](const ZoneStatRow& a, const ZoneStatRow& b) {
            int order = 0;
            if (column == ZoneStatColumn::Name) {
                order = CompareByIdentity(a, b);
            } else {
                const double left = ColumnValue(a, column);
                const double right = ColumnValue(b, column);
                order = left < right ? -1 : (left > right ? 1 : 0);
            }
            if (order == 0) {
                return CompareByIdentity(a, b) < 0;
            }
            return ascending ? order < 0 : order > 0;
        });
    }

    eastl::vector<ZoneStatRow> FilterZoneStats(const eastl::vector<ZoneStatRow>& rows, StringView text) {
        if (text.empty()) {
            return rows;
        }
        eastl::vector<ZoneStatRow> kept;
        for (const ZoneStatRow& row : rows) {
            const eastl::string place = Format("{}:{}", row.site->file, row.site->line);
            if (ContainsIgnoreCase(row.site->name, text) || ContainsIgnoreCase(place, text)) {
                kept.push_back(row);
            }
        }
        return kept;
    }

    eastl::array<double, kExclusiveStageCount> ExclusiveStageMs(const FrameStats& frame) {
        const double sceneUpdate = eastl::max(frame.GetStageMs(FrameStage::SceneUpdate), 0.0);
        const double sceneDraw = eastl::max(frame.GetStageMs(FrameStage::SceneDraw), 0.0);
        const double present = eastl::max(frame.GetStageMs(FrameStage::Present), 0.0);
        eastl::array<double, kExclusiveStageCount> result{};
        result[+ExclusiveStage::UpdateOnly] = eastl::max(frame.GetStageMs(FrameStage::Update) - sceneUpdate, 0.0);
        result[+ExclusiveStage::SceneUpdateStage] = sceneUpdate;
        result[+ExclusiveStage::RenderOnly] = eastl::max(frame.GetStageMs(FrameStage::Render) - sceneDraw, 0.0);
        result[+ExclusiveStage::SceneDrawStage] = sceneDraw;
        result[+ExclusiveStage::PresentStage] = present;
        result[+ExclusiveStage::OtherStage] = eastl::max(frame.cpuMs - (result[+ExclusiveStage::UpdateOnly] + sceneUpdate + result[+ExclusiveStage::RenderOnly] + sceneDraw + present), 0.0);
        return result;
    }

}  // namespace BECore
