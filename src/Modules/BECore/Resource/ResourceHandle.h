#pragma once

#include <BECore/Resource/IResource.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <chrono>

namespace BECore {

    /**
     * @brief Typed handle to a cached resource
     * 
     * Thin wrapper around IntrusivePtr<T> that tracks last access time
     * for potential future LRU eviction support.
     * 
     * @tparam T Resource type (must inherit from IResource)
     * 
     * @example
     * ResourceHandle<XmlResource> handle = resourceManager.Load<XmlResource>("config.xml");
     * if (handle) {
     *     auto node = handle->GetRoot();
     * }
     */
    template<typename T>
        requires std::derived_from<T, IResource>
    class ResourceHandle {
    public:
        ResourceHandle() = default;
        
        /**
         * @brief Construct from IntrusivePtr
         * @param resource Resource pointer to wrap
         */
        explicit ResourceHandle(IntrusivePtr<T> resource) 
            : _resource(std::move(resource))
            , _lastAccess(std::chrono::steady_clock::now()) {
        }
        
        /**
         * @brief Implicit conversion to bool for validity checks
         * @return true if handle contains a valid resource
         */
        explicit operator bool() const noexcept {
            return _resource != nullptr;
        }
        
        /**
         * @brief Arrow operator for accessing resource methods
         * @return Pointer to resource (updates access time)
         */
        T* operator->() const noexcept {
            UpdateAccessTime();
            return _resource.Get();
        }
        
        /**
         * @brief Dereference operator for accessing resource
         * @return Reference to resource (updates access time)
         */
        T& operator*() const noexcept {
            UpdateAccessTime();
            return *_resource;
        }
        
        /**
         * @brief Get underlying pointer without updating access time
         * @return Raw pointer to resource
         */
        T* Get() const noexcept {
            return _resource.Get();
        }
        
        /**
         * @brief Get underlying IntrusivePtr
         * @return Copy of the IntrusivePtr
         */
        IntrusivePtr<T> GetPtr() const noexcept {
            return _resource;
        }
        
        /**
         * @brief Get last access time
         * @return Time point of last access
         */
        std::chrono::steady_clock::time_point GetLastAccessTime() const noexcept {
            return _lastAccess;
        }
        
        /**
         * @brief Reset handle to empty state
         */
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
