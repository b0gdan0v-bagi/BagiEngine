#pragma once

#include <EASTL/string_view.h>
#include <BECore/Utils/String.h>
#include <shared_mutex>

namespace BECore {

    class PoolStringChain;
    namespace Details {

        struct PoolStringEntry {
            const uint64_t hash;
            PoolStringEntry* nextEntry;
            const uint32_t size;
            char data[1];

            constexpr PoolStringEntry(uint64_t h, uint32_t s) : hash(h), nextEntry(nullptr), size(s), data{'\0'} {}

            [[nodiscard]] constexpr eastl::string_view ToStringView() const noexcept {
                return {&data[0], static_cast<size_t>(size)};
            }
        };

        // Специальная обертка для пустой строки, вынесенная отдельно для MSVC
        struct EmptyEntry {
            PoolStringEntry header;
            constexpr EmptyEntry() : header(BECore::String::GetEmptyHash(), 0) {}
        };

        // Инициализируем пустую запись здесь, чтобы избежать C2131 внутри класса
        inline constexpr EmptyEntry g_EmptyEntryStore{};

    }  // namespace Details

    class PoolString {
        friend class PoolStringChain;
    public:
        // Используем адрес заранее созданной константы
        constexpr PoolString() noexcept : _entry(&Details::g_EmptyEntryStore.header) {}

        static PoolString Intern(eastl::string_view str);

        [[nodiscard]] constexpr uint64_t HashValue() const noexcept {
            return _entry->hash;
        }
        [[nodiscard]] constexpr eastl::string_view ToStringView() const noexcept {
            return _entry->ToStringView();
        }
        [[nodiscard]] constexpr const char* CStr() const noexcept {
            return _entry->data;
        }

        [[nodiscard]] constexpr bool Empty() const noexcept {
            return _entry->size == 0;
        }

        [[nodiscard]] constexpr size_t Length() const noexcept {
            return _entry->size;
        }

        [[nodiscard]] constexpr bool operator==(const PoolString& other) const noexcept {
            if consteval {
                return _entry->hash == other._entry->hash;
            }
            return _entry == other._entry;
        }
        [[nodiscard]] constexpr bool operator!=(const PoolString& other) const noexcept {
            return !(*this == other);
        }

    private:
        class Storage;
        friend class PoolString::Storage;

        using Entry = Details::PoolStringEntry;
        constexpr explicit PoolString(const Entry* entry) noexcept : _entry(entry) {}

        const Entry* _entry;
    };

}  // namespace BECore