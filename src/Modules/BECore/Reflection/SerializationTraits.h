#pragma once

#include <EASTL/array.h>
#include <EASTL/optional.h>

namespace BECore {

    // =========================================================================
    // DefaultChecker<T> -- determines whether a value is "default" (skippable)
    // =========================================================================

    template <typename T, typename = void>
    struct DefaultChecker {
        static constexpr bool AlwaysFalse = true;
        static bool IsDefault(const T&) { return false; }
    };

    // Arithmetic types: default is T{}
    template <typename T>
    struct DefaultChecker<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
        static constexpr bool AlwaysFalse = false;
        static bool IsDefault(const T& v) { return v == T{}; }
    };

    // Enum types: default is T{}
    template <typename T>
    struct DefaultChecker<T, std::enable_if_t<std::is_enum_v<T>>> {
        static constexpr bool AlwaysFalse = false;
        static bool IsDefault(const T& v) { return v == T{}; }
    };

    // Containers with .empty() and .clear() (eastl::string, eastl::vector, eastl::unordered_map, etc.)
    // Requires clear() to exclude fixed-size containers (eastl::array, std::array) which have empty() but not clear().
    template <typename T>
    struct DefaultChecker<T, std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_enum_v<T>
                                              && requires(const T& t) { { t.empty() } -> std::convertible_to<bool>; }
                                              && requires(T& t) { t.clear(); }>> {
        static constexpr bool AlwaysFalse = false;
        static bool IsDefault(const T& v) { return v.empty(); }
    };

    // PoolString
    template <>
    struct DefaultChecker<PoolString> {
        static constexpr bool AlwaysFalse = false;
        static bool IsDefault(const PoolString& v) { return v.Empty(); }
    };

    // IntrusivePtrAtomic
    template <typename T>
    struct DefaultChecker<IntrusivePtrAtomic<T>> {
        static constexpr bool AlwaysFalse = false;
        static bool IsDefault(const IntrusivePtrAtomic<T>& v) { return !v; }
    };

    // IntrusivePtrNonAtomic
    template <typename T>
    struct DefaultChecker<IntrusivePtrNonAtomic<T>> {
        static constexpr bool AlwaysFalse = false;
        static bool IsDefault(const IntrusivePtrNonAtomic<T>& v) { return !v; }
    };

    // eastl::optional<T>
    template <typename T>
    struct DefaultChecker<eastl::optional<T>> {
        static constexpr bool AlwaysFalse = false;
        static bool IsDefault(const eastl::optional<T>& v) { return !v.has_value(); }
    };

    // std::optional<T>
    template <typename T>
    struct DefaultChecker<std::optional<T>> {
        static constexpr bool AlwaysFalse = false;
        static bool IsDefault(const std::optional<T>& v) { return !v.has_value(); }
    };

    // eastl::pair<A, B>
    template <typename A, typename B>
    struct DefaultChecker<eastl::pair<A, B>> {
        static constexpr bool AlwaysFalse =
            DefaultChecker<A>::AlwaysFalse || DefaultChecker<B>::AlwaysFalse;
        static bool IsDefault(const eastl::pair<A, B>& v) {
            return DefaultChecker<A>::IsDefault(v.first)
                && DefaultChecker<B>::IsDefault(v.second);
        }
    };

    // std::pair<A, B>
    template <typename A, typename B>
    struct DefaultChecker<std::pair<A, B>> {
        static constexpr bool AlwaysFalse =
            DefaultChecker<A>::AlwaysFalse || DefaultChecker<B>::AlwaysFalse;
        static bool IsDefault(const std::pair<A, B>& v) {
            return DefaultChecker<A>::IsDefault(v.first)
                && DefaultChecker<B>::IsDefault(v.second);
        }
    };

    // eastl::array<T, N>
    template <typename T, size_t N>
    struct DefaultChecker<eastl::array<T, N>> {
        static constexpr bool AlwaysFalse = DefaultChecker<T>::AlwaysFalse;
        static bool IsDefault(const eastl::array<T, N>& arr) {
            for (const auto& v : arr) {
                if (!DefaultChecker<T>::IsDefault(v))
                    return false;
            }
            return true;
        }
    };

    // std::array<T, N>
    template <typename T, size_t N>
    struct DefaultChecker<std::array<T, N>> {
        static constexpr bool AlwaysFalse = DefaultChecker<T>::AlwaysFalse;
        static bool IsDefault(const std::array<T, N>& arr) {
            for (const auto& v : arr) {
                if (!DefaultChecker<T>::IsDefault(v))
                    return false;
            }
            return true;
        }
    };

    // =========================================================================
    // DefaultMaker<T> -- resets a value to its default state
    // =========================================================================

    template <typename T, typename = void>
    struct DefaultMaker {
        static constexpr bool HasMakeDefault = false;
        static void MakeDefault(T&) {}
    };

    // Arithmetic types
    template <typename T>
    struct DefaultMaker<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(T& v) { v = T{}; }
    };

    // Enum types
    template <typename T>
    struct DefaultMaker<T, std::enable_if_t<std::is_enum_v<T>>> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(T& v) { v = T{}; }
    };

    // Containers with .clear() (eastl::string, eastl::vector, eastl::unordered_map, etc.)
    template <typename T>
    struct DefaultMaker<T, std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_enum_v<T>
                                            && requires(T& t) { t.clear(); }>> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(T& v) { v.clear(); }
    };

    // PoolString
    template <>
    struct DefaultMaker<PoolString> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(PoolString& v) { v = PoolString{}; }
    };

    // IntrusivePtrAtomic
    template <typename T>
    struct DefaultMaker<IntrusivePtrAtomic<T>> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(IntrusivePtrAtomic<T>& v) { v.Reset(); }
    };

    // IntrusivePtrNonAtomic
    template <typename T>
    struct DefaultMaker<IntrusivePtrNonAtomic<T>> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(IntrusivePtrNonAtomic<T>& v) { v.Reset(); }
    };

    // eastl::optional<T>
    template <typename T>
    struct DefaultMaker<eastl::optional<T>> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(eastl::optional<T>& v) { v = eastl::nullopt; }
    };

    // std::optional<T>
    template <typename T>
    struct DefaultMaker<std::optional<T>> {
        static constexpr bool HasMakeDefault = true;
        static void MakeDefault(std::optional<T>& v) { v = std::nullopt; }
    };

    // eastl::array<T, N> -- reset all elements
    template <typename T, size_t N>
    struct DefaultMaker<eastl::array<T, N>> {
        static constexpr bool HasMakeDefault = DefaultMaker<T>::HasMakeDefault;
        static void MakeDefault(eastl::array<T, N>& arr) {
            for (auto& v : arr)
                DefaultMaker<T>::MakeDefault(v);
        }
    };

    // std::array<T, N> -- reset all elements
    template <typename T, size_t N>
    struct DefaultMaker<std::array<T, N>> {
        static constexpr bool HasMakeDefault = DefaultMaker<T>::HasMakeDefault;
        static void MakeDefault(std::array<T, N>& arr) {
            for (auto& v : arr)
                DefaultMaker<T>::MakeDefault(v);
        }
    };

}  // namespace BECore
