#include "ConcurrentBlockPool.h"

#include <BECore/Memory/Immortal.h>

namespace BECore {

    namespace {

        /// Never destroyed: a pool is destroyed by a static destructor too (a type pool is immortal, a test's is not), and its destructor takes this lock
        constinit Immortal<Mutex> g_lifetime(BE_LOCK_SITE("ConcurrentBlockPool lifetime"));
        // Both under g_lifetime
        constinit ConcurrentBlockPool* g_registryHead = nullptr;
        constinit uint64_t g_lastPoolId = 0;

        /// True from the start of the calling thread's cache teardown: a later Allocate or Deallocate (another thread-local's destructor) goes to the central list
        constinit thread_local bool t_cachesEnded = false;

    }  // namespace

    struct ConcurrentBlockPool::CacheEntry {
        ConcurrentBlockPool* pool = nullptr;
        uint64_t poolId = 0;
        void* head = nullptr;
        size_t count = 0;
        uint64_t lastUse = 0;
        uint32_t sincePublish = 0;
    };

    struct ConcurrentBlockPool::ThreadCaches {
        Array<CacheEntry, kCachedPools> entries;
        uint64_t tick = 0;
        uint32_t shard = kUnassigned;

        static constexpr uint32_t kUnassigned = UINT32_MAX;

        /// The shard of this thread's counters, given out round robin on the first call
        uint32_t ShardIndex() {
            if (shard == kUnassigned) {
                static std::atomic<uint32_t> next{0};
                shard = next.fetch_add(1, std::memory_order_relaxed) % kShards;
            }
            return shard;
        }

        ~ThreadCaches() {
            t_cachesEnded = true;
            for (CacheEntry& entry : entries) {
                SpillEntry(entry);
            }
        }

        /// The list of @p pool, or null; a hit is the most recently used
        CacheEntry* Find(const ConcurrentBlockPool* pool, uint64_t poolId) {
            for (CacheEntry& entry : entries) {
                if (entry.pool == pool && entry.poolId == poolId) {
                    entry.lastUse = ++tick;
                    return &entry;
                }
            }
            return nullptr;
        }

        /// The list of @p pool, made when there is none: a list left by a dead pool at the same address is dropped, else an empty slot is taken, else the least recently used list is spilled
        CacheEntry& Claim(ConcurrentBlockPool* pool, uint64_t poolId) {
            if (CacheEntry* found = Find(pool, poolId)) {
                return *found;
            }
            CacheEntry* victim = nullptr;
            for (CacheEntry& entry : entries) {
                if (entry.pool == pool) {
                    victim = &entry;
                    break;
                }
            }
            if (victim == nullptr) {
                victim = &entries.front();
                for (CacheEntry& entry : entries) {
                    // An empty entry has lastUse 0, so it is the least recently used
                    if (entry.lastUse < victim->lastUse) {
                        victim = &entry;
                    }
                }
                SpillEntry(*victim);
            }
            *victim = CacheEntry{pool, poolId, nullptr, 0, ++tick};
            return *victim;
        }
    };

    ConcurrentBlockPool::ThreadCaches* ConcurrentBlockPool::Caches() {
        constinit static thread_local ThreadCaches caches;
        return t_cachesEnded ? nullptr : &caches;
    }

    /// Gives the blocks of @p entry back to its pool when that pool is still alive, and empties the entry either way
    void ConcurrentBlockPool::SpillEntry(CacheEntry& entry) {
        if (entry.pool != nullptr) {
            LockGuard lifetime(g_lifetime.value);
            // Only a pool on the list is dereferenced: an address with another id, or none, is a pool that died
            for (ConcurrentBlockPool* pool = g_registryHead; pool != nullptr; pool = pool->_registryNext) {
                if (pool == entry.pool && pool->_id.load(std::memory_order_relaxed) == entry.poolId) {
                    pool->SpillBatch(entry, entry.count);
                    pool->Publish();
                    break;
                }
            }
        }
        entry = CacheEntry{};
    }

    ConcurrentBlockPool::~ConcurrentBlockPool() {
        {
            LockGuard lifetime(g_lifetime.value);
            if (_id.load(std::memory_order_relaxed) != 0) {
                if (_registryPrev != nullptr) {
                    _registryPrev->_registryNext = _registryNext;
                } else {
                    g_registryHead = _registryNext;
                }
                if (_registryNext != nullptr) {
                    _registryNext->_registryPrev = _registryPrev;
                }
            }
        }
        ASSERT(GetLiveCount() == 0, "a pool died with live blocks");
        _stats.Release();
        // Blocks that other threads still keep are gone with the pages
        _central.DiscardLive();
    }

    void* ConcurrentBlockPool::Allocate() {
        if (ThreadCaches* caches = Caches()) {
            CacheEntry* entry = caches->Find(this, _id.load(std::memory_order_relaxed));
            if (entry != nullptr && entry->count > 0) {
                --entry->count;
                void* block = _central.PopFrom(entry->head);
                OnAllocated(caches->ShardIndex());
                if (++entry->sincePublish >= kPublishEvery) {
                    entry->sincePublish = 0;
                    Publish();
                }
                return block;
            }
        }
        return AllocateSlow();
    }

