#pragma once

#include <BECore/Profiler/ProfileSite.h>
#include <BECore/Types/Types.h>
#include <atomic>
#include <bit>
#include <cstdint>

namespace BECore {

    /// Size buckets of the allocation histogram: bucket k holds the sizes in (2^(k-1), 2^k], bucket 0 holds 0 and 1, and the last one
    /// everything above the bound of its predecessor (16 MiB)
    constexpr uint32_t kSizeBuckets = 26;

    /// The bucket of an allocation of @p bytes
    [[nodiscard]] constexpr uint32_t SizeBucket(uint64_t bytes) {
        if (bytes <= 1) {
            return 0;
        }
        const auto bucket = static_cast<uint32_t>(std::bit_width(bytes - 1));
        return bucket < kSizeBuckets ? bucket : kSizeBuckets - 1;
    }

    /// The largest size in @p bucket in bytes; 0 for the last bucket, which has no upper bound
    [[nodiscard]] constexpr uint64_t SizeBucketBound(uint32_t bucket) {
        return bucket + 1 < kSizeBuckets ? uint64_t{1} << bucket : 0;
    }

    /// The allocations made inside one zone under one name, since the last reset
    struct AllocSiteStats {
        const ProfileSite* zone = nullptr;  // nullptr: made outside every zone
        const char* name = nullptr;         // nullptr: made without a name
        uint64_t count = 0;
        uint64_t bytes = 0;

        /// The label of the zone for a report: its name, or kNoZone for an allocation made outside every zone
        [[nodiscard]] const char* ZoneLabel() const {
            return zone != nullptr ? zone->name : kNoZone;
        }

        static constexpr const char* kNoZone = "(no zone)";
    };

    /// The allocations made directly inside one zone (not its inner zones), by size bucket, since the last reset
    struct AllocSizeStats {
        const ProfileSite* zone = nullptr;  // nullptr: made outside every zone
        uint64_t allocs = 0;                // the sum of buckets
        uint64_t buckets[kSizeBuckets]{};

        /// The label of the zone for a report, as AllocSiteStats::ZoneLabel()
        [[nodiscard]] const char* ZoneLabel() const {
            return zone != nullptr ? zone->name : AllocSiteStats::kNoZone;
        }
    };

    /**
     * @brief Counts allocations per zone x name (Alloc/CLAUDE.md, "Names"): the table behind AllocTracker::GetNameStats().
     *
     * Zones and names are keyed by **pointer**: no hash of the text on the allocation path. A name must therefore be a string literal
     * or live as long as the program (EASTL's `mpName` is one; `set_name` takes a literal). Each pointer is interned once into a
     * small pointer table, and a (zone, name) pair is one packed 32-bit id in a pair table whose slot also holds the counters.
     * Two pointers with the same text are two ids; GetStats() merges them.
     *
     * `constinit`-constructible, fixed storage. Add never allocates, locks or logs; a full table drops the allocation and counts it
     * in GetOverflow(). GetStats allocates (reader side).
     *
     * The same Add also counts the allocation in a size histogram per zone (Alloc/CLAUDE.md, "Sizes"): fixed storage of
     * kZonePointers + 1 rows by kSizeBuckets, the row of a zone being its interned index and row 0 the null zone. It does not depend
     * on the name: an allocation whose name or pair slot is dropped is still counted there, and one whose zone does not fit is not.
     */
    class AllocSiteTable {
    public:
        static constexpr uint32_t kZonePointers = 1024;  // distinct zone sites
        static constexpr uint32_t kNamePointers = 1024;  // distinct name pointers
        static constexpr uint32_t kPairSlots = 4096;     // distinct (zone, name) pairs

        constexpr AllocSiteTable() = default;

        /// Counts one allocation of @p bytes for the pair; a null zone or name is its own key
        void Add(const ProfileSite* zone, const char* name, uint64_t bytes);

        /// The pairs with allocations, rows of equal zone pointer and name text merged, the most bytes first
        [[nodiscard]] Vector<AllocSiteStats> GetStats() const;

        /// The size histogram per zone, rows with allocations only, the most allocations first; allocates
        [[nodiscard]] Vector<AllocSizeStats> GetSizeStats() const;

        /// Allocations dropped because a table was full, since the last Reset
        [[nodiscard]] uint64_t GetOverflow() const;

        /// Zeroes the counters, the histogram and the overflow; the interned pointers stay
        void Reset();

    private:
        struct PairSlot {
            std::atomic<uint32_t> key{0};  // 0: free; else packed id + 1
            std::atomic<uint64_t> count{0};
            std::atomic<uint64_t> bytes{0};
        };

        std::atomic<const void*> _zones[kZonePointers]{};
        std::atomic<const void*> _names[kNamePointers]{};
        PairSlot _pairs[kPairSlots]{};
        std::atomic<uint64_t> _sizes[kZonePointers + 1][kSizeBuckets]{};
        std::atomic<uint64_t> _overflow{0};
    };

}  // namespace BECore
