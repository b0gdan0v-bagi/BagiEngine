#pragma once

#include <BECore/PoolString/PoolBlock.h>
#include <BECore/PoolString/PoolBlockSource.h>
#include <BECore/PoolString/PoolEntryTable.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringObserver.h>
#include <BECore/PoolString/PoolStringSnapshot.h>
#include <BECore/Threading/LockGuards.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Threading/SharedMutex.h>
#include <BECore/Types/Types.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>

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

    /// The strings and aligned entry bytes of the table by what makes them grow: `runtime` is what `Intern` and
    /// `InternBatch` copied, `other` the registered and the adopted entries
    struct PoolStringTotals {
        size_t runtimeStrings = 0;
        size_t runtimeBytes = 0;
        size_t otherStrings = 0;
        size_t otherBytes = 0;
    };

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

        /// `PoolString::AllocateBlock`
        [[nodiscard]] PoolBlock AllocateBlock(size_t bytes);

        /// `PoolString::AdoptBlock`. The checks of the token and of the block's bytes run outside the unique lock (they hash
        /// every text), so a big block does not stall the interns of other threads; the lock is taken to link. A success empties `block`.
        std::expected<PoolAdoptResult, PoolAdoptError> AdoptBlock(PoolBlock& block, size_t usedBytes, Span<PoolString> out, PoolAdoptOptions options);

        [[nodiscard]] PoolStringStats GetStats();

        /// Cheap enough for every frame: the shared lock and no walk over the counters of the threads
        [[nodiscard]] PoolStringTotals GetTotals();

        /// Refills `snapshot` with every string of the table and every block. The table and the blocks are copied under the
        /// shared lock, so they agree with each other; the lock is released before the call returns. The call counters of
        /// `snapshot.stats` are read as in `GetStats`. A walk over the whole table: not for every frame.
        void TakeSnapshot(PoolStringSnapshot& snapshot);

        /// One observer at a time: a second one takes the place of the first. `observer` is not owned and is removed
        /// before it dies. Takes the unique lock, so it must not be called from the observer.
        void SetObserver(IPoolStringObserver& observer);

        /// Removes `observer` if it is the one set. No call of it is running or will follow once this returns.
        void RemoveObserver(const IPoolStringObserver& observer);

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
        PoolSourceStats _adopted;
        size_t _unadoptedBlocks = 0;  // blocks given out by `AllocateBlock` and not adopted yet: held, counted in the totals only
        size_t _unadoptedBlockBytes = 0;
        size_t _deadStrings = 0;
        size_t _deadBytes = 0;
        Vector<PoolBlockRow> _blocks;  // every block the pool holds, in the order it took them; a token's `_index` points here
        IPoolStringObserver* _observer = nullptr;
        // Counted under either lock (a registration of entries that all lost needs no unique lock), so atomic
        std::atomic<size_t> _staticDuplicates{0};
        UniquePtr<IPoolBlockSource> _fallback;  // the heap source, made on the first empty answer of `_source`
        char* _current = nullptr;               // the block that small entries go into
        size_t _offset = 0;
        size_t _capacity = 0;
        size_t _currentBlock = 0;  // the record of `_current` in `_blocks`, meaningless while `_capacity` is 0

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

        /// A block of `bytes` (a multiple of 8) from the source, or from the heap when the source has none; counts
        /// `heapFallbackBlocks` and nothing else
        char* TakeBlock(size_t bytes);

        /// A block for the entries `Intern` and its kin copy: `TakeBlock`, counted in `runtime` and recorded with `used` bytes taken
        char* TakeRuntimeBlock(size_t bytes, size_t used);

        /// Takes a runtime block that small entries go into from now on, its first `used` bytes taken
        void StartCurrentBlock(size_t bytes, size_t used);

        /// The fields of `GetStats` that the lock guards; the caller holds either lock
        void ReadLockedStats(PoolStringStats& stats) const;

        /// The call counters of every thread, added to `stats`; takes no lock of the pool
        void AddThreadCounters(PoolStringStats& stats);

        /// `NotOwned` when `block` is not a block this storage gave out (an empty or spent token included); the caller holds either lock
        [[nodiscard]] Optional<PoolAdoptError> CheckToken(const PoolBlock& block) const;

        /// Memory for `entryCount` entries that take `bytes` (a multiple of 8) together; the caller holds the unique lock.
        /// What fits the rest of the current block goes there. Otherwise a single entry of at most one standard block starts a
        /// new standard block, which becomes current (the rest of the old one is lost); anything larger, or several entries
        /// of a batch, gets a block of exactly `bytes` and the current block stays current.
        char* AllocateEntryMemory(size_t bytes, size_t entryCount);

        /// The slow half of `InternBatch`: re-checks the texts at `missing` under the unique lock and places the new ones.
        /// Returns how many new strings it added.
        size_t InternBatchMisses(Span<const StringView> texts, Span<const uint64_t> hashes, Span<PoolString> out, Vector<size_t>& missing);
    };

    /// The storage behind `PoolString`, made on the first use and never destroyed
    [[nodiscard]] PoolStringStorage& GetProcessPoolStorage();

}  // namespace BECore::Details
