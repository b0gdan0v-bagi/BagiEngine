#pragma once

#include <BECore/Reflection/FieldAttributes.h>
#include <BECore/Reflection/MethodInfo.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <EASTL/tuple.h>
#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace BECore {

    /**
     * @brief A reflected field: its name, the pointer-to-member and the attributes of `BE_FIELD_ATTRS(...)`
     * (`FieldAttributes{}` when the field has none).
     */
    template <typename Class, typename T>
    struct FieldInfo {
        StringView name;
        T Class::* ptr;
        FieldAttributes attributes;

        constexpr FieldInfo(StringView n, T Class::* p, const FieldAttributes& a = {}) noexcept : name(n), ptr(p), attributes(a) {}
    };

    template <typename Class, typename T>
    FieldInfo(StringView, T Class::*) -> FieldInfo<Class, T>;

    template <typename Class, typename T>
    FieldInfo(StringView, T Class::*, FieldAttributes) -> FieldInfo<Class, T>;

    /**
     * @brief Reflection traits; `reflected` is false unless the generated code specializes it for a `BE_CLASS` / `BE_EVENT` type
     * (`name`, `purpose`, `fields`: a tuple of FieldInfo, `methods`: a tuple of MethodInfo / ConstMethodInfo).
     *
     * @note The default argument is in the forward declaration in ReflectionMarkers.h.
     */
    template <typename T, typename>
    struct ReflectionTraits {
        static constexpr bool reflected = false;
    };

    /**
     * @brief A type with generated reflection traits.
     */
    template <typename T>
    concept HasReflection = requires {
        {ReflectionTraits<T>::reflected}->std::convertible_to<bool>;
        requires ReflectionTraits<T>::reflected == true;
        {ReflectionTraits<T>::name}->std::convertible_to<StringView>;
        ReflectionTraits<T>::fields;
    };

    template <typename T>
    requires HasReflection<T> constexpr size_t FieldCount() {
        return std::tuple_size_v<decltype(ReflectionTraits<T>::fields)>;
    }

    namespace Detail {
        template <typename T, typename = void>
        struct ReflectedParent {
            using type = void;
        };

        template <typename T>
        struct ReflectedParent<T, std::void_t<typename ReflectionTraits<T>::Parent>> {
            using type = typename ReflectionTraits<T>::Parent;
        };
    }  // namespace Detail

    /**
     * @brief The reflected (`BE_CLASS`) parent the generator wrote as `ReflectionTraits<T>::Parent`; `void` when the class has none.
     */
    template <typename T>
    using ReflectedParentOf = typename Detail::ReflectedParent<T>::type;

    /**
     * @brief Whether a reflected type has a field of this name (the C++ name, with its underscore), at compile time.
     *
     * @example
     * static_assert(HasField<Player>("health"));
     * static_assert(!HasField<Player>("mana"));
     */
    template <typename T>
    requires HasReflection<T> consteval bool HasField(StringView name) {
        bool found = false;
        std::apply([&](auto&&... fields) { ((found = found || (fields.name == name)), ...); }, ReflectionTraits<T>::fields);
        return found;
    }

    /**
     * @brief The FieldInfo of field @p I, in declaration order.
     *
     * @example
     * static_assert(GetFieldInfo<Player, 0>().name == "health");
     */
    template <typename T, size_t I>
        requires HasReflection<T> && (I < FieldCount<T>()) constexpr auto GetFieldInfo() {
        return std::get<I>(ReflectionTraits<T>::fields);
    }

    /**
     * @brief Calls @p func(name, value reference) for every reflected field, in declaration order.
     */
    template <typename T, typename Func>
    requires HasReflection<T> constexpr void ForEachField(T& obj, Func&& func) {
        std::apply([&](auto&&... fields) { (func(fields.name, obj.*(fields.ptr)), ...); }, ReflectionTraits<T>::fields);
    }

    template <typename T, typename Func>
    requires HasReflection<T> constexpr void ForEachField(const T& obj, Func&& func) {
        std::apply([&](auto&&... fields) { (func(fields.name, obj.*(fields.ptr)), ...); }, ReflectionTraits<T>::fields);
    }

    /**
     * @brief A reflected type with `ReflectionTraits<T>::methods` (an empty tuple when no method is marked `BE_FUNCTION`).
     */
    template <typename T>
    concept HasMethodReflection = requires {
        {ReflectionTraits<T>::reflected}->std::convertible_to<bool>;
        requires ReflectionTraits<T>::reflected == true;
        ReflectionTraits<T>::methods;
    };

    template <typename T>
    requires HasMethodReflection<T> constexpr size_t MethodCount() {
        return std::tuple_size_v<decltype(ReflectionTraits<T>::methods)>;
    }

    /**
     * @brief Whether a reflected type has a `BE_FUNCTION` method of this name, at compile time.
     *
     * @example
     * static_assert(HasMethod<Player>("TakeDamage"));
     * static_assert(!HasMethod<Player>("Fly"));
     */
    template <typename T>
    requires HasMethodReflection<T> consteval bool HasMethod(StringView name) {
        bool found = false;
        std::apply([&](auto&&... methods) { ((found = found || (methods.name == name)), ...); }, ReflectionTraits<T>::methods);
        return found;
    }

    /**
     * @brief The MethodInfo or ConstMethodInfo of method @p I, in declaration order.
     *
     * @example
     * static_assert(GetMethodInfo<Player, 0>().name == "TakeDamage");
     */
    template <typename T, size_t I>
        requires HasMethodReflection<T> && (I < MethodCount<T>()) constexpr auto GetMethodInfo() {
        return std::get<I>(ReflectionTraits<T>::methods);
    }

    /**
     * @brief Derived is a reflected class derived from the reflected Base.
     *
     * @example
     * static_assert(ReflectedDerivedFrom<ConsoleSink, ILogSink>);
     */
    template <typename Derived, typename Base>
    concept ReflectedDerivedFrom = HasReflection<Derived> && HasReflection<Base> && std::is_base_of_v<Base, Derived>;

    /**
     * @brief Calls @p func(MethodInfo or ConstMethodInfo) for every reflected method.
     */
    template <typename T, typename Func>
    requires HasMethodReflection<T> constexpr void ForEachMethod(Func&& func) {
        std::apply([&](auto&&... methods) { (func(methods), ...); }, ReflectionTraits<T>::methods);
    }

    namespace Detail {
        /// A method whose signature cannot take @p args is skipped at compile time, so a name shared by such methods is no error
        template <typename Ret, typename Method, typename T, typename... Args>
        constexpr bool TryInvokeOne(Method& method, T& obj, StringView name, Ret& result, Args&&... args) {
            using MethodPtr = decltype(method.ptr);
            if constexpr (std::is_invocable_r_v<Ret, MethodPtr, T&, Args...>) {
                if (method.name == name) {
                    result = (obj.*(method.ptr))(std::forward<Args>(args)...);
                    return true;
                }
            }
            return false;
        }

        template <typename Method, typename T, typename... Args>
        constexpr bool TryInvokeOneVoid(Method& method, T& obj, StringView name, Args&&... args) {
            using MethodPtr = decltype(method.ptr);
            if constexpr (std::is_invocable_v<MethodPtr, T&, Args...>) {
                if (method.name == name) {
                    (obj.*(method.ptr))(std::forward<Args>(args)...);
                    return true;
                }
            }
            return false;
        }

        template <typename Ret, typename T, typename... Args, typename MethodTuple, size_t... Is>
        constexpr Ret InvokeMethodImpl(T& obj, StringView name, MethodTuple& methods, std::index_sequence<Is...>, Args&&... args) {
            Ret result{};
            (TryInvokeOne(std::get<Is>(methods), obj, name, result, std::forward<Args>(args)...) || ...);
            return result;
        }

        template <typename T, typename... Args, typename MethodTuple, size_t... Is>
        constexpr void InvokeMethodVoidImpl(T& obj, StringView name, MethodTuple& methods, std::index_sequence<Is...>, Args&&... args) {
            (TryInvokeOneVoid(std::get<Is>(methods), obj, name, std::forward<Args>(args)...) || ...);
        }
    }  // namespace Detail

    /**
     * @brief Invoke the first reflected method named @p name that can take @p args and return @p Ret.
     * @return Its result, or a value-initialized @p Ret when no method matches (no error is reported).
     *
     * @example
     * // For a class with BE_FUNCTION void Update():
     * InvokeMethod<void>(obj, "Update");
     *
     * // For a class with BE_FUNCTION bool Initialize(IDeserializer& deserializer):
     * bool result = InvokeMethod<bool>(obj, "Initialize", deserializer);
     */
    template <typename Ret, typename T, typename... Args>
    requires HasMethodReflection<T> constexpr Ret InvokeMethod(T& obj, StringView name, Args&&... args) {
        constexpr auto& methods = ReflectionTraits<T>::methods;
        constexpr size_t count = std::tuple_size_v<std::decay_t<decltype(methods)>>;

        if constexpr (std::is_void_v<Ret>) {
            Detail::InvokeMethodVoidImpl(obj, name, methods, std::make_index_sequence<count>{}, std::forward<Args>(args)...);
        } else {
            return Detail::InvokeMethodImpl<Ret>(obj, name, methods, std::make_index_sequence<count>{}, std::forward<Args>(args)...);
        }
    }

}  // namespace BECore
