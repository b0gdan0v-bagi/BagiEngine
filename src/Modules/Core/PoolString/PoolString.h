#pragma once

#include <EASTL/string_view.h>
#include <shared_mutex>
#include <type_traits>

namespace Core {

    struct PoolHash {
        static constexpr uint64_t OFFSET = 14695981039346656037ULL;
        static constexpr uint64_t PRIME = 1099511628211ULL;

        static constexpr uint64_t Calculate(eastl::string_view sv) noexcept {
            uint64_t hash = OFFSET;
            for (char c : sv) {
                hash ^= static_cast<uint64_t>(c);
                hash *= PRIME;
            }
            return hash;
        }
    };

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
            constexpr EmptyEntry() : header(PoolHash::OFFSET, 0) {}
        };

        // Инициализируем пустую запись здесь, чтобы избежать C2131 внутри класса
        inline constexpr EmptyEntry g_EmptyEntryStore{};

        template <size_t N>
        struct FixedString {
            char data[N]{};
            consteval FixedString(const char (&str)[N]) {
                for (size_t i = 0; i < N; ++i)
                    data[i] = str[i];
            }
            constexpr eastl::string_view View() const noexcept {
                return {data, N - 1};
            }
        };

        template <size_t N>
        struct StaticPoolStringEntry {
            PoolStringEntry header;
            char storage[N];

            consteval StaticPoolStringEntry(FixedString<N> fs) : header(PoolHash::Calculate(fs.View()), static_cast<uint32_t>(N - 1)), storage{} {
                for (size_t i = 0; i < N; ++i)
                    storage[i] = fs.data[i];
            }

            constexpr const PoolStringEntry* AsEntry() const noexcept {
                return &header;
            }
        };
    }  // namespace Details

    class PoolString {
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
        template <Details::FixedString Str>
        friend class StaticPoolString;
        class Storage;
        friend class PoolString::Storage;

        using Entry = Details::PoolStringEntry;
        constexpr explicit PoolString(const Entry* entry) noexcept : _entry(entry) {}

        const Entry* _entry;
    };

    template <Details::FixedString Str>
    class StaticPoolString {
    public:
        // Ленивое рантайм-интернирование
        static PoolString ToPoolString() noexcept {
            static PoolString cached = PoolString::Intern(Str.View());
            return cached;
        }

        operator PoolString() const noexcept {
            return ToPoolString();
        }

        // Сравнение для static_assert
        template <Details::FixedString OtherStr>
        constexpr bool operator==(StaticPoolString<OtherStr>) const noexcept {
            return Str.View() == OtherStr.View();
        }

        constexpr bool operator==(const PoolString& other) const noexcept {
            if consteval {
                return PoolHash::Calculate(Str.View()) == other.HashValue();
            }
            return ToPoolString() == other;
        }

        [[nodiscard]] consteval uint64_t HashValue() const noexcept {
            return PoolHash::Calculate(Str.View());
        }

        [[nodiscard]] constexpr eastl::string_view View() const noexcept {
            return Str.View();
        }
    };

    template <Details::FixedString Str>
    consteval auto operator""_ps() {
        return StaticPoolString<Str>{};
    }

}  // namespace Core