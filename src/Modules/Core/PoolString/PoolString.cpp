#include "PoolString.h"

#include <mutex>

namespace Core {

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

        T* Find(uint64_t hash, eastl::string_view str) const noexcept {
            const uint32_t idx = static_cast<uint32_t>(hash & (BucketCount - 1));
            for (T* e = _buckets[idx]; e; e = e->nextEntry) {
                if (e->hash == hash && e->size == str.size()) {
                    if (std::memcmp(e->data, str.data(), e->size) == 0)
                        return e;
                }
            }
            return nullptr;
        }
    };

    class PoolString::Storage {
        std::shared_mutex _mutex;
        IntrusivePoolTable<Entry, 16384> _table;

        struct Page {
            static constexpr size_t Size = 128 * 1024;
            alignas(uint64_t) char data[Size];
            eastl::unique_ptr<Page> prev;
        };
        eastl::unique_ptr<Page> _currentPage;
        size_t _offset = 0;

    public:
        static Storage& Instance() {
            static Storage instance;
            return instance;
        }

        Storage() {
            _currentPage = eastl::make_unique<Page>();
        }

        const Entry* GetOrAdd(eastl::string_view str) {
            if (str.empty())
                return &Details::g_EmptyEntryStore.header;

            const uint64_t hash = String::GetHash(str);

            // 1. Поиск под Shared Lock
            {
                std::shared_lock lock(_mutex);
                if (auto e = _table.Find(hash, str))
                    return e;
            }

            // 2. Вставка под Unique Lock
            std::unique_lock lock(_mutex);

            // Double-check
            if (auto e = _table.Find(hash, str))
                return e;

            const size_t headerSize = offsetof(Entry, data);
            const size_t totalSize = headerSize + str.size() + 1;
            const size_t alignedSize = (totalSize + 7) & ~7;

            if (_offset + alignedSize > Page::Size) {
                auto newPage = eastl::make_unique<Page>();
                newPage->prev = eastl::move(_currentPage);
                _currentPage = eastl::move(newPage);
                _offset = 0;
            }

            Entry* entry = new (&_currentPage->data[_offset]) Entry(hash, static_cast<uint32_t>(str.size()));
            std::memcpy(entry->data, str.data(), str.size());
            entry->data[str.size()] = '\0';

            _offset += alignedSize;
            _table.Insert(entry);
            return entry;
        }
    };

    PoolString PoolString::Intern(eastl::string_view str) {
        return PoolString(Storage::Instance().GetOrAdd(str));
    }

}  // namespace Core