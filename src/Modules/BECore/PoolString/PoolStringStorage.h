#pragma once

#include <BECore/PoolString/PoolBlockSource.h>
#include <BECore/PoolString/PoolEntryTable.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Threading/LockGuards.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Threading/SharedMutex.h>
#include <BECore/Types/Types.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace BECore::Details {

    /// The hit counter of one thread for one storage. Only its thread writes it, with a plain load and store (no
    /// read-modify-write), and no other counter shares its cache line, so counting a hit adds no shared write to `Intern`.
    struct alignas(64) PoolHitCounter {
        std::atomic<uint64_t> hits{0};
        std::atomic<uint64_t> batchCalls{0};
        std::atomic<uint64_t> batchStrings{0};
        const void* owner = nullptr;  ///< Identifies the thread that writes it; only read under the counters' mutex
    };

    /// The counter of the calling thread for the storage it used last. A thread that alternates between storages (a test
    /// next to the process-wide pool) finds its counter again through `PoolStringStorage::RegisterThread`.
    struct PoolThreadCounterCache {
        uint64_t storageId = 0;
        PoolHitCounter* counter = nullptr;
    };

    inline constinit thread_local PoolThreadCounterCache t_counterCache;

    /// The pool itself: the table, the blocks the entries live in, the locks and the statistics. `PoolString` calls the
    /// one process-wide instance; a test builds its own over a fake `IPoolBlockSource` and a small table to check
    /// growth, block accounting and the failure path with absolute numbers.
    ///
    /// Invariants:
    /// - An entry lies inside one block: a string never runs past the memory it was copied into. A string that does not
    ///   fit the rest of the current block starts a standard block (the rest is lost); one larger than a standard block
    ///   gets a block of exactly its size and the current block stays current for the next small strings.
    /// - Blocks come from the source. A source that has none (`PoolMemory` empty) is answered with a block from the
    ///   heap, held by the storage and counted in `heapFallbackBlocks`: `Intern` never fails.
    /// - Entries never move and are never freed; only the table's slots move when it grows, so a `PoolString` taken out
    ///   of the pool stays valid for the life of the storage.
    /// - One `SharedMutex`. Lookups run under the shared lock and a hit takes nothing else; everything that writes (an
    ///   insert, growth, a block) happens under the unique lock, never on the hit path. The block source is called under
    ///   the unique lock only.
    class PoolStringStorage {
    public:
        /// `source` is not owned and outlives the storage. `initialTableCapacity` is a power of two.
        PoolStringStorage(IPoolBlockSource& source, size_t initialTableCapacity);

        PoolStringStorage(const PoolStringStorage&) = delete;
        PoolStringStorage& operator=(const PoolStringStorage&) = delete;

        /// The hit path is inline so that `PoolString::Intern` costs one call, not two
        [[nodiscard]] PoolString Intern(StringView str) {
            if (str.empty()) {
                CountHit();
                return PoolString();
            }
            const uint64_t hash = StringUtils::GetHash(str);
            {
                SharedLock lock(_mutex);
                if (const Entry* entry = _table.Find(hash, str)) {
                    CountHit();
                    return PoolString(entry);
                }
            }
            return InternMiss(str, hash);
        }

        /// The pooled string equal to `str`, or nullopt; never adds. The empty text is the empty `PoolString`.
        [[nodiscard]] Optional<PoolString> Find(StringView str);

        /// `PoolString::InternBatch`: `out[i]` is the string of `texts[i]`. A hit takes the shared lock only, once for the whole
        /// batch; the misses take the unique lock once, the table grows once and every new string of the batch is placed in
        /// one allocation (see `AllocateEntryMemory`). A text twice in a batch is one miss and one hit.
        void InternBatch(Span<const StringView> texts, Span<PoolString> out);
        void InternBatch(Span<const StringView> texts, Span<const uint64_t> hashes, Span<PoolString> out);

        /// `PoolString::RegisterStatic`. Pointers of the entries enter the table: nothing is hashed, copied or allocated for the
        /// text, and an entry is never written to, so it must outlive the storage. The first entry of a text wins.
        void RegisterStatic(Span<const PoolStaticRegistration> registrations);

        /// `PoolString::Reserve`
        void Reserve(size_t strings, size_t bytes);

        [[nodiscard]] PoolStringStats GetStats();

    private:
        using Entry = PoolStringEntry;

        IPoolBlockSource& _source;
        const uint64_t _id;

        SharedMutex _mutex{BE_LOCK_SITE("PoolString")};
        PoolEntryTable<Entry> _table;

        // Written under the unique lock, read under the shared one
        uint64_t _misses = 0;
        uint64_t _writeLocks = 0;
        size_t _heapFallbackBlocks = 0;
        PoolSourceStats _static;  // blocks stay 0: the entries are in the program image
        PoolSourceStats _runtime;
        // Counted under either lock (a registration of entries that all lost needs no unique lock), so atomic
        std::atomic<size_t> _staticDuplicates{0};
        UniquePtr<IPoolBlockSource> _fallback;  // the heap source, made on the first empty answer of `_source`
        char* _current = nullptr;               // the block that small entries go into
        size_t _offset = 0;
        size_t _capacity = 0;

        // One counter per thread that ever interned. A counter outlives its thread (the sum must keep its hits) and is
        // never freed, like the pool; the threads of the engine are a small fixed set.
        Mutex _countersMutex{BE_LOCK_SITE("PoolString counters")};
        Vector<UniquePtr<PoolHitCounter>> _hitCounters;

        PoolHitCounter& RegisterThread();

        PoolHitCounter& CounterOfThread() {
            if (t_counterCache.storageId != _id) [[unlikely]] {
                RegisterThread();
            }
            return *t_counterCache.counter;
        }

        static void Bump(std::atomic<uint64_t>& counter, uint64_t count) {
            counter.store(counter.load(std::memory_order_relaxed) + count, std::memory_order_relaxed);
        }

        void CountHit() {
            Bump(CounterOfThread().hits, 1);
        }

        /// The slow half of `Intern`: takes the unique lock, looks again, inserts
        PoolString InternMiss(StringView str, uint64_t hash);

        /// A block of `bytes` (a multiple of 8) from the source, or from the heap when the source has none
        char* TakeBlock(size_t bytes);

        /// Memory for `entryCount` entries that take `bytes` (a multiple of 8) together; the caller holds the unique lock.
        /// What fits the rest of the current block goes there. Otherwise a single entry of at most one standard block starts a
        /// new standard block, which becomes current (the rest of the old one is lost); anything larger, or several entries
        /// of a batch, gets a block of exactly `bytes` and the current block stays current.
        char* AllocateEntryMemory(size_t bytes, size_t entryCount);

        /// The slow half of `InternBatch`: re-checks the texts at `missing` under the unique lock and places the new ones.
        /// Returns how many new strings it added.
        size_t InternBatchMisses(Span<const StringView> texts, Span<const uint64_t> hashes, Span<PoolString> out, Vector<size_t>& missing);
    };

}  // namespace BECore::Details
