#pragma once

#include "RefCounted.h"

/**
 * Интрузивный умный указатель (неатомарная версия).
 * Управляет объектами, наследующимися от RefCountedNonAtomic.
 * Счетчик ссылок хранится внутри объекта, а не в указателе.
 * Не потокобезопасен, но более быстрый.
 * Используйте только в однопоточных приложениях или когда доступ
 * гарантированно происходит из одного потока.
 *
 * @tparam T Тип объекта (должен наследоваться от RefCountedNonAtomic)
 *
 * Пример использования:
 * class MyResource : public RefCountedNonAtomic {
 * public:
 *     void DoSomething() { ... }
 * };
 *
 * IntrusivePtrNonAtomic<MyResource> ptr1(new MyResource());
 * IntrusivePtrNonAtomic<MyResource> ptr2 = ptr1;  // Счетчик ссылок увеличивается
 * ptr1.Reset();  // Счетчик уменьшается, но объект не удаляется (ptr2 все еще владеет)
 * ptr2.Reset();  // Теперь объект удаляется
 */
template <typename T>
class IntrusivePtrNonAtomic {
    template <typename U>
    friend class IntrusivePtrNonAtomic;
public:
    /**
     * Конструктор по умолчанию (nullptr)
     */
    IntrusivePtrNonAtomic() : _ptr(nullptr) {}

    /**
     * Конструктор из сырого указателя
     * @param ptr Указатель на объект (может быть nullptr)
     */
    explicit IntrusivePtrNonAtomic(T* ptr) : _ptr(ptr) {
        if (_ptr != nullptr) {
            _ptr->AddRef();
        }
    }

    /**
     * Конструктор копирования
     * @param other Другой IntrusivePtrNonAtomic
     */
    IntrusivePtrNonAtomic(const IntrusivePtrNonAtomic& other) : _ptr(other._ptr) {
        if (_ptr != nullptr) {
            _ptr->AddRef();
        }
    }

    /**
     * Конструктор перемещения
     * @param other Другой IntrusivePtrNonAtomic (будет обнулен)
     */
    IntrusivePtrNonAtomic(IntrusivePtrNonAtomic&& other) noexcept : _ptr(other._ptr) {
        other._ptr = nullptr;
    }

    /**
     * Конструктор преобразования из производного типа (covariance)
     * @tparam U Производный тип (должен наследоваться от T)
     * @param other Другой IntrusivePtrNonAtomic с производным типом
     */
    template <typename U>
    IntrusivePtrNonAtomic(const IntrusivePtrNonAtomic<U>& other) 
        : _ptr(static_cast<T*>(other._ptr)) {
        if (_ptr != nullptr) {
            _ptr->AddRef();
        }
    }

    /**
     * Конструктор перемещения из производного типа (covariance)
     * @tparam U Производный тип (должен наследоваться от T)
     * @param other Другой IntrusivePtrNonAtomic с производным типом (будет обнулен)
     */
    template <typename U>
    IntrusivePtrNonAtomic(IntrusivePtrNonAtomic<U>&& other) noexcept 
        : _ptr(other._ptr) {
        static_assert(std::is_base_of_v<T, U>, "U must be derived from T");
        other._ptr = nullptr;
    }

    /**
     * Деструктор
     */
    ~IntrusivePtrNonAtomic() {
        Reset();
    }

    /**
     * Оператор присваивания
     * @param other Другой IntrusivePtrNonAtomic
     * @return Ссылка на себя
     */
    IntrusivePtrNonAtomic& operator=(const IntrusivePtrNonAtomic& other) {
        if (this != &other) {
            if (other._ptr != nullptr) {
                other._ptr->AddRef();
            }
            if (_ptr != nullptr) {
                _ptr->ReleaseRef();
            }
            _ptr = other._ptr;
        }
        return *this;
    }

    /**
     * Оператор перемещающего присваивания
     * @param other Другой IntrusivePtrNonAtomic (будет обнулен)
     * @return Ссылка на себя
     */
    IntrusivePtrNonAtomic& operator=(IntrusivePtrNonAtomic&& other) noexcept {
        if (this != &other) {
            if (_ptr != nullptr) {
                _ptr->ReleaseRef();
            }
            _ptr = other._ptr;
            other._ptr = nullptr;
        }
        return *this;
    }

    /**
     * Оператор присваивания из сырого указателя
     * @param ptr Сырой указатель
     * @return Ссылка на себя
     */
    IntrusivePtrNonAtomic& operator=(T* ptr) {
        if (ptr != nullptr) {
            ptr->AddRef();
        }
        if (_ptr != nullptr) {
            _ptr->ReleaseRef();
        }
        _ptr = ptr;
        return *this;
    }

