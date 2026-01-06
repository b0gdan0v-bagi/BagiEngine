#pragma once

#include <EASTL/map.h>
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

    // Класс-обёртка для unordered_map с методом Find
    template <typename T>
    class UnorderedPoolMap : public eastl::unordered_map<PoolString, T, PoolStringHasher, PoolStringEquality> {
    public:
        using Base = eastl::unordered_map<PoolString, T, PoolStringHasher, PoolStringEquality>;
        using Base::Base;

        // Гетерогенный поиск без необходимости указывать функторы
        template <typename K>
        auto Find(const K& key) const {
            if constexpr (eastl::is_same_v<K, PoolString>) {
                return this->find(key);
            } else {
                return this->find_as(key, PoolStringHasher(), PoolStringEquality());
            }
        }
    };

    // Класс-обёртка для map с методом Find
    template <typename T>
    class PoolMap : public eastl::map<PoolString, T, eastl::less<PoolString>> {
    public:
        using Base = eastl::map<PoolString, T, eastl::less<PoolString>>;
        using Base::Base;

        // Гетерогенный поиск без необходимости указывать компаратор
        template <typename K>
        auto Find(const K& key) const {
            return this->find(key);
        }
    };

}  // namespace Core

// Специализация eastl::equal_to для PoolString
namespace eastl {

    template <>
    struct equal_to<Core::PoolString> {
        using is_transparent = void;  // Позволяет гетерогенный поиск
        
        constexpr bool operator()(const Core::PoolString& lhs, const Core::PoolString& rhs) const noexcept {
            return lhs == rhs;
        }
        constexpr bool operator()(const Core::PoolString& lhs, const eastl::string_view& rhs) const noexcept {
            return lhs.ToStringView() == rhs;
        }
        constexpr bool operator()(const eastl::string_view& lhs, const Core::PoolString& rhs) const noexcept {
            return lhs == rhs.ToStringView();
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

    // Специализация eastl::less для PoolString с поддержкой гетерогенного поиска
    template <>
    struct less<Core::PoolString> {
        using is_transparent = void;  // Позволяет гетерогенный поиск
        
        constexpr bool operator()(const Core::PoolString& lhs, const Core::PoolString& rhs) const noexcept {
            return lhs.ToStringView() < rhs.ToStringView();
        }
        constexpr bool operator()(const Core::PoolString& lhs, const eastl::string_view& rhs) const noexcept {
            return lhs.ToStringView() < rhs;
        }
        constexpr bool operator()(const eastl::string_view& lhs, const Core::PoolString& rhs) const noexcept {
            return lhs < rhs.ToStringView();
        }
    };

}  // namespace eastl
