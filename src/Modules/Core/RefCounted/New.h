#pragma once

#include "IntrusivePtr.h"
#include "RefCounted.h"
#include <type_traits>
#include <tuple>

namespace Core {
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

    /**
     * Создает новый объект через new и автоматически определяет тип указателя
     * на основе базового класса (RefCounted/RefCountedAtomic -> атомарный, RefCountedNonAtomic -> неатомарный)
     * @tparam T Тип объекта (должен наследоваться от RefCounted или RefCountedNonAtomic)
     * @tparam Args Типы аргументов конструктора
     * @param args Аргументы конструктора
     * @return IntrusivePtrAtomic или IntrusivePtrNonAtomic в зависимости от базового класса
     */
    template <typename T, typename... Args>
    typename GetIntrusivePtrType<T>::type New(Args&&... args) {
        return IntrusivePtrNonAtomic<T>(new T(std::forward<Args>(args)...));
    }

    /**
     * Создает новый объект через new и возвращает IntrusivePtrAtomic
     * @tparam T Тип объекта (должен наследоваться от RefCountedAtomic)
     * @tparam Args Типы аргументов конструктора
     * @param args Аргументы конструктора
     * @return IntrusivePtrAtomic, владеющий объектом
     */
    template <typename T, typename... Args>
    IntrusivePtrAtomic<T> NewAtomic(Args&&... args) {
        return IntrusivePtrAtomic<T>(new T(std::forward<Args>(args)...));
    }
}

