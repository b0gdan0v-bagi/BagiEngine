#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Resource/IResource.h>
#include <concepts>
#include <utility>

namespace BECore {

    /** @brief Typed handle to a cached resource. */
    template <typename T>
    requires std::derived_from<T, IResource> class ResourceHandle {
    public:
        ResourceHandle() = default;

        explicit ResourceHandle(IntrusivePtr<T> resource) : _resource(std::move(resource)) {}

        explicit operator bool() const noexcept {
            return _resource != nullptr;
        }

        T* operator->() const noexcept {
            return _resource.Get();
        }

        T& operator*() const noexcept {
            return *_resource;
        }

        T* Get() const noexcept {
            return _resource.Get();
        }

        IntrusivePtr<T> GetPtr() const noexcept {
            return _resource;
        }

        void Reset() noexcept {
            _resource.Reset();
        }

    private:
        IntrusivePtr<T> _resource;
    };

}  // namespace BECore
