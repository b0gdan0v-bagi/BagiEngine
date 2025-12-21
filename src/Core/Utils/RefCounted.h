#pragma once

/**
 * Базовый класс для объектов с подсчетом ссылок (неатомарная версия).
 * Используется совместно с IntrusivePtr для реализации интрузивных указателей.
 * Не потокобезопасен, но более быстрый.
 * Используйте только в однопоточных приложениях или когда доступ
 * гарантированно происходит из одного потока.
 *
 * Пример использования:
 * class MyResource : public RefCountedNonAtomic {
 * public:
 *     void DoSomething() { ... }
 * };
 *
 * IntrusivePtr<MyResource> ptr(new MyResource());
 */
class RefCountedNonAtomic {
public:
    RefCountedNonAtomic() : _refCount(0) {}
    virtual ~RefCountedNonAtomic() = default;

    /**
     * Увеличивает счетчик ссылок
     */
    void AddRef() const {
        ++_refCount;
    }

    /**
     * Уменьшает счетчик ссылок и удаляет объект, если счетчик достиг нуля
     * @return true если объект был удален, false в противном случае
     */
    bool ReleaseRef() const {
        --_refCount;
        if (_refCount == 0) {
            delete this;
            return true;
        }
        return false;
    }

    /**
     * Получает текущее значение счетчика ссылок
     * @return Текущее количество ссылок
     */
    int32_t GetRefCount() const {
        return _refCount;
    }

protected:
    // Запрещаем копирование и присваивание
    RefCountedNonAtomic(const RefCountedNonAtomic&) = delete;
    RefCountedNonAtomic& operator=(const RefCountedNonAtomic&) = delete;
    RefCountedNonAtomic(RefCountedNonAtomic&&) = delete;
    RefCountedNonAtomic& operator=(RefCountedNonAtomic&&) = delete;

private:
    mutable int32_t _refCount;
};

/**
 * Базовый класс для объектов с подсчетом ссылок (атомарная версия).
 * Используется совместно с IntrusivePtr для реализации интрузивных указателей.
 * Потокобезопасен, использует атомарные операции.
 * Используйте в многопоточных приложениях.
 *
 * Пример использования:
 * class MyResource : public RefCountedAtomic {
 * public:
 *     void DoSomething() { ... }
 * };
 *
 * IntrusivePtr<MyResource> ptr(new MyResource());
 */
class RefCountedAtomic {
public:
    RefCountedAtomic() : _refCount(0) {}
    virtual ~RefCountedAtomic() = default;

    /**
     * Увеличивает счетчик ссылок
     */
    void AddRef() const {
        _refCount.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * Уменьшает счетчик ссылок и удаляет объект, если счетчик достиг нуля
     * @return true если объект был удален, false в противном случае
     */
    bool ReleaseRef() const {
        int32_t oldCount = _refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (oldCount == 1) {
            delete this;
            return true;
        }
        return false;
    }

    /**
     * Получает текущее значение счетчика ссылок
     * @return Текущее количество ссылок
     */
    int32_t GetRefCount() const {
        return _refCount.load(std::memory_order_acquire);
    }

protected:
    // Запрещаем копирование и присваивание
    RefCountedAtomic(const RefCountedAtomic&) = delete;
    RefCountedAtomic& operator=(const RefCountedAtomic&) = delete;
    RefCountedAtomic(RefCountedAtomic&&) = delete;
    RefCountedAtomic& operator=(RefCountedAtomic&&) = delete;

private:
    mutable std::atomic<int32_t> _refCount;
};

/**
 * Алиас для удобства - по умолчанию используется атомарная версия
 */
using RefCounted = RefCountedNonAtomic;

