#include "PoolStringStorage.h"

namespace BECore::Details {

    namespace {

        std::atomic<uint64_t> g_nextStorageId{1};

    }  // namespace

    PoolStringStorage::PoolStringStorage(IPoolBlockSource& source, size_t initialTableCapacity)
        : _source(source), _id(g_nextStorageId.fetch_add(1, std::memory_order_relaxed)), _table(initialTableCapacity) {}

    PoolHitCounter& PoolStringStorage::RegisterThread() {
        const void* const owner = &t_counterCache;
        LockGuard lock(_countersMutex);
        PoolHitCounter* counter = nullptr;
        for (const UniquePtr<PoolHitCounter>& existing : _hitCounters) {
            if (existing->owner == owner) {
                counter = existing.get();
                break;
            }
        }
        if (counter == nullptr) {
            _hitCounters.push_back(MakeUnique<PoolHitCounter>());
            counter = _hitCounters.back().get();
            counter->owner = owner;
        }
        t_counterCache = {_id, counter};
        return *counter;
    }

    char* PoolStringStorage::TakeBlock(size_t bytes) {
        PoolMemory memory = _source.Allocate(bytes);
        if (memory.data == nullptr) {
            if (_fallback == nullptr) {
                _fallback = MakeHeapBlockSource();
            }
            memory = _fallback->Allocate(bytes);
            ++_heapFallbackBlocks;
        }
        ++_runtime.blocks;
        _runtime.blockBytes += bytes;
        return static_cast<char*>(memory.data);
    }

    char* PoolStringStorage::AllocateEntryMemory(size_t bytes) {
        if (_offset + bytes <= _capacity) {
            char* memory = _current + _offset;
            _offset += bytes;
            return memory;
        }
        if (bytes > PoolString::kBlockBytes) {
            return TakeBlock(bytes);
        }
        _current = TakeBlock(PoolString::kBlockBytes);
        _capacity = PoolString::kBlockBytes;
        _offset = bytes;
        return _current;
    }

    Optional<PoolString> PoolStringStorage::Find(StringView str) {
        if (str.empty()) {
            return PoolString();
        }
        const uint64_t hash = StringUtils::GetHash(str);
        SharedLock lock(_mutex);
        if (const Entry* entry = _table.Find(hash, str)) {
            return PoolString(entry);
        }
        return std::nullopt;
    }

    PoolString PoolStringStorage::InternMiss(StringView str, uint64_t hash) {
        UniqueLock lock(_mutex);
        ++_writeLocks;

        // Double-check: a thread that lost the race for this text counts a hit, so one text is one miss
        if (const Entry* entry = _table.Find(hash, str)) {
            CountHit();
            return PoolString(entry);
        }

        // The entry stores the length in 32 bits: a longer string would be copied past its block
        ASSERT(str.size() <= kPoolEntryMaxTextBytes);
        const size_t bytes = PoolEntryBytes(str.size());
        const Entry* entry = ConstructPoolEntry(AllocateEntryMemory(bytes), hash, str);

        _table.Insert(hash, entry);
        ++_runtime.strings;
        _runtime.bytes += bytes;
        ++_misses;
        return PoolString(entry);
    }

    PoolStringStats PoolStringStorage::GetStats() {
        PoolStringStats stats;
        {
            SharedLock lock(_mutex);
            stats.stringCount = _table.Used();
            stats.stringBytes = _runtime.bytes;
            stats.blockCount = _runtime.blocks;
            stats.blockBytes = _runtime.blockBytes;
            stats.tableCapacity = _table.Capacity();
            stats.tableUsed = _table.Used();
            stats.longestProbe = _table.LongestProbe();
            stats.misses = _misses;
            stats.runtime = _runtime;
            stats.writeLocks = _writeLocks;
            stats.heapFallbackBlocks = _heapFallbackBlocks;
        }
        {
            LockGuard lock(_countersMutex);
            for (const UniquePtr<PoolHitCounter>& counter : _hitCounters) {
                stats.hits += counter->hits.load(std::memory_order_relaxed);
            }
        }
        stats.internCalls = stats.hits + stats.misses;
        return stats;
    }

}  // namespace BECore::Details
