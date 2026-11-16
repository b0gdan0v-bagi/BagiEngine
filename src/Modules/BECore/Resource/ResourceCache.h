#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/PoolStringMap.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Resource/IResource.h>
#include <BECore/Threading/SharedMutex.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /**
     * @brief Thread-safe cache of loaded resources by virtual path (shared lock for reads, exclusive for writes).
     *
     * Nothing is evicted by itself: a resource stays until Remove(), Clear() or shutdown.
     */
    class ResourceCache {
    public:
        ResourceCache() = default;
        ~ResourceCache() = default;

        // The mutex can be neither copied nor moved.
        ResourceCache(const ResourceCache&) = delete;
        ResourceCache& operator=(const ResourceCache&) = delete;
        ResourceCache(ResourceCache&&) = delete;
        ResourceCache& operator=(ResourceCache&&) = delete;

        /** @return The cached resource, or nullptr. */
        IntrusivePtr<IResource> Get(PoolString path) const;

        /** @brief Replaces an entry of the same path. */
        void Put(PoolString path, IntrusivePtr<IResource> resource);

        bool Contains(PoolString path) const;

        /** @return Whether there was an entry; a handle already given out keeps its resource alive. */
        bool Remove(PoolString path);

        void Clear();

        /** @return The sum of GetMemoryUsage() over the cached resources. */
        uint64_t GetTotalMemoryUsage() const;

        size_t GetCount() const;

    private:
        mutable SharedMutex _mutex{BE_LOCK_SITE("Resource cache")};
        UnorderedPoolMap<IntrusivePtr<IResource>> _cache;
    };

}  // namespace BECore
