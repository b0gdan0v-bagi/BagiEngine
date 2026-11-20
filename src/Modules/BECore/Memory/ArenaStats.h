#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// The counters of one arena, copied out of the registry
    struct ArenaStatsEntry {
        const char* name = "";
        uint64_t capacity = 0;   // bytes of every chunk the arena holds
        uint64_t used = 0;       // bytes bumped now, alignment gaps included
        uint64_t peak = 0;       // the most `used` ever was
        uint64_t overflows = 0;  // allocations that did not fit and took a new chunk
        uint64_t chunks = 0;     // chunks held now, a caller's first buffer included
        uint64_t resets = 0;     // Reset() calls
    };

    /// The counters summed over every arena alive
    struct ArenaTotals {
        uint64_t capacity = 0;
        uint64_t used = 0;
        uint64_t peak = 0;
        uint64_t overflows = 0;
    };

    /**
     * @brief The handle of one arena's counters in the static registry (Memory/CLAUDE.md, "The registry").
     *
     * The registry is a fixed table of slots, `constinit` like AllocTracker, so an arena on any thread can claim one without
     * CoreManager, a lock or an allocation. The arena that claimed a slot is its only writer: the Publish and Count functions are
     * a relaxed load and store, never a read-modify-write, and a reader (GetArenaStats) copies the numbers and never holds a pointer
     * to an arena. A default-constructed handle is "none": every call is a no-op, which is also what an arena gets when the table is full.
     */
    class ArenaStatsSlot {
    public:
        constexpr ArenaStatsSlot() = default;

        /// Claims a free slot for an arena called @p name (a string literal: the registry keeps the pointer); "none" when the table is full
        [[nodiscard]] static ArenaStatsSlot Claim(const char* name);

        /// Gives the slot back to the table; the handle becomes "none". Safe to call twice
        void Release();

        [[nodiscard]] constexpr bool IsValid() const {
            return _index != kNone;
        }

        /// The chunks the arena holds changed
        void PublishCapacity(uint64_t capacity, uint64_t chunks);
        /// The bumped bytes changed; raises the peak
        void PublishUsed(uint64_t used);
        void CountOverflow();
        void CountReset();

    private:
        static constexpr uint32_t kNone = UINT32_MAX;

        explicit constexpr ArenaStatsSlot(uint32_t index) : _index(index) {}

        uint32_t _index = kNone;
    };

    namespace ArenaStats {

        /// Arenas counted at once; an arena that finds the table full runs uncounted
        constexpr uint32_t kMaxArenas = 256;

        /// Arenas with a slot now. Tests use it to see that an ended arena left nothing behind
        [[nodiscard]] uint32_t GetLiveCount();

    }  // namespace ArenaStats

    /// A snapshot of every arena alive (allocates: never on a frame path)
    [[nodiscard]] Vector<ArenaStatsEntry> GetArenaStats();

    /// The sums of the arenas alive, without allocating (FrameStats takes them every frame)
    [[nodiscard]] ArenaTotals GetArenaTotals();

}  // namespace BECore
