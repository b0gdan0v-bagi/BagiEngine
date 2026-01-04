#pragma once

#include <type_traits>

namespace Core {
    /**
     * Класс PassKey используется для обеспечения приватного доступа к конкретным классам.
     * Позволяет ограничить доступ к методам или конструкторам только для указанного типа
     * и его наследников.
     *
     * @tparam T Тип класса, которому разрешено создавать PassKey
     *
     * Пример использования:
     * class MyClass {
     * public:
     *     void PublicMethod() { ... }
     *
     *     // Метод доступен только для Manager и его наследников
     *     void PrivateMethod(PassKey<Manager>) { ... }
     * };
     *
     * class Manager {
     * public:
     *     void DoSomething(MyClass& obj) {
     *         obj.PrivateMethod(PassKey<Manager>{}); // OK
     *     }
     * };
     *
     * class DerivedManager : public Manager {
     * public:
     *     void DoSomething(MyClass& obj) {
     *         obj.PrivateMethod(PassKey<Manager>{}); // OK - наследник может создать PassKey<Manager>
     *     }
     * };
     *
     * // В другом месте:
     * MyClass obj;
     * obj.PrivateMethod(PassKey<Manager>{}); // Ошибка компиляции - только Manager и его наследники могут создать PassKey<Manager>
     */
    template <typename T>
    class PassKey {
        // Разрешаем доступ для самого T
        friend T;

        // Вспомогательный класс для проверки наследования
        template <typename U>
        using IsDerivedFrom = std::bool_constant<std::is_base_of_v<T, U> || std::is_same_v<T, U>>;

    public:
        PassKey() = default;
        PassKey(const PassKey&) = default;
        PassKey& operator=(const PassKey&) = default;
        PassKey(PassKey&&) = default;
        PassKey& operator=(PassKey&&) = default;

        // Шаблонный конструктор, разрешающий создание для наследников T
        // Конструктор доступен только если U является T или наследником T
        template <typename U, typename = std::enable_if_t<IsDerivedFrom<U>::value>>
        PassKey(PassKey<U> const&) {}

        // Шаблонный конструктор перемещения для наследников T
        template <typename U, typename = std::enable_if_t<IsDerivedFrom<U>::value>>
        PassKey(PassKey<U>&&) {}
    };
}  // namespace Core

