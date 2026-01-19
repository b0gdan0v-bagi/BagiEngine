#pragma once

#include <EASTL/string_view.h>
#include <utility>

namespace BECore {

    /**
     * @brief Information about a reflected method
     *
     * Contains the method name and a pointer-to-member-function for invoking the method.
     * Supports methods with various signatures through template parameters.
     *
     * @tparam Class The class containing the method
     * @tparam Ret The return type of the method
     * @tparam Args The parameter types of the method
     *
     * @example
     * // For a method: void MyClass::Update()
     * MethodInfo<MyClass, void> updateInfo{ "Update", &MyClass::Update };
     *
     * // Invoke on an object
     * MyClass obj;
     * updateInfo.Invoke(obj);
     */
    template <typename Class, typename Ret, typename... Args>
    struct MethodInfo {
        eastl::string_view name;
        Ret (Class::*ptr)(Args...);

        /**
         * @brief Invoke the method on an object
         * @param obj The object to invoke the method on
         * @param args The arguments to pass to the method
         * @return The return value of the method
         */
        constexpr Ret Invoke(Class& obj, Args... args) const {
            return (obj.*ptr)(std::forward<Args>(args)...);
        }
    };

    /**
     * @brief MethodInfo for const methods
     *
     * @tparam Class The class containing the method
     * @tparam Ret The return type of the method
     * @tparam Args The parameter types of the method
     */
    template <typename Class, typename Ret, typename... Args>
    struct ConstMethodInfo {
        eastl::string_view name;
        Ret (Class::*ptr)(Args...) const;

        /**
         * @brief Invoke the const method on an object
         * @param obj The object to invoke the method on
         * @param args The arguments to pass to the method
         * @return The return value of the method
         */
        constexpr Ret Invoke(const Class& obj, Args... args) const {
            return (obj.*ptr)(std::forward<Args>(args)...);
        }
    };

    /**
     * @brief Helper to deduce MethodInfo type from pointer-to-member-function
     */
    template <typename Class, typename Ret, typename... Args>
    MethodInfo(eastl::string_view, Ret (Class::*)(Args...)) -> MethodInfo<Class, Ret, Args...>;

    /**
     * @brief Helper to deduce ConstMethodInfo type from const pointer-to-member-function
     */
    template <typename Class, typename Ret, typename... Args>
    ConstMethodInfo(eastl::string_view, Ret (Class::*)(Args...) const) -> ConstMethodInfo<Class, Ret, Args...>;

}  // namespace BECore