    /**
     * Оператор присваивания из производного типа (covariance)
     * @tparam U Производный тип (должен наследоваться от T)
     * @param other Другой IntrusivePtrNonAtomic с производным типом
     * @return Ссылка на себя
     */
    template <typename U>
    IntrusivePtrNonAtomic& operator=(const IntrusivePtrNonAtomic<U>& other) {
        static_assert(std::is_base_of_v<T, U> || std::is_same_v<T, U>, "T must be base of U or same as U");
        T* ptr = static_cast<T*>(other._ptr);
        if (ptr != nullptr) {
            ptr->AddRef();
        }
        if (_ptr != nullptr) {
            _ptr->ReleaseRef();
        }
        _ptr = ptr;
        return *this;
    }

    /**
     * Оператор перемещающего присваивания из производного типа (covariance)
     * @tparam U Производный тип (должен наследоваться от T)
     * @param other Другой IntrusivePtrNonAtomic с производным типом (будет обнулен)
     * @return Ссылка на себя
     */
    template <typename U>
    IntrusivePtrNonAtomic& operator=(IntrusivePtrNonAtomic<U>&& other) noexcept {
        static_assert(std::is_base_of_v<T, U>, "U must be derived from T");
        if (_ptr != nullptr) {
            _ptr->ReleaseRef();
        }
        _ptr = other._ptr;
        other._ptr = nullptr;
        return *this;
    }

    /**
     * Оператор разыменования
     * @return Ссылка на объект
     */
    T& operator*() const {
        assert(_ptr != nullptr);
        return *_ptr;
    }

    /**
     * Оператор доступа к членам
     * @return Указатель на объект
     */
    T* operator->() const {
        assert(_ptr != nullptr);
        return _ptr;
    }

    /**
     * Получает сырой указатель
     * @return Сырой указатель или nullptr
     */
    T* Get() const {
        return _ptr;
    }

    /**
     * Проверяет, является ли указатель валидным
     * @return true если указатель не nullptr, false в противном случае
     */
    bool IsValid() const {
        return _ptr != nullptr;
    }

    /**
     * Оператор приведения к bool
     * @return true если указатель не nullptr, false в противном случае
     */
    explicit operator bool() const {
        return _ptr != nullptr;
    }

    /**
     * Сбрасывает указатель (уменьшает счетчик ссылок)
     */
    void Reset() {
        if (_ptr != nullptr) {
            _ptr->ReleaseRef();
            _ptr = nullptr;
        }
    }

    /**
     * Сбрасывает указатель и устанавливает новый
     * @param ptr Новый указатель (может быть nullptr)
     */
    void Reset(T* ptr) {
        if (ptr != nullptr) {
            ptr->AddRef();
        }
        if (_ptr != nullptr) {
            _ptr->ReleaseRef();
        }
        _ptr = ptr;
    }

    /**
     * Получает количество ссылок на объект
     * @return Количество ссылок или 0, если указатель nullptr
     */
    int32_t GetRefCount() const {
        return _ptr != nullptr ? _ptr->GetRefCount() : 0;
    }

    /**
     * Оператор сравнения на равенство
     */
    bool operator==(const IntrusivePtrNonAtomic& other) const {
        return _ptr == other._ptr;
    }

    /**
     * Оператор сравнения на неравенство
     */
    bool operator!=(const IntrusivePtrNonAtomic& other) const {
        return _ptr != other._ptr;
    }

    /**
     * Оператор сравнения с nullptr
     */
    bool operator==(std::nullptr_t) const {
        return _ptr == nullptr;
    }

    /**
     * Оператор сравнения с nullptr
     */
    bool operator!=(std::nullptr_t) const {
        return _ptr != nullptr;
    }

private:
    T* _ptr;
};

/**
 * Интрузивный умный указатель (атомарная версия).
 * Управляет объектами, наследующимися от RefCountedAtomic.
 * Счетчик ссылок хранится внутри объекта, а не в указателе.
 * Потокобезопасен, использует атомарные операции.
 * Используйте в многопоточных приложениях.
 *
 * @tparam T Тип объекта (должен наследоваться от RefCountedAtomic)
 *
 * Пример использования:
 * class MyResource : public RefCountedAtomic {
 * public:
 *     void DoSomething() { ... }
 * };
 *
 * IntrusivePtrAtomic<MyResource> ptr1(new MyResource());
 * IntrusivePtrAtomic<MyResource> ptr2 = ptr1;  // Счетчик ссылок увеличивается
 * ptr1.Reset();  // Счетчик уменьшается, но объект не удаляется (ptr2 все еще владеет)
 * ptr2.Reset();  // Теперь объект удаляется
 */
template <typename T>
class IntrusivePtrAtomic {
public:
    /**
     * Конструктор по умолчанию (nullptr)
     */
    IntrusivePtrAtomic() : _ptr(nullptr) {}

