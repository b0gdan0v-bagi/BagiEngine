#pragma once

#include <Core/Utils/String.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <shared_mutex>

namespace Core {

    namespace Details {

        // Интрузивная запись: содержит всё необходимое для хранения и поиска в таблице
        struct PoolStringEntry {
            const uint64_t hash;
            PoolStringEntry* nextEntry = nullptr;  // Интрузивная цепочка для хеш-таблицы
            const uint32_t size;
            const char data[1] = {'\0'};

            constexpr PoolStringEntry(uint64_t h, size_t s) : hash(h), size(static_cast<uint32_t>(s)) {}

            constexpr eastl::string_view ToStringView() const noexcept {
                return {&data[0], static_cast<size_t>(size)};
            }
        };

        template <size_t N>
        struct FixedString {
            char data[N];
            consteval FixedString(const char (&str)[N]) {
                for (size_t i = 0; i < N; ++i)
                    data[i] = str[i];
            }
        };

        template <size_t N>
        struct StaticPoolStringEntry {
            const uint64_t hash;
            PoolStringEntry* nextEntry = nullptr;
            const uint32_t size;
            char data[N];

            consteval StaticPoolStringEntry(FixedString<N> fs) 
                : hash(String::GetHash(eastl::string_view(fs.data, N - 1))), size(N - 1) {
                for (size_t i = 0; i < N; ++i)
                    data[i] = fs.data[i];
            }

            constexpr const PoolStringEntry* AsEntry() const {
                return reinterpret_cast<const PoolStringEntry*>(this);
            }
        };

    }  // namespace Details

    class PoolString {
    public:
        PoolString() noexcept : _entry(&_empty) {}

        static PoolString Intern(eastl::string_view str);
        static PoolString Find(eastl::string_view str);

        const char* CStr() const noexcept {
            return &_entry->data[0];
        }
        eastl::string_view ToStringView() const noexcept {
            return _entry->ToStringView();
        }
        uint64_t HashValue() const noexcept {
            return _entry->hash;
        }

        bool operator==(const PoolString& other) const noexcept {
            return _entry == other._entry;
        }
        bool operator!=(const PoolString& other) const noexcept {
            return _entry != other._entry;
        }
        bool operator<(const PoolString& other) const noexcept {
            return _entry < other._entry;
        }

        explicit operator bool() const noexcept {
            return _entry->size > 0;
        }

        static bool RegisterStatic(const Details::PoolStringEntry* entry);

        // Прозрачный компаратор для использования в контейнерах EASTL/std
        struct FastHash {
            using is_transparent = void;
            size_t operator()(PoolString s) const {
                return static_cast<size_t>(s.HashValue());
            }
            size_t operator()(eastl::string_view sv) const {
                return static_cast<size_t>(String::GetHash(sv));
            }
        };

    private:
        class Storage;
        friend class Storage;
        template <Details::FixedString>
        friend class StaticPoolString;
        using Entry = Details::PoolStringEntry;
        explicit PoolString(const Entry* entry) noexcept : _entry(entry) {}

        static inline const Entry _empty{String::GetEmptyHash(), 0};
        const Entry* _entry;
    };

    // C++23: Объявление compile-time строки: inline constexpr auto MyStr = StaticPoolString<"Hello">::Get();
    template <Details::FixedString Str>
    class StaticPoolString {
        static constexpr Details::StaticPoolStringEntry<sizeof(Str)> _entry{Str};
        static inline const bool _registered = PoolString::RegisterStatic(_entry.AsEntry());

    public:
        static constexpr PoolString Get() noexcept {
            (void)_registered;
            return PoolString(_entry.AsEntry());
        }
        operator PoolString() const noexcept {
            return Get();
        }
    };

}  // namespace Core
