#pragma once

#include <BECore/Memory/BlockPool.h>
#include <BECore/Threading/Mutex.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /**
     * @brief A BlockPool that any thread may use, for objects made on one thread and freed on another.
     *
     * - **Shape.** One central BlockPool under a Mutex, and in front of it a small free list per thread and pool. A hit takes no lock and no atomic
     *   read-modify-write besides the counters; the central lock is taken once per batch (`kCacheBatch` blocks) when a list runs dry or grows past
     *   `kCacheCapacity`. A block freed by another thread than the one that made it ends up in the central list at the next spill, so the pages do
     *   not grow in a producer-consumer pattern.
     * - **Thread end.** A thread's lists go back to their pools when it ends. `DrainThreadCache()` does it by hand, for a thread that is about to idle.
     * - **Lifetime.** A pool may die while another thread still keeps blocks of it: such a list is recognised by the pool's id (never reused) and dropped
     *   without touching the pool or the blocks (their pages went with the pool). A new pool at the same address is not mistaken for the dead one.
     *   Only the pool's destructor and a spill at thread end or on eviction take the global lifetime lock, never the hot path.
     * - **Counters.** GetLiveCount() is exact when no other thread allocates or frees during the call: every thread counts on a shard of its own (one
     *   atomic add on a line nobody else writes; a counter all threads add to made four threads much slower than `new`). The PoolStats slot, claimed on
     *   the first Allocate so the pool can be `constinit`, is **published** from the shards at every refill and spill, every `kPublishEvery` calls that
     *   hit the list, and by DrainThreadCache() and a thread's end: what a reader of the registry sees lags by up to that many calls per thread, and
     *   the peak is the largest published `live`. A block in a thread's list is not live.
     * - Debug asserts a pointer that is no block of this pool, a double free and a write after free, as BlockPool does (the first takes the central lock
     *   in every Deallocate: Debug only). A pool that dies with live blocks is a bug (Debug asserts).
     */
    class ConcurrentBlockPool {
    public:
        static constexpr size_t kCacheBatch = 16;
        static constexpr size_t kCacheCapacity = 2 * kCacheBatch;
        /// Pools one thread keeps a list for; a thread that uses more spills the least recently used list
        static constexpr size_t kCachedPools = 8;
        static constexpr uint32_t kPublishEvery = 128;

        /// @param name a string literal: the counters registry keeps the pointer; see BlockPool for the rest
        constexpr ConcurrentBlockPool(const char* name, size_t blockSize, size_t alignment, size_t blocksPerPage = 0) : _name(name), _central(name, blockSize, alignment, blocksPerPage, false) {}

        ~ConcurrentBlockPool();

        ConcurrentBlockPool(const ConcurrentBlockPool&) = delete;
        ConcurrentBlockPool& operator=(const ConcurrentBlockPool&) = delete;

        /// A block of GetBlockSize() bytes aligned as asked. Never null; the contents are not initialised
        [[nodiscard]] void* Allocate();

        /// Gives a block of this pool back, from any thread; a null pointer does nothing
        void Deallocate(void* ptr);

        /// Blocks handed out and not given back, on every thread
        [[nodiscard]] size_t GetLiveCount() const;
        /// Takes the central lock
        [[nodiscard]] size_t GetPageCount() const;
        [[nodiscard]] size_t GetBlockSize() const {
            return _central.GetBlockSize();
        }

        /// Gives the free blocks the calling thread keeps, of every pool, back to their pools
        static void DrainThreadCache();

    private:
        struct CacheEntry;
        struct ThreadCaches;
        struct Registry;

        static ThreadCaches* Caches();
        static void SpillEntry(CacheEntry& entry);

        void OnAllocated(uint32_t shard);
        void OnFreed(uint32_t shard);
        /// Writes the shards' sums into the PoolStats slot
        void Publish();
        void SumShards(uint64_t& allocations, uint64_t& frees) const;
        void EnsureRegistered();
        [[nodiscard]] void* AllocateSlow();
        void DeallocateSlow(void* ptr);
        void SpillBatch(CacheEntry& entry, size_t count);
        void ReportPages();

        const char* _name;
        BlockPool _central;
        mutable Mutex _lock{BE_LOCK_SITE("ConcurrentBlockPool central")};
        // Zero until the first use; assigned once under the lifetime lock, from a counter that never repeats
        std::atomic<uint64_t> _id{0};
        // One line per shard, so the counters of two threads do not share one
        static constexpr size_t kShards = 8;
        static constexpr size_t kShardAlign = 128;
        struct alignas(kShardAlign) Shard {
            std::atomic<uint64_t> allocations{0};
            std::atomic<uint64_t> frees{0};
        };
        Shard _shards[kShards];
        PoolStatsSlot _stats;
        size_t _pagesReported = 0;  // under _lock
        ConcurrentBlockPool* _registryPrev = nullptr;
        ConcurrentBlockPool* _registryNext = nullptr;
    };

}  // namespace BECore
