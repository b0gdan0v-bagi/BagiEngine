#include "PoolString.h"

namespace BECore {

    /// Open-addressing set of entries: slots of `{hash, entry}`, a power-of-two capacity, linear probing, an empty slot has
    /// a null entry. The stored hash lets a probe skip a slot without touching its entry. The owner (`Storage`) holds the
    /// shared lock around `Find` and the unique lock around `Insert`, which also grows the table. Only the slots move on
    /// growth: entries live in blocks and never move, so a `const T*` taken out of the table stays valid.
    template <typename T>
    class PoolEntryTable {
        struct Slot {
            uint64_t hash = 0;
            const T* entry = nullptr;
        };

        /// The table doubles when it would hold more than capacity / kMaxLoadDivisor entries
        static constexpr size_t kMaxLoadDivisor = 2;

        Vector<Slot> _slots;
        size_t _used = 0;
        size_t _longestProbe = 0;

        [[nodiscard]] size_t Mask() const noexcept {
            return _slots.size() - 1;
        }

        /// Returns how many slots past its home slot the entry landed in
        static size_t Place(Vector<Slot>& slots, uint64_t hash, const T* entry) noexcept {
            const size_t mask = slots.size() - 1;
            size_t idx = static_cast<size_t>(hash) & mask;
            size_t probe = 0;
            while (slots[idx].entry != nullptr) {
                idx = (idx + 1) & mask;
                ++probe;
            }
            slots[idx] = Slot{hash, entry};
            return probe;
        }

        void Grow() {
            Vector<Slot> grown(_slots.size() * 2);
            size_t longest = 0;
            for (const Slot& slot : _slots) {
                if (slot.entry != nullptr) {
                    longest = std::max(longest, Place(grown, slot.hash, slot.entry));
                }
            }
            _slots = eastl::move(grown);
            _longestProbe = longest;
        }

    public:
        explicit PoolEntryTable(size_t initialCapacity) : _slots(initialCapacity) {
            ASSERT(initialCapacity >= 2 && (initialCapacity & (initialCapacity - 1)) == 0);
        }

        /// The entry must not be in the table yet (the caller has just looked it up under the same unique lock)
        void Insert(uint64_t hash, const T* entry) {
            if ((_used + 1) * kMaxLoadDivisor > _slots.size()) {
                Grow();
            }
            _longestProbe = std::max(_longestProbe, Place(_slots, hash, entry));
            ++_used;
        }

        [[nodiscard]] size_t Used() const noexcept {
            return _used;
        }
        [[nodiscard]] size_t Capacity() const noexcept {
            return _slots.size();
        }
        /// The largest distance of a slot of the table from its home slot, as of now (growth recomputes it)
        [[nodiscard]] size_t LongestProbe() const noexcept {
            return _longestProbe;
        }

        [[nodiscard]] const T* Find(uint64_t hash, StringView str) const noexcept {
            const size_t mask = Mask();
            for (size_t idx = static_cast<size_t>(hash) & mask;; idx = (idx + 1) & mask) {
                const Slot& slot = _slots[idx];
                if (slot.entry == nullptr) {
                    return nullptr;
                }
                if (slot.hash == hash && slot.entry->size == str.size() && std::memcmp(slot.entry->data, str.data(), str.size()) == 0) {
                    return slot.entry;
                }
            }
        }
    };

    namespace {

        constexpr size_t kCacheLineBytes = 64;

        /// The hit counter of one thread. Only its thread writes it, with a plain load and store (no read-modify-write),
        /// and no other thread's counter shares its cache line, so counting a hit adds no shared write to `Intern`.
        struct alignas(kCacheLineBytes) HitCounter {
            std::atomic<uint64_t> hits{0};
        };

        constinit thread_local HitCounter* t_hitCounter = nullptr;

    }  // namespace

    /// Interned entries live in blocks that are never freed or moved. Invariant: an entry lies inside one block, so a
    /// string never runs past the end of the memory it was copied into. A string that does not fit a standard block gets
    /// a block of exactly its size and the current block keeps its free space for the next small strings.
    class PoolString::Storage {
        static constexpr size_t kInitialTableCapacity = 16384;

        SharedMutex _mutex{BE_LOCK_SITE("PoolString")};
        PoolEntryTable<Entry> _table{kInitialTableCapacity};

        // Written under the unique lock, read under the shared one
        uint64_t _misses = 0;
        size_t _stringBytes = 0;
        size_t _blockBytes = 0;

        // One counter per thread that ever interned. A counter outlives its thread (the sum must keep its hits) and is
        // never freed, like the pool; the threads of the engine are a small fixed set.
        Mutex _countersMutex{BE_LOCK_SITE("PoolString counters")};
        Vector<UniquePtr<HitCounter>> _hitCounters;

