#pragma once

#include <EASTL/string_view.h>
#include <EASTL/tuple.h>
#include <type_traits>
#include <cstddef>

namespace BECore {

    /**
     * @brief Information about a reflected field
     *
     * Contains the field name and a pointer-to-member for accessing the field.
     *
     * @tparam Class The class containing the field
     * @tparam T The type of the field
     */
    template <typename Class, typename T>
    struct FieldInfo {
        eastl::string_view name;
        T Class::*ptr;

        constexpr FieldInfo(eastl::string_view n, T Class::*p) noexcept
            : name(n), ptr(p) {}
    };

    /**
     * @brief Helper to deduce FieldInfo type from pointer-to-member
     */
    template <typename Class, typename T>
    FieldInfo(eastl::string_view, T Class::*) -> FieldInfo<Class, T>;

    /**
     * @brief Primary template for reflection traits (empty by default)
     *
     * Specialized by generated code for types marked with BE_REFLECT_CLASS.
     * Provides:
     * - static constexpr eastl::string_view name - the type name
     * - static constexpr auto fields - tuple of FieldInfo
     *
     * @tparam T The type to get reflection traits for
     */
    template <typename T, typename = void>
    struct ReflectionTraits {
        static constexpr bool reflected = false;
    };

    /**
     * @brief Concept to check if a type has reflection traits
     *
     * Used for SFINAE and if constexpr checks in serialization code.
     *
     * @example
     * template<typename T>
     * void Serialize(IArchive& archive, T& obj) {
     *     if constexpr (HasReflection<T>) {
     *         // Use ReflectionTraits<T>::fields
     *     }
     * }
     */
    template <typename T>
    concept HasReflection = requires {
        { ReflectionTraits<T>::reflected } -> std::convertible_to<bool>;
        requires ReflectionTraits<T>::reflected == true;
        { ReflectionTraits<T>::name } -> std::convertible_to<eastl::string_view>;
        ReflectionTraits<T>::fields;
    };

    /**
     * @brief Helper to get the number of reflected fields
     *
     * @tparam T The reflected type
     * @return Number of fields in the reflection traits
     */
    template <typename T>
        requires HasReflection<T>
    constexpr size_t FieldCount() {
        return std::tuple_size_v<decltype(ReflectionTraits<T>::fields)>;
    }

    /**
     * @brief Helper to iterate over all reflected fields
     *
     * @tparam T The reflected type
     * @tparam Func Callable type
     * @param obj The object to iterate over
     * @param func Function to call for each field (receives name and value reference)
     */
    template <typename T, typename Func>
        requires HasReflection<T>
    constexpr void ForEachField(T& obj, Func&& func) {
        std::apply([&](auto&&... fields) {
            (func(fields.name, obj.*(fields.ptr)), ...);
        }, ReflectionTraits<T>::fields);
    }

    /**
     * @brief Const version of ForEachField
     */
    template <typename T, typename Func>
        requires HasReflection<T>
    constexpr void ForEachField(const T& obj, Func&& func) {
        std::apply([&](auto&&... fields) {
            (func(fields.name, obj.*(fields.ptr)), ...);
        }, ReflectionTraits<T>::fields);
    }

}  // namespace BECore