    /**
     * Конструктор из сырого указателя
     * @param ptr Указатель на объект (может быть nullptr)
     */
    explicit IntrusivePtrAtomic(T* ptr) : _ptr(ptr) {
        if (_ptr != nullptr) {
            _ptr->AddRef();
        }
    }

    /**
     * Конструктор копирования
     * @param other Другой IntrusivePtrAtomic
     */
    IntrusivePtrAtomic(const IntrusivePtrAtomic& other) : _ptr(other._ptr) {
        if (_ptr != nullptr) {
            _ptr->AddRef();
        }
    }

    /**
     * Конструктор перемещения
     * @param other Другой IntrusivePtrAtomic (будет обнулен)
     */
    IntrusivePtrAtomic(IntrusivePtrAtomic&& other) noexcept : _ptr(other._ptr) {
        other._ptr = nullptr;
    }

    /**
     * Конструктор преобразования из производного типа (covariance)
     * @tparam U Производный тип (должен наследоваться от T)
     * @param other Другой IntrusivePtrAtomic с производным типом
     */
    template <typename U>
    IntrusivePtrAtomic(const IntrusivePtrAtomic<U>& other) : _ptr(other.Get()) {
        if (_ptr != nullptr) {
            _ptr->AddRef();
        }
    }

    /**
     * Конструктор перемещения из производного типа (covariance)
     * @tparam U Производный тип (должен наследоваться от T)
     * @param other Другой IntrusivePtrAtomic с производным типом (будет обнулен)
     */
    template <typename U>
    IntrusivePtrAtomic(IntrusivePtrAtomic<U>&& other) noexcept : _ptr(other.Get()) {
        other.Reset();
    }

    /**
     * Деструктор
     */
    ~IntrusivePtrAtomic() {
        Reset();
    }

    /**
     * Оператор присваивания
     * @param other Другой IntrusivePtrAtomic
     * @return Ссылка на себя
     */
    IntrusivePtrAtomic& operator=(const IntrusivePtrAtomic& other) {
        if (this != &other) {
            if (other._ptr != nullptr) {
                other._ptr->AddRef();
            }
            if (_ptr != nullptr) {
                _ptr->ReleaseRef();
            }
            _ptr = other._ptr;
        }
        return *this;
    }

    /**
     * Оператор перемещающего присваивания
     * @param other Другой IntrusivePtrAtomic (будет обнулен)
     * @return Ссылка на себя
     */
    IntrusivePtrAtomic& operator=(IntrusivePtrAtomic&& other) noexcept {
        if (this != &other) {
            if (_ptr != nullptr) {
                _ptr->ReleaseRef();
            }
            _ptr = other._ptr;
            other._ptr = nullptr;
        }
        return *this;
    }

    /**
     * Оператор присваивания из сырого указателя
     * @param ptr Сырой указатель
     * @return Ссылка на себя
     */
    IntrusivePtrAtomic& operator=(T* ptr) {
        if (ptr != nullptr) {
            ptr->AddRef();
        }
        if (_ptr != nullptr) {
            _ptr->ReleaseRef();
        }
        _ptr = ptr;
        return *this;
    }

    /**
     * Оператор присваивания из производного типа (covariance)
     * @tparam U Производный тип (должен наследоваться от T)
     * @param other Другой IntrusivePtrAtomic с производным типом
     * @return Ссылка на себя
     */
    template <typename U>
    IntrusivePtrAtomic& operator=(const IntrusivePtrAtomic<U>& other) {
        if (other.Get() != nullptr) {
            other.Get()->AddRef();
        }
        if (_ptr != nullptr) {
            _ptr->ReleaseRef();
        }
        _ptr = other.Get();
        return *this;
    }

    /**
     * Оператор перемещающего присваивания из производного типа (covariance)
     * @tparam U Производный тип (должен наследоваться от T)
     * @param other Другой IntrusivePtrAtomic с производным типом (будет обнулен)
     * @return Ссылка на себя
     */
    template <typename U>
    IntrusivePtrAtomic& operator=(IntrusivePtrAtomic<U>&& other) noexcept {
        if (_ptr != nullptr) {
            _ptr->ReleaseRef();
        }
        _ptr = other.Get();
        other.Reset();
        return *this;
    }

    /**
     * Оператор разыменования
     * @return Ссылка на объект
     */
    T& operator*() const {
        assert(_ptr != nullptr);
        return *_ptr;
    }

    /**
     * Оператор доступа к членам
     * @return Указатель на объект
     */
    T* operator->() const {
        assert(_ptr != nullptr);
        return _ptr;
    }

    /**
     * Получает сырой указатель
     * @return Сырой указатель или nullptr
     */
    T* Get() const {
        return _ptr;
    }

    /**
     * Проверяет, является ли указатель валидным
     * @return true если указатель не nullptr, false в противном случае
     */
    bool IsValid() const {
        return _ptr != nullptr;
    }

