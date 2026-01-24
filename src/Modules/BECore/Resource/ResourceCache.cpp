#include <BECore/Resource/ResourceCache.h>

namespace BECore {

    IntrusivePtr<IResource> ResourceCache::Get(PoolString path) const {
        std::shared_lock lock(_mutex);
        auto it = _cache.Find(path);
        if (it != _cache.end()) {
            return it->second;
        }
        return IntrusivePtr<IResource>();
    }

    void ResourceCache::Put(PoolString path, IntrusivePtr<IResource> resource) {
        std::unique_lock lock(_mutex);
        _cache[path] = std::move(resource);
    }

    bool ResourceCache::Contains(PoolString path) const {
        std::shared_lock lock(_mutex);
        return _cache.Find(path) != _cache.end();
    }

    void ResourceCache::Clear() {
        std::unique_lock lock(_mutex);
        _cache.clear();
    }

    uint64_t ResourceCache::GetTotalMemoryUsage() const {
        std::shared_lock lock(_mutex);
        uint64_t total = 0;
        for (const auto& [path, resource] : _cache) {
            if (resource) {
                total += resource->GetMemoryUsage();
            }
        }
        return total;
    }

    size_t ResourceCache::GetCount() const {
        std::shared_lock lock(_mutex);
        return _cache.size();
    }

}  // namespace BECore
