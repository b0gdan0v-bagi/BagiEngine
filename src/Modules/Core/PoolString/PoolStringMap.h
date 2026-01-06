#pragma once

#include <EASTL/unordered_map.h>

// Специализация eastl::hash для StaticPoolString
namespace eastl {

    template <Core::Details::FixedString Str>
    struct hash<Core::StaticPoolString<Str>> {
        constexpr size_t operator()(const Core::StaticPoolString<Str>& sps) const noexcept {
            return static_cast<size_t>(sps.HashValue());
        }
    };

    template <>
    struct hash<Core::PoolString> {
        size_t operator()(const Core::PoolString& ps) const noexcept {
            return static_cast<size_t>(ps.HashValue());
        }
    };

}  // namespace eastl

// Структуры сравнения для использования в map/unordered_map
namespace Core {

    // Хешер, поддерживающий разные типы
    struct PoolStringHasher {
        size_t operator()(const PoolString& s) const {
            return eastl::hash<eastl::string_view>{}(s.ToStringView());
        }
        size_t operator()(const eastl::string_view& s) const {
            return eastl::hash<eastl::string_view>{}(s);
        }
    };

    struct PoolStringEquality {
        bool operator()(const PoolString& a, const PoolString& b) const {
            return a.ToStringView() == b.ToStringView();
        }
        bool operator()(const PoolString& a, const eastl::string_view& b) const {
            return a.ToStringView() == b;
        }
        bool operator()(const eastl::string_view& a, const PoolString& b) const {
            return a == b.ToStringView();
        }
    };

    // Для PoolString
    struct PoolStringEqual {
        constexpr bool operator()(const PoolString& lhs, const PoolString& rhs) const noexcept {
            return lhs == rhs;
        }
    };

    // Для StaticPoolString (шаблонная версия)
    template <Details::FixedString Str>
    struct StaticPoolStringEqual {
        template <Details::FixedString OtherStr>
        constexpr bool operator()(const StaticPoolString<Str>& lhs, const StaticPoolString<OtherStr>& rhs) const noexcept {
            return lhs == rhs;
        }

        constexpr bool operator()(const StaticPoolString<Str>& lhs, const PoolString& rhs) const noexcept {
            return lhs == rhs;
        }

        constexpr bool operator()(const PoolString& lhs, const StaticPoolString<Str>& rhs) const noexcept {
            return rhs == lhs;
        }
    };

    template <typename T>
    using UnorderedPoolMap = eastl::unordered_map<PoolString, T, PoolStringHasher, PoolStringEquality>;

}  // namespace Core

// Специализация eastl::equal_to для PoolString
namespace eastl {

    template <>
    struct equal_to<Core::PoolString> {
        constexpr bool operator()(const Core::PoolString& lhs, const Core::PoolString& rhs) const noexcept {
            return lhs == rhs;
        }
    };

    template <Core::Details::FixedString Str>
    struct equal_to<Core::StaticPoolString<Str>> {
        template <Core::Details::FixedString OtherStr>
        constexpr bool operator()(const Core::StaticPoolString<Str>& lhs, const Core::StaticPoolString<OtherStr>& rhs) const noexcept {
            return lhs == rhs;
        }

        constexpr bool operator()(const Core::StaticPoolString<Str>& lhs, const Core::PoolString& rhs) const noexcept {
            return lhs == rhs;
        }

        constexpr bool operator()(const Core::PoolString& lhs, const Core::StaticPoolString<Str>& rhs) const noexcept {
            return rhs == lhs;
        }
    };

}  // namespace eastl
