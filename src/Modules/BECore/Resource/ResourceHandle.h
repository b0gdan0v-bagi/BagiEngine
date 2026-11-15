#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Resource/IResource.h>
#include <chrono>
#include <concepts>
#include <utility>

namespace BECore {

    /**
     * @brief Typed handle to a cached resource: an IntrusivePtr<T> that also records the last access
     * (operator-> and operator*), kept for a future LRU eviction and read by nobody yet.
     */
    template <typename T>
    requires std::derived_from<T, IResource> class ResourceHandle {
    public:
        ResourceHandle() = default;

        explicit ResourceHandle(IntrusivePtr<T> resource) : _resource(std::move(resource)), _lastAccess(std::chrono::steady_clock::now()) {}

        explicit operator bool() const noexcept {
            return _resource != nullptr;
        }

        T* operator->() const noexcept {
            UpdateAccessTime();
            return _resource.Get();
        }

        T& operator*() const noexcept {
            UpdateAccessTime();
            return *_resource;
        }

        /** @brief The raw pointer, without recording an access. */
        T* Get() const noexcept {
            return _resource.Get();
        }

        IntrusivePtr<T> GetPtr() const noexcept {
            return _resource;
        }

        std::chrono::steady_clock::time_point GetLastAccessTime() const noexcept {
            return _lastAccess;
        }

        void Reset() noexcept {
            _resource.Reset();
        }

    private:
        void UpdateAccessTime() const noexcept {
            _lastAccess = std::chrono::steady_clock::now();
        }

        IntrusivePtr<T> _resource;
        mutable std::chrono::steady_clock::time_point _lastAccess;
    };

}  // namespace BECore
