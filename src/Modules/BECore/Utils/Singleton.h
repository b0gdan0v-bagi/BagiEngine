#pragma once

namespace BECore {
    template <typename T> class Singleton {
    public:
        /**
         * Получает единственный экземпляр класса T
         * @return Ссылка на единственный экземпляр
         */
        static T& GetInstance() {
            if (_instance == nullptr) {
                _instance = new T();
            }
            return *_instance;
        }

        /**
         * Удаляет единственный экземпляр класса T
         * Полезно для тестирования или явного освобождения ресурсов
         */
        static void DestroyInstance() {
            if (_instance != nullptr) {
                delete _instance;
                _instance = nullptr;
            }
        }

        /**
         * Проверяет, существует ли экземпляр
         * @return true если экземпляр создан, false в противном случае
         */
        static bool IsInstanceCreated() {
            return _instance != nullptr;
        }

    protected:
        Singleton() = default;
        virtual ~Singleton() = default;

        // Запрещаем копирование и присваивание
        Singleton(const Singleton&) = delete;
        Singleton& operator=(const Singleton&) = delete;
        Singleton(Singleton&&) = delete;
        Singleton& operator=(Singleton&&) = delete;

    private:
        static T* _instance;
    };

    // Инициализация статических членов
    template <typename T> T* Singleton<T>::_instance = nullptr;

    /**
     * Базовый класс для реализации паттерна Singleton (атомарная версия).
     * Потокобезопасен, использует double-checked locking.
     * Используйте в многопоточных приложениях.
     *
     * @tparam T Тип класса, который должен быть Singleton
     *
     * Пример использования:
     * class MyManager : public SingletonAtomic<MyManager> {
     *     friend class SingletonAtomic<MyManager>;
     * private:
     *     MyManager() = default;
     * public:
     *     void DoSomething() { ... }
     * };
     *
     * // Использование:
     * MyManager::GetInstance().DoSomething();
     */
    template <typename T> class SingletonAtomic {
    public:
        /**
         * Получает единственный экземпляр класса T
         * @return Ссылка на единственный экземпляр
         */
        static T& GetInstance() {
            T* instance = _instance.load(std::memory_order_acquire);
            if (instance == nullptr) {
                std::lock_guard<std::mutex> lock(_mutex);
                instance = _instance.load(std::memory_order_relaxed);
                if (instance == nullptr) {
                    instance = new T();
                    _instance.store(instance, std::memory_order_release);
                }
            }
            return *instance;
        }

        /**
         * Удаляет единственный экземпляр класса T
         * Полезно для тестирования или явного освобождения ресурсов
         */
        static void DestroyInstance() {
            T* instance = _instance.load(std::memory_order_acquire);
            if (instance != nullptr) {
                std::lock_guard<std::mutex> lock(_mutex);
                instance = _instance.load(std::memory_order_relaxed);
                if (instance != nullptr) {
                    delete instance;
                    _instance.store(nullptr, std::memory_order_release);
                }
            }
        }

        /**
         * Проверяет, существует ли экземпляр
         * @return true если экземпляр создан, false в противном случае
         */
        static bool IsInstanceCreated() {
            return _instance.load(std::memory_order_acquire) != nullptr;
        }

    protected:
        SingletonAtomic() = default;
        virtual ~SingletonAtomic() = default;

        // Запрещаем копирование и присваивание
        SingletonAtomic(const SingletonAtomic&) = delete;
        SingletonAtomic& operator=(const SingletonAtomic&) = delete;
        SingletonAtomic(SingletonAtomic&&) = delete;
        SingletonAtomic& operator=(SingletonAtomic&&) = delete;

    private:
        static std::atomic<T*> _instance;
        static std::mutex _mutex;
    };

    // Инициализация статических членов
    template <typename T> std::atomic<T*> SingletonAtomic<T>::_instance{nullptr};

    template <typename T> std::mutex SingletonAtomic<T>::_mutex;

}  // namespace BECore

