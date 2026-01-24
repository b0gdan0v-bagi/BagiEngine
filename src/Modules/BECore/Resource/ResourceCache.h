#pragma once

#include <BECore/Resource/IResource.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringMap.h>
#include <shared_mutex>

namespace BECore {

    /**
     * @brief Thread-safe cache for loaded resources
     * 
     * Stores resources by virtual path with reference counting.
     * Uses shared_mutex for read-heavy concurrent access patterns.
     * 
     * @note Eviction is not implemented in v1. Resources stay cached
     * until explicitly cleared or application shutdown.
     * 
     * @example
     * ResourceCache cache;
     * cache.Put("config.xml", myResource);
     * if (auto res = cache.Get("config.xml")) {
     *     // Use resource...
     * }
     */
    class ResourceCache {
    public:
        ResourceCache() = default;
        ~ResourceCache() = default;
        
        // Non-copyable, non-movable (contains mutex)
        ResourceCache(const ResourceCache&) = delete;
        ResourceCache& operator=(const ResourceCache&) = delete;
        ResourceCache(ResourceCache&&) = delete;
        ResourceCache& operator=(ResourceCache&&) = delete;
        
        /**
         * @brief Get cached resource by path
         * 
         * Thread-safe for concurrent reads.
         * 
         * @param path Virtual path to the resource
         * @return Resource pointer or nullptr if not cached
         */
        IntrusivePtr<IResource> Get(PoolString path) const;
        
        /**
         * @brief Store resource in cache
         * 
         * Thread-safe. If resource already exists, it will be replaced.
         * 
         * @param path Virtual path as cache key
         * @param resource Resource to cache
         */
        void Put(PoolString path, IntrusivePtr<IResource> resource);
        
        /**
         * @brief Check if resource is cached
         * 
         * Thread-safe for concurrent reads.
         * 
         * @param path Virtual path to check
         * @return true if resource exists in cache
         */
        bool Contains(PoolString path) const;
        
        /**
         * @brief Clear all cached resources
         * 
         * Thread-safe. Acquires exclusive lock.
         */
        void Clear();
        
        /**
         * @brief Get total memory usage of cached resources
         * 
         * Thread-safe for concurrent reads.
         * 
         * @return Sum of GetMemoryUsage() for all cached resources
         */
        uint64_t GetTotalMemoryUsage() const;
        
        /**
         * @brief Get number of cached resources
         * 
         * Thread-safe for concurrent reads.
         * 
         * @return Cache entry count
         */
        size_t GetCount() const;

    private:
        mutable std::shared_mutex _mutex;
        UnorderedPoolMap<IntrusivePtr<IResource>> _cache;
    };

}  // namespace BECore
