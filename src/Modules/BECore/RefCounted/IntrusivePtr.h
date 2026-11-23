#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <EASTL/functional.h>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace BECore {

    /// Tag of the adopting constructor of IntrusivePtr: the pointer already holds a count that the new owner takes over.
    struct AdoptRef {};
    inline constexpr AdoptRef kAdoptRef{};

    /**
     * Owner of one count of an object that carries its own counter (RefCountedNonAtomic or RefCountedAtomic, `RefCounted.h`).
     *
     * One class for both counters: the object's counter decides whether a count is atomic, not the pointer. Make the first owner with
     * `New<T>` (`New.h`). A conversion to a base is implicit; a downcast does not compile, use StaticPointerCast or DynamicPointerCast.
     *
     * Every release moves the pointer out first and drops the count afterwards, so a destructor that reaches back into the pointer
     * that held the object sees it empty.
     */
    template <typename T>
    class IntrusivePtr {
        template <typename U>
        friend class IntrusivePtr;

    public:
        IntrusivePtr() noexcept = default;

        /// Takes a new count of @p ptr (null is allowed). For a pointer whose count is already owned by this pointer, use the AdoptRef form.
        explicit IntrusivePtr(T* ptr) noexcept : _ptr(ptr) {
            if (_ptr != nullptr) {
                _ptr->AddRef();
            }
        }

        /// Takes over a count that @p ptr already holds, with no AddRef: the pair of Detach(), nothing else should make one.
        IntrusivePtr(T* ptr, AdoptRef) noexcept : _ptr(ptr) {}

        IntrusivePtr(const IntrusivePtr& other) noexcept : _ptr(other._ptr) {
            if (_ptr != nullptr) {
                _ptr->AddRef();
            }
        }

        IntrusivePtr(IntrusivePtr&& other) noexcept : _ptr(std::exchange(other._ptr, nullptr)) {}

        template <typename U>
        requires std::convertible_to<U*, T*> IntrusivePtr(const IntrusivePtr<U>& other) noexcept : _ptr(other._ptr) {
            if (_ptr != nullptr) {
                _ptr->AddRef();
            }
        }

        template <typename U>
        requires std::convertible_to<U*, T*> IntrusivePtr(IntrusivePtr<U>&& other) noexcept : _ptr(std::exchange(other._ptr, nullptr)) {}

        ~IntrusivePtr() noexcept {
            Reset();
        }

        IntrusivePtr& operator=(const IntrusivePtr& other) noexcept {
            IntrusivePtr(other).swap(*this);
            return *this;
        }

        IntrusivePtr& operator=(IntrusivePtr&& other) noexcept {
            IntrusivePtr(std::move(other)).swap(*this);
            return *this;
        }

        template <typename U>
        requires std::convertible_to<U*, T*> IntrusivePtr& operator=(const IntrusivePtr<U>& other) noexcept {
            IntrusivePtr(other).swap(*this);
            return *this;
        }

        template <typename U>
        requires std::convertible_to<U*, T*> IntrusivePtr& operator=(IntrusivePtr<U>&& other) noexcept {
            IntrusivePtr(std::move(other)).swap(*this);
            return *this;
        }

        /// Takes a new count of @p ptr; `ptr = nullptr` empties the pointer.
        IntrusivePtr& operator=(T* ptr) noexcept {
            IntrusivePtr(ptr).swap(*this);
            return *this;
        }

        T& operator*() const {
            ASSERT(_ptr != nullptr);
            return *_ptr;
        }

        T* operator->() const {
            ASSERT(_ptr != nullptr);
            return _ptr;
        }

        T* Get() const noexcept {
            return _ptr;
        }

        bool IsValid() const noexcept {
            return _ptr != nullptr;
        }

        explicit operator bool() const noexcept {
            return _ptr != nullptr;
        }

        void Reset() noexcept {
            if (T* old = std::exchange(_ptr, nullptr); old != nullptr) {
                old->ReleaseRef();
            }
        }

        void Reset(T* ptr) noexcept {
            IntrusivePtr(ptr).swap(*this);
        }

        /// Gives the pointer up without dropping its count: the pointer is empty afterwards and the caller owns the count (give it back with the AdoptRef constructor).
        [[nodiscard]] T* Detach() noexcept {
            return std::exchange(_ptr, nullptr);
        }

        void swap(IntrusivePtr& other) noexcept {
            std::swap(_ptr, other._ptr);
        }

        /// The count of the pointee, 0 for an empty pointer; a snapshot (`RefCounted.h`).
        int32_t GetRefCount() const noexcept {
            return _ptr != nullptr ? _ptr->GetRefCount() : 0;
        }

    private:
        T* _ptr = nullptr;
    };

    template <typename T>
    void swap(IntrusivePtr<T>& left, IntrusivePtr<T>& right) noexcept {
        left.swap(right);
    }

    /// The pointer as a `U`, by `static_cast` (the caller knows the type); the source keeps its count.
    template <typename U, typename T>
    IntrusivePtr<U> StaticPointerCast(const IntrusivePtr<T>& ptr) noexcept {
        return IntrusivePtr<U>(static_cast<U*>(ptr.Get()));
    }

    /// The same cast taking the source's count over: the source is empty afterwards.
    template <typename U, typename T>
    IntrusivePtr<U> StaticPointerCast(IntrusivePtr<T>&& ptr) noexcept {
        return IntrusivePtr<U>(static_cast<U*>(ptr.Detach()), kAdoptRef);
    }

    /// The pointer as a `U` by `dynamic_cast`; empty when the object is no `U`. The source keeps its count.
    template <typename U, typename T>
    IntrusivePtr<U> DynamicPointerCast(const IntrusivePtr<T>& ptr) noexcept {
        return IntrusivePtr<U>(dynamic_cast<U*>(ptr.Get()));
    }

    /// The same cast taking the source's count over only when it succeeds; a failed cast leaves the source as it was.
    template <typename U, typename T>
    IntrusivePtr<U> DynamicPointerCast(IntrusivePtr<T>&& ptr) noexcept {
        U* casted = dynamic_cast<U*>(ptr.Get());
        if (casted == nullptr) {
            return {};
        }
        static_cast<void>(ptr.Detach());
        return IntrusivePtr<U>(casted, kAdoptRef);
    }

    // The comparisons are the ordering of the addresses; != and the reversed forms come from C++20 rewriting, there is none by hand.
    // clang-format off: the formatter of this repository is set to C++17 and splits `operator<=>`
    template <typename T, typename U>
    bool operator==(const IntrusivePtr<T>& left, const IntrusivePtr<U>& right) noexcept {
        return left.Get() == right.Get();
    }

    template <typename T, typename U>
    std::strong_ordering operator<=>(const IntrusivePtr<T>& left, const IntrusivePtr<U>& right) noexcept {
        return std::compare_three_way{}(left.Get(), right.Get());
    }

    template <typename T>
    bool operator==(const IntrusivePtr<T>& left, std::nullptr_t) noexcept {
        return left.Get() == nullptr;
    }

    template <typename T>
    std::strong_ordering operator<=>(const IntrusivePtr<T>& left, std::nullptr_t) noexcept {
        return std::compare_three_way{}(left.Get(), static_cast<T*>(nullptr));
    }

    template <typename T, typename U>
    bool operator==(const IntrusivePtr<T>& left, U* right) noexcept {
        return left.Get() == right;
    }

    template <typename T, typename U>
    std::strong_ordering operator<=>(const IntrusivePtr<T>& left, U* right) noexcept {
        return std::compare_three_way{}(left.Get(), right);
    }
    // clang-format on

    /// Kept for one merge so that a game follows; use IntrusivePtr.
    template <typename T>
    using IntrusivePtrAtomic = IntrusivePtr<T>;

    /// Kept for one merge so that a game follows; use IntrusivePtr.
    template <typename T>
    using IntrusivePtrNonAtomic = IntrusivePtr<T>;

}  // namespace BECore

namespace std {  //-NoStd: the specialization of std::hash is the only way to make the pointer a key of a standard container

    template <typename T>
    struct hash<::BECore::IntrusivePtr<T>> {
        size_t operator()(const ::BECore::IntrusivePtr<T>& ptr) const noexcept {
            return hash<T*>{}(ptr.Get());
        }
    };

}  // namespace std

namespace eastl {

    template <typename T>
    struct hash<::BECore::IntrusivePtr<T>> {
        size_t operator()(const ::BECore::IntrusivePtr<T>& ptr) const noexcept {
            return hash<T*>{}(ptr.Get());
        }
    };

}  // namespace eastl
