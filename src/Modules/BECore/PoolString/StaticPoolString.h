#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringEntry.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/StringUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {
    namespace Details {
        template <size_t N>
        struct FixedString {
            char data[N]{};
            consteval FixedString(const char (&str)[N]) {
                for (size_t i = 0; i < N; ++i)
                    data[i] = str[i];
            }
            constexpr StringView View() const noexcept {
                return {data, N - 1};
            }
        };

        /// The bytes of a `PoolStringEntry` of a text of `N - 1` characters, built at compile time. `PoolStringEntry` itself
        /// holds one character, so a literal needs a type with the whole text in place; `StaticPoolString` checks the two lay
        /// out alike.
        template <size_t N>
        struct alignas(kPoolEntryAlignment) StaticEntryStore {
            const uint64_t hash;
            const uint32_t size;
            char text[N];

            constexpr explicit StaticEntryStore(const FixedString<N>& str) : hash(BECore::StringUtils::GetHash(str.View())), size(static_cast<uint32_t>(N - 1)), text{} {
                for (size_t i = 0; i < N; ++i)
                    text[i] = str.data[i];
            }
        };
    }  // namespace Details

    template <Details::FixedString Str>
    class StaticPoolString {
        using Store = Details::StaticEntryStore<sizeof(Str.data)>;

        // The pool reads the text through `PoolStringEntry::data`, as it does for entries in its blocks, so the store must be
        // that layout: hash, size, the text at offset 12, the whole padded to the alignment of an entry
        static_assert(offsetof(Store, hash) == offsetof(Details::PoolStringEntry, hash));
        static_assert(offsetof(Store, size) == offsetof(Details::PoolStringEntry, size));
        static_assert(offsetof(Store, text) == offsetof(Details::PoolStringEntry, data));
        static_assert(sizeof(Store) == Details::PoolEntryBytes(sizeof(Str.data) - 1));
        static_assert(alignof(Store) == Details::kPoolEntryAlignment);

        /// One per distinct literal (the linker merges the copies of an inline variable), in read-only data: a constant costs
        /// the pool no memory
        static constexpr inline Store _store{Str};

    public:
        /// The `PoolString` of the literal. The first call registers the entry in the pool; if the text was in it already
        /// (from `Intern`, an adopted block or another ready entry), that older entry is the result.
        static PoolString ToPoolString() noexcept {
            static const PoolString cached = PoolString::RegisterStatic(Entry());
            return cached;
        }

        /// The literal's own entry, for registration and tests. It is not necessarily the entry the pool holds for the text
        /// (see `ToPoolString`), so a caller uses the `PoolString`, never this address, as the string.
        static const Details::PoolStringEntry& Entry() noexcept {
            // `PoolStringEntry.h` makes the layout of an entry a contract that blocks, cooks and this store all follow, so
            // reading the store through that type is the layout agreement, not a trick around aliasing
            return *reinterpret_cast<const Details::PoolStringEntry*>(&_store);
        }

        operator PoolString() const noexcept {
            return ToPoolString();
        }

        template <Details::FixedString OtherStr>
        constexpr bool operator==(StaticPoolString<OtherStr>) const noexcept {
            return Str.View() == OtherStr.View();
        }

        constexpr bool operator==(const PoolString& other) const noexcept {
            if consteval {
                return BECore::StringUtils::GetHash(Str.View()) == other.HashValue();
            }
            return ToPoolString() == other;
        }

        constexpr bool operator!=(const PoolString& other) const noexcept {
            return !(*this == other);
        }

        constexpr bool operator<(const PoolString& other) const noexcept {
            if consteval {
                return Str.View() < other.ToStringView();
            }
            return ToPoolString().ToStringView() < other.ToStringView();
        }

        constexpr bool operator>(const PoolString& other) const noexcept {
            // Not `other < *this`: that picks the free `operator<(PoolString, StaticPoolString)`, which calls back here
            return other.ToStringView() < View();
        }

        constexpr bool operator<=(const PoolString& other) const noexcept {
            return !(*this > other);
        }

        constexpr bool operator>=(const PoolString& other) const noexcept {
            return !(*this < other);
        }

        [[nodiscard]] consteval uint64_t HashValue() const noexcept {
            return BECore::StringUtils::GetHash(Str.View());
        }

        [[nodiscard]] constexpr StringView View() const noexcept {
            return Str.View();
        }
    };

    template <Details::FixedString Str>
    constexpr bool operator==(const PoolString& lhs, const StaticPoolString<Str>& rhs) noexcept {
        return rhs == lhs;
    }

    template <Details::FixedString Str>
    constexpr bool operator!=(const PoolString& lhs, const StaticPoolString<Str>& rhs) noexcept {
        return rhs != lhs;
    }

    template <Details::FixedString Str>
    constexpr bool operator<(const PoolString& lhs, const StaticPoolString<Str>& rhs) noexcept {
        return rhs > lhs;
    }

    template <Details::FixedString Str>
    constexpr bool operator>(const PoolString& lhs, const StaticPoolString<Str>& rhs) noexcept {
        return rhs < lhs;
    }

    template <Details::FixedString Str>
    constexpr bool operator<=(const PoolString& lhs, const StaticPoolString<Str>& rhs) noexcept {
        return rhs >= lhs;
    }

    template <Details::FixedString Str>
    constexpr bool operator>=(const PoolString& lhs, const StaticPoolString<Str>& rhs) noexcept {
        return rhs <= lhs;
    }

    template <Details::FixedString Str>
    consteval auto operator""_intern() {
        return StaticPoolString<Str>{};
    }

}  // namespace BECore
