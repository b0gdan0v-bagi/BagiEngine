#pragma once

namespace BECore {
    // Вспомогательная функция для определения типа указателя
    template <typename T>
    struct GetIntrusivePtrType {
        // Проверяем наследование от RefCountedAtomic напрямую
        // RefCounted - это алиас для RefCountedAtomic, но std::is_base_of_v работает с реальными типами
        // Проверяем только RefCountedAtomic, так как RefCounted - это просто алиас
        static constexpr bool IsAtomic = std::is_base_of_v<RefCountedAtomic, T>;
        using type = std::conditional_t<
            IsAtomic,
            IntrusivePtrAtomic<T>,
            IntrusivePtrNonAtomic<T>
        >;
    };

template <typename T, typename... Args>
    auto New(Args&&... args) {
        if constexpr (std::derived_from<T, RefCountedAtomic>) {
            return IntrusivePtrAtomic<T>(new T(std::forward<Args>(args)...));
        } else {
            return IntrusivePtrNonAtomic<T>(new T(std::forward<Args>(args)...));
        }
    }
}