    void ConcurrentBlockPool::Deallocate(void* ptr) {
        if (ptr == nullptr) {
            return;
        }
#if ENGINE_ENABLE_ASSERTS
        {
            LockGuard lock(_lock);
            const bool ours = _central.IsBlock(ptr);
            ASSERT(ours, "the pointer is no block of this pool (foreign, or not on a block's start)");
            if (!ours) {
                return;
            }
        }
#endif
        if (ThreadCaches* caches = Caches()) {
            CacheEntry* entry = caches->Find(this, _id.load(std::memory_order_relaxed));
            if (entry != nullptr && entry->count < kCacheCapacity) {
                if (_central.PushTo(entry->head, ptr)) {
                    ++entry->count;
                    OnFreed(caches->ShardIndex());
                    if (++entry->sincePublish >= kPublishEvery) {
                        entry->sincePublish = 0;
                        Publish();
                    }
                }
                return;
            }
        }
        DeallocateSlow(ptr);
    }

    size_t ConcurrentBlockPool::GetPageCount() const {
        LockGuard lock(_lock);
        return _central.GetPageCount();
    }

    void ConcurrentBlockPool::DrainThreadCache() {
        if (ThreadCaches* caches = Caches()) {
            for (CacheEntry& entry : caches->entries) {
                SpillEntry(entry);
            }
        }
    }

    void ConcurrentBlockPool::SumShards(uint64_t& allocations, uint64_t& frees) const {
        // A snapshot without ordering: the loads are relaxed, so a free can be summed before the allocation it undoes and `frees` can exceed `allocations`
        frees = 0;
        for (const Shard& shard : _shards) {
            frees += shard.frees.load(std::memory_order_relaxed);
        }
        allocations = 0;
        for (const Shard& shard : _shards) {
            allocations += shard.allocations.load(std::memory_order_relaxed);
        }
    }

    size_t ConcurrentBlockPool::GetLiveCount() const {
        uint64_t allocations = 0;
        uint64_t frees = 0;
        SumShards(allocations, frees);
        return frees > allocations ? 0 : static_cast<size_t>(allocations - frees);
    }

    void ConcurrentBlockPool::OnAllocated(uint32_t shard) {
        _shards[shard].allocations.fetch_add(1, std::memory_order_relaxed);
    }

    void ConcurrentBlockPool::OnFreed(uint32_t shard) {
        _shards[shard].frees.fetch_add(1, std::memory_order_relaxed);
    }

    void ConcurrentBlockPool::Publish() {
        uint64_t allocations = 0;
        uint64_t frees = 0;
        SumShards(allocations, frees);
        if (frees > allocations) {
            return;  // a torn snapshot (see SumShards) is not published: a wrapped count would stay in the peak for good
        }
        _stats.SetCounts(allocations - frees, allocations);
    }

    void ConcurrentBlockPool::EnsureRegistered() {
        if (_id.load(std::memory_order_acquire) != 0) {
            return;
        }
        LockGuard lifetime(g_lifetime.value);
        if (_id.load(std::memory_order_relaxed) != 0) {
            return;
        }
        _stats = PoolStatsSlot::Claim(_name, _central.GetBlockSize(), true);
        _registryNext = g_registryHead;
        if (g_registryHead != nullptr) {
            g_registryHead->_registryPrev = this;
        }
        g_registryHead = this;
        // The id is published last: a thread that sees it sees the slot too
        _id.store(++g_lastPoolId, std::memory_order_release);
    }

    void* ConcurrentBlockPool::AllocateSlow() {
        EnsureRegistered();
        void* block = nullptr;
        ThreadCaches* caches = Caches();
        CacheEntry* entry = caches != nullptr ? &caches->Claim(this, _id.load(std::memory_order_acquire)) : nullptr;
        {
            LockGuard lock(_lock);
            block = _central.Allocate();
            if (entry != nullptr) {
                // One batch for the next calls; blocks go on the thread's list in the free state, as BlockPool keeps its own
                while (entry->count < kCacheBatch) {
                    if (!_central.PushTo(entry->head, _central.Allocate())) {
                        break;
                    }
                    ++entry->count;
                }
            }
            ReportPages();
        }
        OnAllocated(caches != nullptr ? caches->ShardIndex() : 0);
        Publish();
        return block;
    }

    void ConcurrentBlockPool::DeallocateSlow(void* ptr) {
        EnsureRegistered();
        ThreadCaches* caches = Caches();
        if (caches == nullptr) {
            LockGuard lock(_lock);
            _central.Deallocate(ptr);
        } else {
            CacheEntry& entry = caches->Claim(this, _id.load(std::memory_order_acquire));
            if (entry.count >= kCacheCapacity) {
                SpillBatch(entry, kCacheBatch);
            }
            if (!_central.PushTo(entry.head, ptr)) {
                return;
            }
            ++entry.count;
        }
        OnFreed(caches != nullptr ? caches->ShardIndex() : 0);
        Publish();
    }

    void ConcurrentBlockPool::SpillBatch(CacheEntry& entry, size_t count) {
        LockGuard lock(_lock);
        for (size_t i = 0; i < count && entry.count > 0; ++i) {
            _central.Deallocate(_central.PopFrom(entry.head));
            --entry.count;
        }
    }

    void ConcurrentBlockPool::ReportPages() {
        while (_pagesReported < _central.GetPageCount()) {
            _stats.OnPage(_central.GetPageBytes());
            ++_pagesReported;
        }
    }

}  // namespace BECore
