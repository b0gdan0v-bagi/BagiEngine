#pragma once

#include "RefCounted.h"
#include <cassert>
#include <type_traits>

namespace Core {

    template <typename T> class IntrusivePtrNonAtomic {
        template <typename U> friend class IntrusivePtrNonAtomic;

    public:
        IntrusivePtrNonAtomic() = default;

        explicit IntrusivePtrNonAtomic(T* ptr) : _ptr(ptr) {
            if (_ptr != nullptr) {
                _ptr->AddRef();
            }
        }

        IntrusivePtrNonAtomic(const IntrusivePtrNonAtomic& other) : _ptr(other._ptr) {
            if (_ptr != nullptr) {
                _ptr->AddRef();
            }
        }

        IntrusivePtrNonAtomic(IntrusivePtrNonAtomic&& other) noexcept : _ptr(other._ptr) {
            other._ptr = nullptr;
        }

        template <typename U> IntrusivePtrNonAtomic(const IntrusivePtrNonAtomic<U>& other) : _ptr(static_cast<T*>(other._ptr)) {
            if (_ptr != nullptr) {
                _ptr->AddRef();
            }
        }

        template <typename U> IntrusivePtrNonAtomic(IntrusivePtrNonAtomic<U>&& other) noexcept : _ptr(other._ptr) {
            static_assert(std::is_base_of_v<T, U>, "U must be derived from T");
            other._ptr = nullptr;
        }

        ~IntrusivePtrNonAtomic() {
            Reset();
        }

        bool operator!() const {
            return _ptr == nullptr;
        }

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

        template <typename U> IntrusivePtrNonAtomic& operator=(const IntrusivePtrNonAtomic<U>& other) {
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

        template <typename U> IntrusivePtrNonAtomic& operator=(IntrusivePtrNonAtomic<U>&& other) noexcept {
            static_assert(std::is_base_of_v<T, U>, "U must be derived from T");
            if (_ptr != nullptr) {
                _ptr->ReleaseRef();
            }
            _ptr = other._ptr;
            other._ptr = nullptr;
            return *this;
        }

        T& operator*() const {
            assert(_ptr != nullptr);
            return *_ptr;
        }

        T* operator->() const {
            assert(_ptr != nullptr);
            return _ptr;
        }

        T* Get() const {
            return _ptr;
        }

        bool IsValid() const {
            return _ptr != nullptr;
        }

        explicit operator bool() const {
            return _ptr != nullptr;
        }

        void Reset() {
            if (_ptr != nullptr) {
                _ptr->ReleaseRef();
                _ptr = nullptr;
            }
        }

        void Reset(T* ptr) {
            if (ptr != nullptr) {
                ptr->AddRef();
            }
            if (_ptr != nullptr) {
                _ptr->ReleaseRef();
            }
            _ptr = ptr;
        }

        int32_t GetRefCount() const {
            return _ptr != nullptr ? _ptr->GetRefCount() : 0;
        }

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
        T* _ptr = nullptr;
    };

    template <typename T> class IntrusivePtrAtomic {
    public:
        IntrusivePtrAtomic() = default;

        explicit IntrusivePtrAtomic(T* ptr) : _ptr(ptr) {
            if (_ptr != nullptr) {
                _ptr->AddRef();
            }
        }

        IntrusivePtrAtomic(const IntrusivePtrAtomic& other) : _ptr(other._ptr) {
            if (_ptr != nullptr) {
                _ptr->AddRef();
            }
        }

        IntrusivePtrAtomic(IntrusivePtrAtomic&& other) noexcept : _ptr(other._ptr) {
            other._ptr = nullptr;
        }

        template <typename U> IntrusivePtrAtomic(const IntrusivePtrAtomic<U>& other) : _ptr(other.Get()) {
            if (_ptr != nullptr) {
                _ptr->AddRef();
            }
        }

        template <typename U> IntrusivePtrAtomic(IntrusivePtrAtomic<U>&& other) noexcept : _ptr(other.Get()) {
            other.Reset();
        }

        ~IntrusivePtrAtomic() {
            Reset();
        }

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

        template <typename U> IntrusivePtrAtomic& operator=(const IntrusivePtrAtomic<U>& other) {
            if (other.Get() != nullptr) {
                other.Get()->AddRef();
            }
            if (_ptr != nullptr) {
                _ptr->ReleaseRef();
            }
            _ptr = other.Get();
            return *this;
        }

        template <typename U> IntrusivePtrAtomic& operator=(IntrusivePtrAtomic<U>&& other) noexcept {
            if (_ptr != nullptr) {
                _ptr->ReleaseRef();
            }
            _ptr = other.Get();
            other.Reset();
            return *this;
        }

        T& operator*() const {
            assert(_ptr != nullptr);
            return *_ptr;
        }

        T* operator->() const {
            assert(_ptr != nullptr);
            return _ptr;
        }

        T* Get() const {
            return _ptr;
        }

        bool IsValid() const {
            return _ptr != nullptr;
        }

        explicit operator bool() const {
            return _ptr != nullptr;
        }

        void Reset() {
            if (_ptr != nullptr) {
                _ptr->ReleaseRef();
                _ptr = nullptr;
            }
        }

        void Reset(T* ptr) {
            if (ptr != nullptr) {
                ptr->AddRef();
            }
            if (_ptr != nullptr) {
                _ptr->ReleaseRef();
            }
            _ptr = ptr;
        }

        int32_t GetRefCount() const {
            return _ptr != nullptr ? _ptr->GetRefCount() : 0;
        }

        bool operator==(const IntrusivePtrAtomic& other) const {
            return _ptr == other._ptr;
        }

        bool operator!=(const IntrusivePtrAtomic& other) const {
            return _ptr != other._ptr;
        }

        bool operator==(std::nullptr_t) const {
            return _ptr == nullptr;
        }
        bool operator!=(std::nullptr_t) const {
            return _ptr != nullptr;
        }

    private:
        T* _ptr = nullptr;
    };

    template <typename T> IntrusivePtrNonAtomic<T> MakeIntrusiveNonAtomic(T* ptr) {
        return IntrusivePtrNonAtomic<T>(ptr);
    }

    template <typename T, typename... Args>
    std::enable_if_t<sizeof...(Args) != 1 || !std::is_same_v<std::decay_t<std::tuple_element_t<0, std::tuple<Args...>>>, T*>, IntrusivePtrNonAtomic<T>> MakeIntrusiveNonAtomic(Args&&... args) {
        return IntrusivePtrNonAtomic<T>(new T(std::forward<Args>(args)...));
    }

    template <typename T> IntrusivePtrAtomic<T> MakeIntrusiveAtomic(T* ptr) {
        return IntrusivePtrAtomic<T>(ptr);
    }

    template <typename T, typename... Args>
    std::enable_if_t<sizeof...(Args) != 1 || !std::is_same_v<std::decay_t<std::tuple_element_t<0, std::tuple<Args...>>>, T*>, IntrusivePtrAtomic<T>> MakeIntrusiveAtomic(Args&&... args) {
        return IntrusivePtrAtomic<T>(new T(std::forward<Args>(args)...));
    }

    template <typename T> using IntrusivePtr = IntrusivePtrNonAtomic<T>;

    template <typename T> IntrusivePtr<T> MakeIntrusive(T* ptr) {
        return IntrusivePtr<T>(ptr);
    }

    template <typename T, typename... Args>
    std::enable_if_t<sizeof...(Args) != 1 || !std::is_same_v<std::decay_t<std::tuple_element_t<0, std::tuple<Args...>>>, T*>, IntrusivePtr<T>> MakeIntrusive(Args&&... args) {
        return IntrusivePtr<T>(new T(std::forward<Args>(args)...));
    }
}  // namespace Core

