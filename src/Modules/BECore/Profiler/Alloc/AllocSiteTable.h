#pragma once

#include <BECore/Profiler/ProfileSite.h>
#include <BECore/Types/Types.h>
#include <atomic>
#include <cstdint>

namespace BECore {

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

        /// Allocations dropped because a table was full, since the last Reset
        [[nodiscard]] uint64_t GetOverflow() const;

        /// Zeroes the counters and the overflow; the interned pointers stay
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
        std::atomic<uint64_t> _overflow{0};
    };

}  // namespace BECore