        static constexpr size_t kEntryAlignment = alignof(uint64_t);
        static constexpr size_t kBlockWords = kBlockBytes / sizeof(uint64_t);
        static_assert(kBlockBytes % kEntryAlignment == 0);

        // uint64_t words, so every block starts aligned to 8 as the entry header needs
        Vector<UniquePtr<uint64_t[]>> _blocks;
        char* _currentBlock = nullptr;
        size_t _offset = 0;

        HitCounter& RegisterThread() {
            LockGuard lock(_countersMutex);
            _hitCounters.push_back(MakeUnique<HitCounter>());
            t_hitCounter = _hitCounters.back().get();
            return *t_hitCounter;
        }

        void CountHit() {
            HitCounter* counter = t_hitCounter;
            if (counter == nullptr) [[unlikely]] {
                counter = &RegisterThread();
            }
            counter->hits.store(counter->hits.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        }

        void AddStandardBlock() {
            _blockBytes += kBlockBytes;
            _blocks.push_back(MakeUnique<uint64_t[]>(kBlockWords));
            _currentBlock = reinterpret_cast<char*>(_blocks.back().get());
            _offset = 0;
        }

        /// Memory for one entry of `alignedSize` bytes (a multiple of 8). The caller holds the unique lock.
        char* AllocateEntryMemory(size_t alignedSize) {
            if (alignedSize > kBlockBytes) {
                _blockBytes += alignedSize;
                _blocks.push_back(MakeUnique<uint64_t[]>(alignedSize / sizeof(uint64_t)));
                return reinterpret_cast<char*>(_blocks.back().get());
            }
            if (_offset + alignedSize > kBlockBytes) {
                AddStandardBlock();
            }
            char* memory = _currentBlock + _offset;
            _offset += alignedSize;
            return memory;
        }

    public:
        static Storage& Instance() {
            static Storage instance;
            return instance;
        }

        Storage() {
            AddStandardBlock();
        }

        const Entry* Find(StringView str) {
            if (str.empty()) {
                return &Details::g_EmptyEntryStore.header;
            }

            const uint64_t hash = StringUtils::GetHash(str);
            SharedLock lock(_mutex);
            return _table.Find(hash, str);
        }

        PoolStringStats GetStats() {
            PoolStringStats stats;
            {
                SharedLock lock(_mutex);
                stats.stringCount = _table.Used();
                stats.stringBytes = _stringBytes;
                stats.blockCount = _blocks.size();
                stats.blockBytes = _blockBytes;
                stats.tableCapacity = _table.Capacity();
                stats.tableUsed = _table.Used();
                stats.longestProbe = _table.LongestProbe();
                stats.misses = _misses;
            }
            {
                LockGuard lock(_countersMutex);
                for (const UniquePtr<HitCounter>& counter : _hitCounters) {
                    stats.hits += counter->hits.load(std::memory_order_relaxed);
                }
            }
            stats.internCalls = stats.hits + stats.misses;
            return stats;
        }

        const Entry* GetOrAdd(StringView str) {
            if (str.empty()) {
                CountHit();
                return &Details::g_EmptyEntryStore.header;
            }

            const uint64_t hash = StringUtils::GetHash(str);

            {
                SharedLock lock(_mutex);
                if (auto e = _table.Find(hash, str)) {
                    CountHit();
                    return e;
                }
            }

            UniqueLock lock(_mutex);

            // Double-check: a thread that lost the race for this text counts a hit, so one text is one miss
            if (auto e = _table.Find(hash, str)) {
                CountHit();
                return e;
            }

            // The entry stores the length in 32 bits: a longer string would be copied past its block
            ASSERT(str.size() <= UINT32_MAX - offsetof(Entry, data) - kEntryAlignment);
            const size_t totalSize = offsetof(Entry, data) + str.size() + 1;
            const size_t alignedSize = (totalSize + kEntryAlignment - 1) & ~(kEntryAlignment - 1);

            Entry* entry = new (AllocateEntryMemory(alignedSize)) Entry(hash, static_cast<uint32_t>(str.size()));
            std::memcpy(entry->data, str.data(), str.size());
            entry->data[str.size()] = '\0';

            _table.Insert(hash, entry);
            _stringBytes += alignedSize;
            ++_misses;
            return entry;
        }
    };

    PoolString PoolString::Intern(StringView str) {
        return PoolString(Storage::Instance().GetOrAdd(str));
    }

    PoolStringStats PoolString::GetStats() {
        return Storage::Instance().GetStats();
    }

    Optional<PoolString> PoolString::Find(StringView str) {
        if (const Entry* entry = Storage::Instance().Find(str)) {
            return PoolString(entry);
        }
        return std::nullopt;
    }

}  // namespace BECore
