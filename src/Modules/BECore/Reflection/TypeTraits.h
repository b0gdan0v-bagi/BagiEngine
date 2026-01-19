#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/MethodInfo.h>
#include <EASTL/string_view.h>
#include <EASTL/tuple.h>
#include <type_traits>
#include <cstddef>
#include <utility>

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
     * @note Default argument is specified in ReflectionMarkers.h forward declaration
     */
    template <typename T, typename>
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

    // =========================================================================
    // Method Reflection Helpers
    // =========================================================================

    /**
     * @brief Concept to check if a type has method reflection
     *
     * Types with BE_FUNCTION marked methods will have ReflectionTraits<T>::methods
     */
    template <typename T>
    concept HasMethodReflection = requires {
        { ReflectionTraits<T>::reflected } -> std::convertible_to<bool>;
        requires ReflectionTraits<T>::reflected == true;
        ReflectionTraits<T>::methods;
    };

    /**
     * @brief Helper to get the number of reflected methods
     *
     * @tparam T The reflected type
     * @return Number of methods in the reflection traits
     */
    template <typename T>
        requires HasMethodReflection<T>
    constexpr size_t MethodCount() {
        return std::tuple_size_v<decltype(ReflectionTraits<T>::methods)>;
    }

    /**
     * @brief Helper to iterate over all reflected methods
     *
     * @tparam T The reflected type
     * @tparam Func Callable type
     * @param func Function to call for each method (receives MethodInfo)
     */
    template <typename T, typename Func>
        requires HasMethodReflection<T>
    constexpr void ForEachMethod(Func&& func) {
        std::apply([&](auto&&... methods) {
            (func(methods), ...);
        }, ReflectionTraits<T>::methods);
    }

    namespace Detail {
        /**
         * @brief Try to invoke a single method if signature matches
         * Uses if constexpr to skip methods with incompatible signatures at compile time
         */
        template <typename Ret, typename Method, typename T, typename... Args>
        bool TryInvokeOne(Method& method, T& obj, eastl::string_view name, Ret& result, Args&&... args) {
            using MethodPtr = decltype(method.ptr);
            if constexpr (std::is_invocable_r_v<Ret, MethodPtr, T&, Args...>) {
                if (method.name == name) {
                    result = (obj.*(method.ptr))(std::forward<Args>(args)...);
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief Try to invoke a single void method if signature matches
         */
        template <typename Method, typename T, typename... Args>
        bool TryInvokeOneVoid(Method& method, T& obj, eastl::string_view name, Args&&... args) {
            using MethodPtr = decltype(method.ptr);
            if constexpr (std::is_invocable_v<MethodPtr, T&, Args...>) {
                if (method.name == name) {
                    (obj.*(method.ptr))(std::forward<Args>(args)...);
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief Helper to find and invoke a method by name
         */
        template <typename Ret, typename T, typename... Args, typename MethodTuple, size_t... Is>
        Ret InvokeMethodImpl(T& obj, eastl::string_view name, MethodTuple& methods,
                             std::index_sequence<Is...>, Args&&... args) {
            Ret result{};
            // Fold expression with short-circuit - stops at first match
            (TryInvokeOne(std::get<Is>(methods), obj, name, result, std::forward<Args>(args)...) || ...);
            return result;
        }

        /**
         * @brief Helper for void return type
         */
        template <typename T, typename... Args, typename MethodTuple, size_t... Is>
        void InvokeMethodVoidImpl(T& obj, eastl::string_view name, MethodTuple& methods,
                                  std::index_sequence<Is...>, Args&&... args) {
            // Fold expression with short-circuit - stops at first match
            (TryInvokeOneVoid(std::get<Is>(methods), obj, name, std::forward<Args>(args)...) || ...);
        }
    }  // namespace Detail

    /**
     * @brief Invoke a reflected method by name
     *
     * @tparam Ret The expected return type
     * @tparam Args The argument types
     * @tparam T The object type
     * @param obj The object to invoke the method on
     * @param name The name of the method to invoke
     * @param args The arguments to pass to the method
     * @return The return value of the method
     *
     * @example
     * // For a class with BE_FUNCTION void Update():
     * InvokeMethod<void>(obj, "Update");
     *
     * // For a class with BE_FUNCTION bool Initialize(const XmlNode& node):
     * bool result = InvokeMethod<bool>(obj, "Initialize", node);
     */
    template <typename Ret, typename T, typename... Args>
        requires HasMethodReflection<T>
    Ret InvokeMethod(T& obj, eastl::string_view name, Args&&... args) {
        constexpr auto& methods = ReflectionTraits<T>::methods;
        constexpr size_t count = std::tuple_size_v<std::decay_t<decltype(methods)>>;
        
        if constexpr (std::is_void_v<Ret>) {
            Detail::InvokeMethodVoidImpl(obj, name, methods, 
                                         std::make_index_sequence<count>{},
                                         std::forward<Args>(args)...);
        } else {
            return Detail::InvokeMethodImpl<Ret>(obj, name, methods,
                                                  std::make_index_sequence<count>{},
                                                  std::forward<Args>(args)...);
        }
    }

}  // namespace BECore
