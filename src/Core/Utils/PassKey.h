#pragma once

namespace Core {
    /**
     * Класс PassKey используется для обеспечения приватного доступа к конкретным классам.
     * Позволяет ограничить доступ к методам или конструкторам только для указанного типа.
     *
     * @tparam T Тип класса, которому разрешено создавать PassKey
     *
     * Пример использования:
     * class MyClass {
     * public:
     *     void PublicMethod() { ... }
     *
     *     // Метод доступен только для Manager
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
     * // В другом месте:
     * MyClass obj;
     * obj.PrivateMethod(PassKey<Manager>{}); // Ошибка компиляции - только Manager может создать PassKey<Manager>
     */
    template <typename T>
    class PassKey {
        friend T;

    public:
        PassKey() = default;
        PassKey(const PassKey&) = default;
        PassKey& operator=(const PassKey&) = default;
        PassKey(PassKey&&) = default;
        PassKey& operator=(PassKey&&) = default;
    };
}  // namespace Core

