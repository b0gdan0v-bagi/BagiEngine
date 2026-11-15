#pragma once

#include <BECore/Types/Types.h>
#include <utility>

namespace BECore {

    /**
     * @brief A reflected method: its name, the pointer-to-member that invokes it and the names of its parameters
     *
     * `paramNames` is the comma separated list the generator reads from the declaration (`amount,label`); it is empty when the
     * declaration names none, and the type registry then calls the parameters `arg0`, `arg1`, ...
     *
     * @example
     * MethodInfo<MyClass, void> updateInfo{ "Update", &MyClass::Update };
     * updateInfo.Invoke(obj);
     */
    template <typename Class, typename Ret, typename... Args>
    struct MethodInfo {
        StringView name;
        Ret (Class::*ptr)(Args...);
        StringView paramNames{};

        constexpr Ret Invoke(Class& obj, Args... args) const {
            return (obj.*ptr)(std::forward<Args>(args)...);
        }
    };

    /// MethodInfo for const methods.
    template <typename Class, typename Ret, typename... Args>
    struct ConstMethodInfo {
        StringView name;
        Ret (Class::*ptr)(Args...) const;
        StringView paramNames{};

        constexpr Ret Invoke(const Class& obj, Args... args) const {
            return (obj.*ptr)(std::forward<Args>(args)...);
        }
    };

    template <typename Class, typename Ret, typename... Args>
    MethodInfo(StringView, Ret (Class::*)(Args...)) -> MethodInfo<Class, Ret, Args...>;

    template <typename Class, typename Ret, typename... Args>
    MethodInfo(StringView, Ret (Class::*)(Args...), StringView) -> MethodInfo<Class, Ret, Args...>;

    template <typename Class, typename Ret, typename... Args>
    ConstMethodInfo(StringView, Ret (Class::*)(Args...) const) -> ConstMethodInfo<Class, Ret, Args...>;

    template <typename Class, typename Ret, typename... Args>
    ConstMethodInfo(StringView, Ret (Class::*)(Args...) const, StringView) -> ConstMethodInfo<Class, Ret, Args...>;

}  // namespace BECore
