#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringAnalysis.h>
#include <BECore/PoolString/PoolStringMonitor.h>
#include <BECore/PoolString/PoolStringSnapshot.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>

// The answer of `pool_strings` (CLAUDE.md, "Control method"). Its `TextView`s point into the program image or into the
// snapshot the struct was read from, which therefore has to outlive every write of it.

namespace BECore {

    /// The name of an origin in the control API: the `source` of a search row and the `source` parameter
    [[nodiscard]] StringView GetPoolStringOriginName(PoolStringOrigin origin);

    /// `checks`: how many pairs of rows each check of `PoolStringAnalysis` found
    struct PoolStringsChecks {
        BE_CLASS(PoolStringsChecks)

        BE_REFLECT_FIELD uint64_t duplicateTexts = 0;
        BE_REFLECT_FIELD uint64_t hashCollisions = 0;

        [[nodiscard]] static PoolStringsChecks Read(const PoolStringSnapshot& snapshot);
    };

    struct PoolStringsGrowthFrame {
        BE_CLASS(PoolStringsGrowthFrame)

        BE_REFLECT_FIELD uint64_t frame = 0;
        BE_REFLECT_FIELD uint64_t otherStrings = 0;
        BE_REFLECT_FIELD uint64_t runtimeBytes = 0;
        BE_REFLECT_FIELD uint64_t runtimeStrings = 0;
    };

    /// `growth`: the last frames, the oldest first; `bytes` and `strings` are the sums of their runtime additions
    struct PoolStringsGrowth {
        BE_CLASS(PoolStringsGrowth)

        BE_REFLECT_FIELD uint64_t bytes = 0;
        BE_REFLECT_FIELD ArenaVector<PoolStringsGrowthFrame> frames;
        BE_REFLECT_FIELD uint64_t strings = 0;

        [[nodiscard]] static PoolStringsGrowth Read(const PoolStringMonitor& monitor, size_t frameCount);
    };

    /// The records of one profiler zone
    struct PoolStringsZone {
        BE_CLASS(PoolStringsZone)

        /// How many of its strings a zone lists, the earliest ones
        static constexpr size_t kMaxTexts = 8;

        BE_REFLECT_FIELD uint64_t bytes = 0;
        BE_REFLECT_FIELD TextView file;
        BE_REFLECT_FIELD uint64_t lastFrame = 0;
        BE_REFLECT_FIELD uint32_t line = 0;
        BE_REFLECT_FIELD uint64_t strings = 0;
        BE_REFLECT_FIELD ArenaVector<PoolString> texts;
        BE_REFLECT_FIELD TextView zone;
    };

    /// `recording`: what the monitor recorded, the zone with the most bytes first
    struct PoolStringsRecording {
        BE_CLASS(PoolStringsRecording)

        BE_REFLECT_FIELD uint64_t dropped = 0;
        BE_REFLECT_FIELD bool enabled = false;
        BE_REFLECT_FIELD ArenaVector<PoolStringsZone> zones;

        [[nodiscard]] static PoolStringsRecording Read(const PoolStringMonitor& monitor);
    };

    struct PoolStringsFound {
        BE_CLASS(PoolStringsFound)

        BE_REFLECT_FIELD TextView file;
        BE_REFLECT_FIELD ArenaString hash;  ///< Hex: 64 bits do not fit a JSON double
        BE_REFLECT_FIELD uint64_t length = 0;
        BE_REFLECT_FIELD TextView source;
        BE_REFLECT_FIELD PoolString text;
    };

    /// `search`: `matched` counts every row the query selects, `rows` holds the first of them by text
    struct PoolStringsSearch {
        BE_CLASS(PoolStringsSearch)

        BE_REFLECT_FIELD uint64_t matched = 0;
        BE_REFLECT_FIELD ArenaVector<PoolStringsFound> rows;

        [[nodiscard]] static PoolStringsSearch Read(const PoolStringSnapshot& snapshot, const PoolStringQuery& query, size_t limit);
    };

    struct PoolStringsSource {
        BE_CLASS(PoolStringsSource)

        BE_REFLECT_FIELD uint64_t blockBytes = 0;
        BE_REFLECT_FIELD uint64_t blocks = 0;
        BE_REFLECT_FIELD uint64_t bytes = 0;
        BE_REFLECT_FIELD uint64_t strings = 0;

        [[nodiscard]] static PoolStringsSource Read(const PoolSourceStats& stats);
    };

    struct PoolStringsBlock {
        BE_CLASS(PoolStringsBlock)

        BE_REFLECT_FIELD uint64_t capacity = 0;
        BE_REFLECT_FIELD uint64_t deadBytes = 0;
        BE_REFLECT_FIELD uint64_t deadStrings = 0;
        BE_REFLECT_FIELD TextView kind;
        BE_REFLECT_FIELD TextView source;
        BE_REFLECT_FIELD uint64_t used = 0;

        [[nodiscard]] static PoolStringsBlock Read(const PoolBlockRow& row);
    };

    /// `summary`: the fields of `PoolStringStats`, the load of the table and every block
    struct PoolStringsSummary {
        BE_CLASS(PoolStringsSummary)

        BE_REFLECT_FIELD PoolStringsSource adopted;
        BE_REFLECT_FIELD uint64_t batchCalls = 0;
        BE_REFLECT_FIELD uint64_t batchStrings = 0;
        BE_REFLECT_FIELD uint64_t blockBytes = 0;
        BE_REFLECT_FIELD uint64_t blockCount = 0;
        BE_REFLECT_FIELD ArenaVector<PoolStringsBlock> blocks;
        BE_REFLECT_FIELD uint64_t deadBytes = 0;
        BE_REFLECT_FIELD uint64_t deadStrings = 0;
        BE_REFLECT_FIELD uint64_t heapFallbackBlocks = 0;
        BE_REFLECT_FIELD uint64_t hits = 0;
        BE_REFLECT_FIELD uint64_t internCalls = 0;
        BE_REFLECT_FIELD uint64_t longestProbe = 0;
        BE_REFLECT_FIELD uint64_t misses = 0;
        BE_REFLECT_FIELD PoolStringsSource runtime;
        BE_REFLECT_FIELD uint64_t staticDuplicates = 0;
        BE_REFLECT_FIELD PoolStringsSource staticEntries;
        BE_REFLECT_FIELD uint64_t stringBytes = 0;
        BE_REFLECT_FIELD uint64_t stringCount = 0;
        BE_REFLECT_FIELD uint64_t tableCapacity = 0;
        BE_REFLECT_FIELD double tableLoad = 0.0;
        BE_REFLECT_FIELD uint64_t tableUsed = 0;
        BE_REFLECT_FIELD uint64_t writeLocks = 0;

        [[nodiscard]] static PoolStringsSummary Read(const PoolStringSnapshot& snapshot);
    };

    /// `pool_strings`
    struct PoolStringsAnswer {
        BE_CLASS(PoolStringsAnswer)

        BE_REFLECT_FIELD PoolStringsChecks checks;
        BE_REFLECT_FIELD PoolStringsGrowth growth;
        BE_REFLECT_FIELD PoolStringsRecording recording;
        BE_REFLECT_FIELD PoolStringsSearch search;
        BE_REFLECT_FIELD PoolStringsSummary summary;
    };

}  // namespace BECore
