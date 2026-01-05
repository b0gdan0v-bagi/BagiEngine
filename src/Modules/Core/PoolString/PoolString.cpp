#include "PoolString.h"

#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>
#include <cstring>
#include <shared_mutex>

namespace Core {

    // Оптимизированная интрузивная хеш-таблица
    template <typename T, uint32_t BucketCount>
    class IntrusivePoolTable {
        static_assert((BucketCount & (BucketCount - 1)) == 0, "BucketCount must be power of 2");
        T* _buckets[BucketCount] = {nullptr};

    public:
        void Insert(T* entry) noexcept {
            const uint32_t index = static_cast<uint32_t>(entry->hash & (BucketCount - 1));
            entry->nextEntry = _buckets[index];
            _buckets[index] = entry;
        }

        T* Find(uint64_t hash, eastl::string_view str) const noexcept {
            const uint32_t index = static_cast<uint32_t>(hash & (BucketCount - 1));
            for (T* e = _buckets[index]; e; e = e->nextEntry) {
                if (e->hash == hash && e->size == str.size()) {
                    if (std::memcmp(e->data, str.data(), e->size) == 0)
                        return e;
                }
            }
            return nullptr;
        }
    };

    class PoolString::Storage {
        mutable std::shared_mutex _mutex;

        // Статическая таблица: Lock-free поиск, так как не меняется после инициализации Storage
        IntrusivePoolTable<Entry, 4096> _staticTable;

        // Динамическая таблица: Защищена мьютексом
        IntrusivePoolTable<Entry, 16384> _dynamicTable;

        struct Page {
            static constexpr size_t Size = 64 * 1024;  // 64KB страницы для лучшей локальности
            alignas(Entry) char data[Size];
            eastl::unique_ptr<Page> prev;
        };
        eastl::unique_ptr<Page> _currentPage;
        size_t _offset = 0;

    public:
        static Storage& Instance() {
            static Storage instance;
            return instance;
        }

        // Накопитель для статических записей до инициализации синглтона
        static eastl::vector<const Entry*>& PendingEntries() {
            static eastl::vector<const Entry*> vec;
            return vec;
        }

        Storage() {
            for (auto e : PendingEntries()) {
                _staticTable.Insert(const_cast<Entry*>(e));
            }
            _currentPage = eastl::make_unique<Page>();
        }

        const Entry* GetOrAdd(eastl::string_view str) {
            const uint64_t hash = String::GetHash(str);

            // 1. Быстрый Lock-free поиск в статической таблице
            if (auto e = _staticTable.Find(hash, str)) {
                return e;
            }

            // 2. Поиск в динамической под Reader Lock
            {
                std::shared_lock lock(_mutex);
                if (auto e = _dynamicTable.Find(hash, str)) {
                    return e;
                }
            }

            // 3. Аллокация новой строки под Writer Lock
            std::unique_lock lock(_mutex);

            // Повторная проверка (double-checked locking)
            if (auto e = _dynamicTable.Find(hash, str)) {
                return e;
            }

            const size_t needed = sizeof(Entry) + str.size();  // +1 для \0 уже в sizeof(Entry)
            const size_t alignedNeeded = (needed + alignof(Entry) - 1) & ~(alignof(Entry) - 1);

            if (_offset + alignedNeeded > Page::Size) {
                auto newPage = eastl::make_unique<Page>();
                newPage->prev = std::move(_currentPage);
                _currentPage = std::move(newPage);
                _offset = 0;
            }

            Entry* newEntry = new (&_currentPage->data[_offset]) Entry(hash, str.size());
            char* entryData = const_cast<char*>(&newEntry->data[0]);
            std::memcpy(entryData, str.data(), str.size());
            entryData[str.size()] = '\0';

            _offset += alignedNeeded;
            _dynamicTable.Insert(newEntry);

            return newEntry;
        }
    };

    bool PoolString::RegisterStatic(const Entry* entry) {
        Storage::PendingEntries().push_back(entry);
        return true;
    }

    PoolString PoolString::Intern(eastl::string_view str) {
        if (str.empty())
            return PoolString();
        return PoolString(Storage::Instance().GetOrAdd(str));
    }

    PoolString PoolString::Find(eastl::string_view str) {
        if (str.empty())
            return PoolString();
        // В упрощенной реализации Find делает то же, что GetOrAdd,
        // но в реальности может просто возвращать nullptr, если не найдено.
        return PoolString(Storage::Instance().GetOrAdd(str));
    }

}  // namespace Core
