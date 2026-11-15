#pragma once

#include <BECore/Threading/LockGuards.h>
#include <BECore/Threading/Mutex.h>
#include <atomic>

namespace BECore {
    template <typename T>
    class Singleton {
    public:
        /// Creates the instance on the first call. Not thread-safe: use SingletonAtomic when threads can meet here.
        static T& GetInstance() {
            if (_instance == nullptr) {
                _instance = new T();
            }
            return *_instance;
        }

        /// Deletes the instance (a test, an explicit shutdown); the next GetInstance makes a new one.
        static void DestroyInstance() {
            if (_instance != nullptr) {
                delete _instance;
                _instance = nullptr;
            }
        }

        static bool IsInstanceCreated() {
            return _instance != nullptr;
        }

    protected:
        Singleton() = default;
        virtual ~Singleton() = default;

        Singleton(const Singleton&) = delete;
        Singleton& operator=(const Singleton&) = delete;
        Singleton(Singleton&&) = delete;
        Singleton& operator=(Singleton&&) = delete;

    private:
        static T* _instance;
    };

    template <typename T>
    T* Singleton<T>::_instance = nullptr;

    /**
     * The thread-safe Singleton: double-checked locking on the first GetInstance and on DestroyInstance. The derived class befriends it
     * (`friend class SingletonAtomic<MyManager>;`) to keep a private constructor. The instance itself gets no locking from here.
     */
    template <typename T>
    class SingletonAtomic {
    public:
        static T& GetInstance() {
            T* instance = _instance.load(std::memory_order_acquire);
            if (instance == nullptr) {
                LockGuard lock(_mutex);
                instance = _instance.load(std::memory_order_relaxed);
                if (instance == nullptr) {
                    instance = new T();
                    _instance.store(instance, std::memory_order_release);
                }
            }
            return *instance;
        }

        /// A reference taken before this call dangles: destroy only when nothing uses the instance.
        static void DestroyInstance() {
            T* instance = _instance.load(std::memory_order_acquire);
            if (instance != nullptr) {
                LockGuard lock(_mutex);
                instance = _instance.load(std::memory_order_relaxed);
                if (instance != nullptr) {
                    delete instance;
                    _instance.store(nullptr, std::memory_order_release);
                }
            }
        }

        static bool IsInstanceCreated() {
            return _instance.load(std::memory_order_acquire) != nullptr;
        }

    protected:
        SingletonAtomic() = default;
        virtual ~SingletonAtomic() = default;

        SingletonAtomic(const SingletonAtomic&) = delete;
        SingletonAtomic& operator=(const SingletonAtomic&) = delete;
        SingletonAtomic(SingletonAtomic&&) = delete;
        SingletonAtomic& operator=(SingletonAtomic&&) = delete;

    private:
        static std::atomic<T*> _instance;
        static Mutex _mutex;
    };

    template <typename T>
    std::atomic<T*> SingletonAtomic<T>::_instance{nullptr};

    template <typename T>
    Mutex SingletonAtomic<T>::_mutex{BE_LOCK_SITE("SingletonAtomic")};

}  // namespace BECore
