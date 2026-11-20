#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// The counters of one block pool, copied out of the registry
    struct PoolStatsEntry {
        const char* name = "";
        uint64_t blockSize = 0;    // bytes of one block, as the pool strides them
        uint64_t live = 0;         // blocks handed out and not given back
        uint64_t peak = 0;         // the most `live` ever was
        uint64_t pages = 0;        // pages held now
        uint64_t pageBytes = 0;    // bytes of every page held
        uint64_t allocations = 0;  // Allocate() calls, a running total
    };

    /// The counters summed over every pool alive
    struct PoolTotals {
        uint64_t liveBlocks = 0;
        uint64_t liveBytes = 0;  // live blocks times their pool's block size
        uint64_t pages = 0;
        uint64_t pageBytes = 0;
    };

    /**
     * @brief The handle of one block pool's counters in the static registry (Memory/CLAUDE.md, "The registry").
     *
     * The same table scheme as ArenaStatsSlot: a `constinit` table, claim and release without a lock or an allocation, readers copy numbers and never hold a
     * pointer to a pool. The difference is the writer: a pool claimed as single-thread has one writer and its counters are a relaxed load and store; a
     * concurrent pool is written from many threads and its counters are a relaxed `fetch_add`. A default-constructed handle is "none": every call is a
     * no-op, which is also what a pool gets when the table is full.
     */
    class PoolStatsSlot {
    public:
        constexpr PoolStatsSlot() = default;

        /// Claims a free slot for a pool called @p name (a string literal: the registry keeps the pointer); "none" when the table is full
        [[nodiscard]] static PoolStatsSlot Claim(const char* name, uint64_t blockSize, bool concurrent);

        /// Gives the slot back to the table; the handle becomes "none". Safe to call twice
        void Release();

        [[nodiscard]] constexpr bool IsValid() const {
            return _index != kNone;
        }

        /// A block was handed out: one more live, one more allocation; raises the peak
        void OnAllocate();
        /// A block came back
        void OnFree();
        /// A page of @p pageBytes was taken from the heap
        void OnPage(uint64_t pageBytes);

    private:
        static constexpr uint32_t kNone = UINT32_MAX;

        constexpr PoolStatsSlot(uint32_t index, bool concurrent) : _index(index), _concurrent(concurrent) {}

        uint32_t _index = kNone;
        bool _concurrent = false;
    };

    namespace PoolStats {

        /// Pools counted at once; a pool that finds the table full runs uncounted
        constexpr uint32_t kMaxPools = 256;

        /// Pools with a slot now. Tests use it to see that a dead pool left nothing behind
        [[nodiscard]] uint32_t GetLiveCount();

    }  // namespace PoolStats

    /// A snapshot of every pool alive (allocates: never on a frame path)
    [[nodiscard]] Vector<PoolStatsEntry> GetPoolStats();

    /// The sums of the pools alive, without allocating (FrameStats takes them every frame)
    [[nodiscard]] PoolTotals GetPoolTotals();

}  // namespace BECore
