#pragma once

namespace Core {
    namespace Details {
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
    }  // namespace Details

    template <Details::FixedString Str>
    class StaticPoolString {
    public:
        static PoolString ToPoolString() noexcept {
            static PoolString cached = PoolString::Intern(Str.View());
            return cached;
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
                return Core::String::GetHash(Str.View()) == other.HashValue();
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
            return other < *this;
        }

        constexpr bool operator<=(const PoolString& other) const noexcept {
            return !(*this > other);
        }

        constexpr bool operator>=(const PoolString& other) const noexcept {
            return !(*this < other);
        }

        [[nodiscard]] consteval uint64_t HashValue() const noexcept {
            return Core::String::GetHash(Str.View());
        }

        [[nodiscard]] constexpr eastl::string_view View() const noexcept {
            return Str.View();
        }
    };

    // Обратные операторы сравнения: PoolString op StaticPoolString
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

}  // namespace Core
