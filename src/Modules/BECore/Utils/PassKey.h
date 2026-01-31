#pragma once

#include <concepts>

namespace BECore {
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
     *         obj.PrivateMethod({}); // OK - используем {} для PassKey
     *     }
     * };
     *
     * class DerivedManager : public Manager {
     * public:
     *     void DoSomething(MyClass& obj) {
     *         obj.PrivateMethod({}); // OK - наследник может создать PassKey<Manager>
     *     }
     * };
     *
     * // В другом месте:
     * MyClass obj;
     * obj.PrivateMethod({}); // Ошибка компиляции - только Manager и его наследники могут создать PassKey<Manager>
     */
    template <typename T>
    class PassKey {
    protected:
        // Protected конструктор: доступен для T (через friend) и наследников T
        constexpr PassKey() noexcept = default;

    private:
        // friend даёт T доступ к protected конструктору
        friend T;

        // Разрешаем конструирование PassKey<U> для производных типов
        template <typename U>
        friend class PassKey;

        // Запрещаем копирование и присваивание
        PassKey(const PassKey&) = delete;
        PassKey& operator=(const PassKey&) = delete;
        PassKey& operator=(PassKey&&) = delete;

    public:
        // Move конструктор публичный для передачи в функции
        constexpr PassKey(PassKey&&) noexcept = default;

        // Конструктор для поддержки PassKey<Derived> -> PassKey<Base>
        template <typename U>
        requires (std::derived_from<U, T> || std::same_as<U, T>)
        constexpr PassKey(PassKey<U>&&) noexcept {}
    };
}  // namespace BECore

