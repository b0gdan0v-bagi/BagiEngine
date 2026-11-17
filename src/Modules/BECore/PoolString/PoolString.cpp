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

        [[nodiscard]] size_t Mask() const noexcept {
            return _slots.size() - 1;
        }

        static void Place(Vector<Slot>& slots, uint64_t hash, const T* entry) noexcept {
            const size_t mask = slots.size() - 1;
            size_t idx = static_cast<size_t>(hash) & mask;
            while (slots[idx].entry != nullptr) {
                idx = (idx + 1) & mask;
            }
            slots[idx] = Slot{hash, entry};
        }

        void Grow() {
            Vector<Slot> grown(_slots.size() * 2);
            for (const Slot& slot : _slots) {
                if (slot.entry != nullptr) {
                    Place(grown, slot.hash, slot.entry);
                }
            }
            _slots = eastl::move(grown);
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
            Place(_slots, hash, entry);
            ++_used;
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

    /// Interned entries live in blocks that are never freed or moved. Invariant: an entry lies inside one block, so a
    /// string never runs past the end of the memory it was copied into. A string that does not fit a standard block gets
    /// a block of exactly its size and the current block keeps its free space for the next small strings.
    class PoolString::Storage {
        static constexpr size_t kInitialTableCapacity = 16384;

        SharedMutex _mutex{BE_LOCK_SITE("PoolString")};
        PoolEntryTable<Entry> _table{kInitialTableCapacity};

        static constexpr size_t kEntryAlignment = alignof(uint64_t);
        static constexpr size_t kBlockWords = kBlockBytes / sizeof(uint64_t);
        static_assert(kBlockBytes % kEntryAlignment == 0);

        // uint64_t words, so every block starts aligned to 8 as the entry header needs
        Vector<UniquePtr<uint64_t[]>> _blocks;
        char* _currentBlock = nullptr;
        size_t _offset = 0;

        void AddStandardBlock() {
            _blocks.push_back(MakeUnique<uint64_t[]>(kBlockWords));
            _currentBlock = reinterpret_cast<char*>(_blocks.back().get());
            _offset = 0;
        }

        /// Memory for one entry of `alignedSize` bytes (a multiple of 8). The caller holds the unique lock.
        char* AllocateEntryMemory(size_t alignedSize) {
            if (alignedSize > kBlockBytes) {
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

        const Entry* GetOrAdd(StringView str) {
            if (str.empty()) {
                return &Details::g_EmptyEntryStore.header;
            }

            const uint64_t hash = StringUtils::GetHash(str);

            {
                SharedLock lock(_mutex);
                if (auto e = _table.Find(hash, str)) {
                    return e;
                }
            }

            UniqueLock lock(_mutex);

            // Double-check
            if (auto e = _table.Find(hash, str)) {
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
            return entry;
        }
    };

    PoolString PoolString::Intern(StringView str) {
        return PoolString(Storage::Instance().GetOrAdd(str));
    }

    Optional<PoolString> PoolString::Find(StringView str) {
        if (const Entry* entry = Storage::Instance().Find(str)) {
            return PoolString(entry);
        }
        return std::nullopt;
    }

}  // namespace BECore
