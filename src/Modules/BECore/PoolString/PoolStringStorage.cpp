#include "PoolStringStorage.h"

#include <EASTL/sort.h>

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

    char* PoolStringStorage::AllocateEntryMemory(size_t bytes, size_t entryCount) {
        if (_offset + bytes <= _capacity) {
            char* memory = _current + _offset;
            _offset += bytes;
            return memory;
        }
        if (bytes > PoolString::kBlockBytes || entryCount > 1) {
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
        const Entry* entry = ConstructPoolEntry(AllocateEntryMemory(bytes, 1), hash, str);

        _table.Insert(hash, entry);
        ++_runtime.strings;
        _runtime.bytes += bytes;
        ++_misses;
        return PoolString(entry);
    }

    void PoolStringStorage::InternBatch(Span<const StringView> texts, Span<PoolString> out) {
        ASSERT(out.size() == texts.size());
        Vector<uint64_t> hashes(texts.size());
        for (size_t i = 0; i < texts.size(); ++i) {
            hashes[i] = StringUtils::GetHash(texts[i]);
        }
        InternBatch(texts, Span<const uint64_t>(hashes.data(), hashes.size()), out);
    }

    void PoolStringStorage::InternBatch(Span<const StringView> texts, Span<const uint64_t> hashes, Span<PoolString> out) {
        ASSERT(out.size() == texts.size() && hashes.size() == texts.size());
        for (size_t i = 0; i < texts.size(); ++i) {
            ASSERT(hashes[i] == StringUtils::GetHash(texts[i]), "InternBatch: a hash is not the hash of its text");
        }
        if (texts.empty()) {
            return;
        }

        Vector<size_t> missing;  // allocates on the first miss only: a batch of hits allocates nothing
        {
            SharedLock lock(_mutex);
            for (size_t i = 0; i < texts.size(); ++i) {
                if (texts[i].empty()) {
                    out[i] = PoolString();
                } else if (const Entry* entry = _table.Find(hashes[i], texts[i])) {
                    out[i] = PoolString(entry);
                } else {
                    missing.push_back(i);
                }
            }
        }
        const size_t added = missing.empty() ? 0 : InternBatchMisses(texts, hashes, out, missing);

        // Everything that is not a new string is a hit: found, empty, a repeat inside the batch, or inserted by another thread
        PoolHitCounter& counter = CounterOfThread();
        Bump(counter.hits, texts.size() - added);
        Bump(counter.batchCalls, 1);
        Bump(counter.batchStrings, texts.size());
    }

    size_t PoolStringStorage::InternBatchMisses(Span<const StringView> texts, Span<const uint64_t> hashes, Span<PoolString> out, Vector<size_t>& missing) {
        UniqueLock lock(_mutex);
        ++_writeLocks;

        // Another thread may have inserted some of them since the shared pass
        size_t kept = 0;
        for (const size_t index : missing) {
            if (const Entry* entry = _table.Find(hashes[index], texts[index])) {
                out[index] = PoolString(entry);
            } else {
                missing[kept++] = index;
            }
        }
        missing.resize(kept);
        if (missing.empty()) {
            return 0;
        }

        // Equal texts have equal hashes: after sorting by hash only the texts of one hash can repeat. The first position of a
        // text is the one that gets the entry, the later ones copy its result.
        struct Repeat {
            size_t index;
            size_t first;
        };
        eastl::sort(missing.begin(), missing.end(), [&](size_t left, size_t right) { return hashes[left] != hashes[right] ? hashes[left] < hashes[right] : left < right; });
        Vector<size_t> fresh;
        Vector<Repeat> repeats;
        size_t bytes = 0;
        for (size_t groupBegin = 0; groupBegin < missing.size();) {
            size_t groupEnd = groupBegin;
            while (groupEnd < missing.size() && hashes[missing[groupEnd]] == hashes[missing[groupBegin]]) {
                ++groupEnd;
            }
            const size_t freshBefore = fresh.size();
            for (size_t k = groupBegin; k < groupEnd; ++k) {
                const size_t index = missing[k];
                size_t first = index;
                for (size_t f = freshBefore; f < fresh.size(); ++f) {
                    if (texts[fresh[f]] == texts[index]) {
                        first = fresh[f];
                        break;
                    }
                }
                if (first == index) {
                    // The entry stores the length in 32 bits: a longer string would be copied past its block
                    ASSERT(texts[index].size() <= kPoolEntryMaxTextBytes);
                    fresh.push_back(index);
                    bytes += PoolEntryBytes(texts[index].size());
                } else {
                    repeats.push_back({index, first});
                }
            }
            groupBegin = groupEnd;
        }

        _table.Reserve(_table.Used() + fresh.size());
        char* memory = AllocateEntryMemory(bytes, fresh.size());
        for (const size_t index : fresh) {
            const Entry* entry = ConstructPoolEntry(memory, hashes[index], texts[index]);
            memory += PoolEntryBytes(texts[index].size());
            _table.Insert(hashes[index], entry);
            out[index] = PoolString(entry);
        }
        for (const Repeat& repeat : repeats) {
            out[repeat.index] = out[repeat.first];
        }

        _runtime.strings += fresh.size();
        _runtime.bytes += bytes;
        _misses += fresh.size();
        return fresh.size();
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
                stats.batchCalls += counter->batchCalls.load(std::memory_order_relaxed);
                stats.batchStrings += counter->batchStrings.load(std::memory_order_relaxed);
            }
        }
        stats.internCalls = stats.hits + stats.misses;
        return stats;
    }

}  // namespace BECore::Details