    /**
     * Оператор приведения к bool
     * @return true если указатель не nullptr, false в противном случае
     */
    explicit operator bool() const {
        return _ptr != nullptr;
    }

    /**
     * Сбрасывает указатель (уменьшает счетчик ссылок)
     */
    void Reset() {
        if (_ptr != nullptr) {
            _ptr->ReleaseRef();
            _ptr = nullptr;
        }
    }

    /**
     * Сбрасывает указатель и устанавливает новый
     * @param ptr Новый указатель (может быть nullptr)
     */
    void Reset(T* ptr) {
        if (ptr != nullptr) {
            ptr->AddRef();
        }
        if (_ptr != nullptr) {
            _ptr->ReleaseRef();
        }
        _ptr = ptr;
    }

    /**
     * Получает количество ссылок на объект
     * @return Количество ссылок или 0, если указатель nullptr
     */
    int32_t GetRefCount() const {
        return _ptr != nullptr ? _ptr->GetRefCount() : 0;
    }

    /**
     * Оператор сравнения на равенство
     */
    bool operator==(const IntrusivePtrAtomic& other) const {
        return _ptr == other._ptr;
    }

    /**
     * Оператор сравнения на неравенство
     */
    bool operator!=(const IntrusivePtrAtomic& other) const {
        return _ptr != other._ptr;
    }

    /**
     * Оператор сравнения с nullptr
     */
    bool operator==(std::nullptr_t) const {
        return _ptr == nullptr;
    }

    /**
     * Оператор сравнения с nullptr
     */
    bool operator!=(std::nullptr_t) const {
        return _ptr != nullptr;
    }

private:
    T* _ptr;
};

/**
 * Создает новый IntrusivePtrNonAtomic из сырого указателя
 * @tparam T Тип объекта
 * @param ptr Сырой указатель
 * @return IntrusivePtrNonAtomic, владеющий объектом
 */
template <typename T>
IntrusivePtrNonAtomic<T> MakeIntrusiveNonAtomic(T* ptr) {
    return IntrusivePtrNonAtomic<T>(ptr);
}

/**
 * Создает новый объект и возвращает IntrusivePtrNonAtomic
 * @tparam T Тип объекта
 * @tparam Args Типы аргументов конструктора
 * @param args Аргументы конструктора
 * @return IntrusivePtrNonAtomic, владеющий объектом
 */
template <typename T, typename... Args>
std::enable_if_t<sizeof...(Args) != 1 || !std::is_same_v<std::decay_t<std::tuple_element_t<0, std::tuple<Args...>>>, T*>, IntrusivePtrNonAtomic<T>>
MakeIntrusiveNonAtomic(Args&&... args) {
    return IntrusivePtrNonAtomic<T>(new T(std::forward<Args>(args)...));
}

/**
 * Создает новый IntrusivePtrAtomic из сырого указателя
 * @tparam T Тип объекта
 * @param ptr Сырой указатель
 * @return IntrusivePtrAtomic, владеющий объектом
 */
template <typename T>
IntrusivePtrAtomic<T> MakeIntrusiveAtomic(T* ptr) {
    return IntrusivePtrAtomic<T>(ptr);
}

/**
 * Создает новый объект и возвращает IntrusivePtrAtomic
 * @tparam T Тип объекта
 * @tparam Args Типы аргументов конструктора
 * @param args Аргументы конструктора
 * @return IntrusivePtrAtomic, владеющий объектом
 */
template <typename T, typename... Args>
std::enable_if_t<sizeof...(Args) != 1 || !std::is_same_v<std::decay_t<std::tuple_element_t<0, std::tuple<Args...>>>, T*>, IntrusivePtrAtomic<T>>
MakeIntrusiveAtomic(Args&&... args) {
    return IntrusivePtrAtomic<T>(new T(std::forward<Args>(args)...));
}

/**
 * Алиасы для удобства - по умолчанию используются атомарные версии
 */
template <typename T>
using IntrusivePtr = IntrusivePtrNonAtomic<T>;

template <typename T>
IntrusivePtr<T> MakeIntrusive(T* ptr) {
    return IntrusivePtr<T>(ptr);
}

/**
 * Создает новый объект и возвращает IntrusivePtr
 * @tparam T Тип объекта
 * @tparam Args Типы аргументов конструктора
 * @param args Аргументы конструктора
 * @return IntrusivePtr, владеющий объектом
 */
template <typename T, typename... Args>
std::enable_if_t<sizeof...(Args) != 1 || !std::is_same_v<std::decay_t<std::tuple_element_t<0, std::tuple<Args...>>>, T*>, IntrusivePtr<T>>
MakeIntrusive(Args&&... args) {
    return IntrusivePtr<T>(new T(std::forward<Args>(args)...));
}

