#include "PoolStringStorage.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore::Details {

    namespace {

        std::atomic<uint64_t> g_nextStorageId{1};

        /// Gives each row the block its entry lies in, and with it the origin; a row in no block stays a static one
        void ClassifyRows(PoolStringSnapshot& snapshot) {
            struct Range {
                uintptr_t begin;
                uint32_t block;
            };
            Vector<Range> ranges;
            ranges.reserve(snapshot.blocks.size());
            for (uint32_t i = 0; i < snapshot.blocks.size(); ++i) {
                ranges.push_back({reinterpret_cast<uintptr_t>(snapshot.blocks[i].data), i});
            }
            eastl::sort(ranges.begin(), ranges.end(), [](const Range& left, const Range& right) { return left.begin < right.begin; });

            for (PoolStringRow& row : snapshot.strings) {
                const uintptr_t text = reinterpret_cast<uintptr_t>(row.text.CStr());
                const auto after = eastl::upper_bound(ranges.begin(), ranges.end(), text, [](uintptr_t address, const Range& range) { return address < range.begin; });
                if (after == ranges.begin()) {
                    continue;
                }
                const Range& range = *(after - 1);
                const PoolBlockRow& block = snapshot.blocks[range.block];
                if (text - range.begin < block.capacity) {
                    row.block = range.block;
                    row.origin = block.kind == PoolBlockKind::Runtime ? PoolStringOrigin::Runtime : PoolStringOrigin::Adopted;
                }
            }
        }

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
        return static_cast<char*>(memory.data);
    }

    char* PoolStringStorage::TakeRuntimeBlock(size_t bytes, size_t used) {
        char* memory = TakeBlock(bytes);
        ++_runtime.blocks;
        _runtime.blockBytes += bytes;
        _blocks.push_back({.data = memory, .kind = PoolBlockKind::Runtime, .capacity = bytes, .used = used});
        return memory;
    }

    void PoolStringStorage::StartCurrentBlock(size_t bytes, size_t used) {
        _current = TakeRuntimeBlock(bytes, used);
        _currentBlock = _blocks.size() - 1;
        _capacity = bytes;
        _offset = used;
    }

    char* PoolStringStorage::AllocateEntryMemory(size_t bytes, size_t entryCount) {
        if (_offset + bytes <= _capacity) {
            char* memory = _current + _offset;
            _offset += bytes;
            _blocks[_currentBlock].used = _offset;
            return memory;
        }
        if (bytes > PoolString::kBlockBytes || entryCount > 1) {
            return TakeRuntimeBlock(bytes, bytes);
        }
        StartCurrentBlock(PoolString::kBlockBytes, bytes);
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
        if (_observer != nullptr) {
            _observer->OnRuntimeString(PoolString(entry));
        }
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
        if (_observer != nullptr) {
            for (const size_t index : fresh) {
                _observer->OnRuntimeString(out[index]);
            }
        }
        return fresh.size();
    }

    void PoolStringStorage::RegisterStatic(Span<const PoolStaticRegistration> registrations) {
        Vector<size_t> pending;  // allocates on the first absent text only
        size_t duplicates = 0;
        {
            SharedLock lock(_mutex);
            for (size_t i = 0; i < registrations.size(); ++i) {
                const Entry& entry = *registrations[i].entry;
                ASSERT(entry.hash == StringUtils::GetHash(entry.ToStringView()) && entry.data[entry.size] == '\0', "RegisterStatic: an entry has a wrong hash or no NUL");
                if (entry.size == 0) {
                    *registrations[i].value = PoolString();
                } else if (const Entry* found = _table.Find(entry.hash, entry.ToStringView())) {
                    *registrations[i].value = PoolString(found);
                    duplicates += found != &entry ? 1 : 0;
                } else {
                    pending.push_back(i);
                }
            }
        }
        if (pending.empty()) {
            if (duplicates != 0) {
                _staticDuplicates.fetch_add(duplicates, std::memory_order_relaxed);
            }
            return;
        }

        UniqueLock lock(_mutex);
        ++_writeLocks;
        _staticDuplicates.fetch_add(duplicates, std::memory_order_relaxed);
        // The table grows at most once, for every entry still absent (a repeat inside the batch is counted too: the table
        // may then be one step larger than the insertions need)
        _table.Reserve(_table.Used() + pending.size());
        for (const size_t i : pending) {
            const Entry& entry = *registrations[i].entry;
            if (const Entry* found = _table.Find(entry.hash, entry.ToStringView())) {
                *registrations[i].value = PoolString(found);
                if (found != &entry) {
                    _staticDuplicates.fetch_add(1, std::memory_order_relaxed);
                }
                continue;
            }
            _table.Insert(entry.hash, &entry);
            ++_static.strings;
            _static.bytes += PoolEntryBytes(entry.size);
            *registrations[i].value = PoolString(&entry);
        }
    }

    void PoolStringStorage::Reserve(size_t strings, size_t bytes) {
        UniqueLock lock(_mutex);
        ++_writeLocks;
        // Each half of the hint is dropped when it is not sane (it may come from a file header): the sum is checked
        // against the table's limit before it is formed, and a block larger than the arena's reservation could only come
        // from the heap fallback, which aborts when it cannot allocate
        constexpr size_t kMaxStrings = PoolEntryTable<Entry>::kMaxReservedEntries;
        if (strings <= kMaxStrings && _table.Used() <= kMaxStrings - strings) {
            _table.Reserve(_table.Used() + strings);
        }
        if (bytes == 0 || bytes > kDefaultReservedBytes) {
            return;
        }
        const size_t aligned = (bytes + kPoolEntryAlignment - 1) & ~(kPoolEntryAlignment - 1);
        if (_capacity - _offset < aligned) {
            StartCurrentBlock(aligned, 0);
        }
    }

    PoolBlock PoolStringStorage::AllocateBlock(size_t bytes) {
        // A size from a file header is validated by the loader before it gets here: the rounding below cannot wrap under this limit
        ASSERT(bytes <= kPoolEntryMaxTextBytes, "AllocateBlock: the size is not a size of a block");
        const size_t aligned = bytes == 0 ? kPoolEntryAlignment : (bytes + kPoolEntryAlignment - 1) & ~(kPoolEntryAlignment - 1);
        UniqueLock lock(_mutex);
        ++_writeLocks;
        char* memory = TakeBlock(aligned);
        _blocks.push_back({.data = memory, .kind = PoolBlockKind::Allocated, .capacity = aligned});
        ++_unadoptedBlocks;
        _unadoptedBlockBytes += aligned;
        return PoolBlock(memory, aligned, _id, _blocks.size() - 1);
    }

    Optional<PoolAdoptError> PoolStringStorage::CheckToken(const PoolBlock& block) const {
        if (block._storageId != _id || block._index >= _blocks.size() || _blocks[block._index].data != block._data || _blocks[block._index].capacity != block._capacity) {
            return PoolAdoptError::NotOwned;
        }
        // A successful adoption empties its token and a token cannot be copied: no live token names an adopted block
        ASSERT(_blocks[block._index].kind == PoolBlockKind::Allocated);
        return std::nullopt;
    }

    std::expected<PoolAdoptResult, PoolAdoptError> PoolStringStorage::AdoptBlock(PoolBlock& block, size_t usedBytes, Span<PoolString> out, PoolAdoptOptions options) {
        {
            SharedLock lock(_mutex);
            if (const Optional<PoolAdoptError> error = CheckToken(block)) {
                return std::unexpected(*error);
            }
        }
        if (usedBytes > block._capacity) {
            return std::unexpected(PoolAdoptError::UsedBytesTooLarge);
        }
        if (usedBytes % kPoolEntryAlignment != 0) {
            return std::unexpected(PoolAdoptError::UsedBytesNotAligned);
        }

        // The walk. Every size is compared with what is left before it is added to anything, so a size field of 0xFFFFFFFF
        // cannot overflow a 32-bit `size_t` or make a read pass `usedBytes`.
        constexpr size_t kHeaderBytes = offsetof(Entry, data);
        constexpr size_t kMinEntryBytes = PoolEntryBytes(0);
        const char* const base = block._data;
        Vector<const Entry*> entries;
        size_t linkable = 0;
        for (size_t offset = 0; offset < usedBytes;) {
            const size_t remaining = usedBytes - offset;
            if (remaining < kMinEntryBytes) {
                return std::unexpected(PoolAdoptError::TruncatedEntry);
            }
            uint32_t size = 0;
            std::memcpy(&size, base + offset + offsetof(Entry, size), sizeof(size));
            if (size > remaining - kHeaderBytes - 1) {
                return std::unexpected(PoolAdoptError::TruncatedEntry);
            }
            const size_t entryBytes = PoolEntryBytes(size);
            if (entryBytes > remaining) {
                return std::unexpected(PoolAdoptError::TruncatedEntry);
            }
            if (size > options.maxTextBytes) {
                return std::unexpected(PoolAdoptError::TextTooLong);
            }
            if (base[offset + kHeaderBytes + size] != '\0') {
                return std::unexpected(PoolAdoptError::MissingNul);
            }
            if (options.verifyHashes) {
                uint64_t hash = 0;
                std::memcpy(&hash, base + offset + offsetof(Entry, hash), sizeof(hash));
                if (hash != StringUtils::GetHash(StringView(base + offset + kHeaderBytes, size))) {
                    return std::unexpected(PoolAdoptError::WrongHash);
                }
            }
            entries.push_back(reinterpret_cast<const Entry*>(base + offset));
            linkable += size != 0 ? 1 : 0;
            offset += entryBytes;
        }
        if (!out.empty() && out.size() != entries.size()) {
            return std::unexpected(PoolAdoptError::OutSizeMismatch);
        }

        UniqueLock lock(_mutex);
        ++_writeLocks;
        _table.Reserve(_table.Used() + linkable);
        PoolAdoptResult result;
        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry* entry = entries[i];
            PoolString winner;
            if (entry->size == 0) {
                ++result.deadStrings;
                result.deadBytes += PoolEntryBytes(0);
            } else if (const Entry* older = _table.Find(entry->hash, entry->ToStringView())) {
                winner = PoolString(older);
                ++result.deadStrings;
                result.deadBytes += PoolEntryBytes(entry->size);
            } else {
                _table.Insert(entry->hash, entry);
                winner = PoolString(entry);
                ++result.linkedStrings;
                _adopted.bytes += PoolEntryBytes(entry->size);
            }
            if (!out.empty()) {
                out[i] = winner;
            }
        }

        PoolBlockRow& record = _blocks[block._index];
        record.kind = PoolBlockKind::Adopted;
        record.used = usedBytes;
        record.deadStrings = result.deadStrings;
        record.deadBytes = result.deadBytes;
        record.source = String(options.sourceName.begin(), options.sourceName.end());
        _adopted.strings += result.linkedStrings;
        ++_adopted.blocks;
        _adopted.blockBytes += block._capacity;
        --_unadoptedBlocks;
        _unadoptedBlockBytes -= block._capacity;
        _deadStrings += result.deadStrings;
        _deadBytes += result.deadBytes;
        block = PoolBlock();
        return result;
    }

    void PoolStringStorage::ReadLockedStats(PoolStringStats& stats) const {
        stats.stringCount = _table.Used();
        stats.stringBytes = _runtime.bytes + _static.bytes + _adopted.bytes;
        stats.blockCount = _runtime.blocks + _adopted.blocks + _unadoptedBlocks;
        stats.blockBytes = _runtime.blockBytes + _adopted.blockBytes + _unadoptedBlockBytes;
        stats.tableCapacity = _table.Capacity();
        stats.tableUsed = _table.Used();
        stats.longestProbe = _table.LongestProbe();
        stats.misses = _misses;
        stats.runtime = _runtime;
        stats.staticEntries = _static;
        stats.adopted = _adopted;
        stats.deadStrings = _deadStrings;
        stats.deadBytes = _deadBytes;
        stats.staticDuplicates = _staticDuplicates.load(std::memory_order_relaxed);
        stats.writeLocks = _writeLocks;
        stats.heapFallbackBlocks = _heapFallbackBlocks;
    }

    void PoolStringStorage::AddThreadCounters(PoolStringStats& stats) {
        {
            LockGuard lock(_countersMutex);
            for (const UniquePtr<PoolHitCounter>& counter : _hitCounters) {
                stats.hits += counter->hits.load(std::memory_order_relaxed);
                stats.batchCalls += counter->batchCalls.load(std::memory_order_relaxed);
                stats.batchStrings += counter->batchStrings.load(std::memory_order_relaxed);
            }
        }
        stats.internCalls = stats.hits + stats.misses;
    }

    PoolStringStats PoolStringStorage::GetStats() {
        PoolStringStats stats;
        {
            SharedLock lock(_mutex);
            ReadLockedStats(stats);
        }
        AddThreadCounters(stats);
        return stats;
    }

    PoolStringTotals PoolStringStorage::GetTotals() {
        SharedLock lock(_mutex);
        return {_runtime.strings, _runtime.bytes, _static.strings + _adopted.strings, _static.bytes + _adopted.bytes};
    }

    void PoolStringStorage::TakeSnapshot(PoolStringSnapshot& snapshot) {
        size_t stringCount = 0;
        size_t blockCount = 0;
        {
            SharedLock lock(_mutex);
            stringCount = _table.Used();
            blockCount = _blocks.size();
        }
        // Reserved outside the lock; only what is added in between can make a vector grow under it
        snapshot.strings.clear();
        snapshot.strings.reserve(stringCount);
        snapshot.blocks.reserve(blockCount);
        snapshot.stats = PoolStringStats();
        {
            SharedLock lock(_mutex);
            ReadLockedStats(snapshot.stats);
            _table.ForEach([&snapshot](const Entry& entry) { snapshot.strings.push_back({.text = PoolString(&entry)}); });
            snapshot.blocks.assign(_blocks.begin(), _blocks.end());
        }
        AddThreadCounters(snapshot.stats);
        ClassifyRows(snapshot);
    }

    void PoolStringStorage::SetObserver(IPoolStringObserver& observer) {
        UniqueLock lock(_mutex);
        _observer = &observer;
    }

    void PoolStringStorage::RemoveObserver(const IPoolStringObserver& observer) {
        UniqueLock lock(_mutex);
        if (_observer == &observer) {
            _observer = nullptr;
        }
    }

}  // namespace BECore::Details
