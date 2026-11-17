#include "PoolString.h"

namespace BECore {

    template <typename T, uint32_t BucketCount>
    class IntrusivePoolTable {
        static_assert((BucketCount & (BucketCount - 1)) == 0);
        T* _buckets[BucketCount]{};

    public:
        void Insert(T* entry) noexcept {
            const uint32_t idx = static_cast<uint32_t>(entry->hash & (BucketCount - 1));
            entry->nextEntry = _buckets[idx];
            _buckets[idx] = entry;
        }

        T* Find(uint64_t hash, StringView str) const noexcept {
            const uint32_t idx = static_cast<uint32_t>(hash & (BucketCount - 1));
            for (T* e = _buckets[idx]; e; e = e->nextEntry) {
                if (e->hash == hash && e->size == str.size()) {
                    if (std::memcmp(e->data, str.data(), e->size) == 0) {
                        return e;
                    }
                }
            }
            return nullptr;
        }
    };

    /// Interned entries live in blocks that are never freed or moved. Invariant: an entry lies inside one block, so a
    /// string never runs past the end of the memory it was copied into. A string that does not fit a standard block gets
    /// a block of exactly its size and the current block keeps its free space for the next small strings.
    class PoolString::Storage {
        SharedMutex _mutex{BE_LOCK_SITE("PoolString")};
        IntrusivePoolTable<Entry, 16384> _table;

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

            _table.Insert(entry);
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
