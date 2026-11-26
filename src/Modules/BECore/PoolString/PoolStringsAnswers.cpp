#include "PoolStringsAnswers.h"

#include <BECore/Profiler/Alloc/AllocSiteTable.h>
#include <Generated/PoolStringsAnswers.gen.hpp>

namespace BECore {

    namespace {

        constexpr size_t kHashDigits = 16;

        /// A zone's name and file are literals of the program image that may be missing
        TextView TextOf(const char* text) {
            return text != nullptr ? TextView(StringView(text)) : TextView();
        }

        TextView KindName(PoolBlockKind kind) {
            switch (kind) {
                case PoolBlockKind::Runtime:
                    return TextView("runtime");
                case PoolBlockKind::Allocated:
                    return TextView("allocated");
                case PoolBlockKind::Adopted:
                    return TextView("adopted");
            }
            return TextView();
        }

        PoolStringsFound ReadRow(const PoolStringSnapshot& snapshot, const PoolStringRow& row) {
            PoolStringsFound found;
            if (row.block != kNoPoolBlock) {
                found.file = TextView(StringView(snapshot.blocks[row.block].source));
            }
            char hash[kHashDigits];
            FormatToN(hash, kHashDigits, "{:016x}", row.text.HashValue());
            found.hash.assign(hash, kHashDigits);
            found.length = row.text.Length();
            found.source = TextView(GetPoolStringOriginName(row.origin));
            found.text = row.text;
            return found;
        }

    }  // namespace

    StringView GetPoolStringOriginName(PoolStringOrigin origin) {
        switch (origin) {
            case PoolStringOrigin::Static:
                return "static";
            case PoolStringOrigin::Runtime:
                return "runtime";
            case PoolStringOrigin::Adopted:
                return "adopted";
        }
        return StringView();
    }

    PoolStringsChecks PoolStringsChecks::Read(const PoolStringSnapshot& snapshot) {
        PoolStringsChecks answer;
        Vector<PoolStringAnalysis::RowPair> pairs;
        PoolStringAnalysis::FindDuplicateTexts(snapshot, pairs);
        answer.duplicateTexts = pairs.size();
        PoolStringAnalysis::FindHashCollisions(snapshot, pairs);
        answer.hashCollisions = pairs.size();
        return answer;
    }

    PoolStringsGrowth PoolStringsGrowth::Read(const PoolStringMonitor& monitor, size_t frameCount) {
        PoolStringsGrowth answer;
        answer.frames.reserve(monitor.RecentCount(frameCount));
        monitor.ForEachRecent(frameCount, [&answer](const PoolGrowthFrame& frame) {
            PoolStringsGrowthFrame row;
            row.frame = frame.frame;
            row.otherStrings = frame.otherStrings;
            row.runtimeBytes = frame.runtimeBytes;
            row.runtimeStrings = frame.runtimeStrings;
            answer.frames.push_back(row);
            answer.bytes += frame.runtimeBytes;
            answer.strings += frame.runtimeStrings;
        });
        return answer;
    }

    PoolStringsRecording PoolStringsRecording::Read(const PoolStringMonitor& monitor) {
        PoolStringsRecording answer;
        Vector<PoolStringRecord> records;
        Vector<PoolZoneGroup> groups;
        monitor.GroupByZone(records, groups);
        answer.dropped = monitor.GetDroppedCount();
        answer.enabled = monitor.IsRecording();
        answer.zones.reserve(groups.size());
        for (const PoolZoneGroup& group : groups) {
            PoolStringsZone& zone = answer.zones.emplace_back();
            zone.bytes = group.bytes;
            if (group.zone != nullptr) {
                zone.file = TextOf(group.zone->file);
                zone.line = group.zone->line;
                zone.zone = TextOf(group.zone->name);
            } else {
                zone.zone = TextOf(AllocSiteStats::kNoZone);
            }
            zone.lastFrame = group.lastFrame;
            zone.strings = group.count;
            const size_t listed = std::min(group.count, PoolStringsZone::kMaxTexts);
            zone.texts.reserve(listed);
            for (size_t i = group.first; i < group.first + listed; ++i) {
                zone.texts.push_back(records[i].text);
            }
        }
        return answer;
    }

    PoolStringsSearch PoolStringsSearch::Read(const PoolStringSnapshot& snapshot, const PoolStringQuery& query, size_t limit) {
        PoolStringsSearch answer;
        Vector<uint32_t> rows;
        PoolStringAnalysis::Search(snapshot, query, rows);
        answer.matched = rows.size();
        const size_t listed = std::min(rows.size(), limit);
        answer.rows.reserve(listed);
        for (size_t i = 0; i < listed; ++i) {
            answer.rows.push_back(ReadRow(snapshot, snapshot.strings[rows[i]]));
        }
        return answer;
    }

    PoolStringsSource PoolStringsSource::Read(const PoolSourceStats& stats) {
        PoolStringsSource answer;
        answer.blockBytes = stats.blockBytes;
        answer.blocks = stats.blocks;
        answer.bytes = stats.bytes;
        answer.strings = stats.strings;
        return answer;
    }

    PoolStringsBlock PoolStringsBlock::Read(const PoolBlockRow& row) {
        PoolStringsBlock answer;
        answer.capacity = row.capacity;
        answer.deadBytes = row.deadBytes;
        answer.deadStrings = row.deadStrings;
        answer.kind = KindName(row.kind);
        answer.source = TextView(StringView(row.source));
        answer.used = row.used;
        return answer;
    }

    PoolStringsSummary PoolStringsSummary::Read(const PoolStringSnapshot& snapshot) {
        const PoolStringStats& stats = snapshot.stats;
        PoolStringsSummary answer;
        answer.adopted = PoolStringsSource::Read(stats.adopted);
        answer.batchCalls = stats.batchCalls;
        answer.batchStrings = stats.batchStrings;
        answer.blockBytes = stats.blockBytes;
        answer.blockCount = stats.blockCount;
        answer.blocks.reserve(snapshot.blocks.size());
        for (const PoolBlockRow& row : snapshot.blocks) {
            answer.blocks.push_back(PoolStringsBlock::Read(row));
        }
        answer.deadBytes = stats.deadBytes;
        answer.deadStrings = stats.deadStrings;
        answer.heapFallbackBlocks = stats.heapFallbackBlocks;
        answer.hits = stats.hits;
        answer.internCalls = stats.internCalls;
        answer.longestProbe = stats.longestProbe;
        answer.misses = stats.misses;
        answer.runtime = PoolStringsSource::Read(stats.runtime);
        answer.staticDuplicates = stats.staticDuplicates;
        answer.staticEntries = PoolStringsSource::Read(stats.staticEntries);
        answer.stringBytes = stats.stringBytes;
        answer.stringCount = stats.stringCount;
        answer.tableCapacity = stats.tableCapacity;
        answer.tableLoad = stats.tableCapacity != 0 ? static_cast<double>(stats.tableUsed) / static_cast<double>(stats.tableCapacity) : 0.0;
        answer.tableUsed = stats.tableUsed;
        answer.writeLocks = stats.writeLocks;
        return answer;
    }

}  // namespace BECore
