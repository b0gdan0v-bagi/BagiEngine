#include <BECore/Resource/ResourceManager.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Logger/Logger.h>

#include <Generated/IResourceLoader.gen.hpp>
#include <Generated/EnumResourceLoader.gen.hpp>

namespace BECore {

    void ResourceManager::Initialize() {
        LOG_INFO("Initializing ResourceManager...");
        
        // Register all loaders via generated factory
        for (auto type : EnumUtils<ResourceLoaderType>::Values()) {
            auto loader = ResourceLoaderFactory::Create(type);
            if (loader) {
                RegisterLoader(std::move(loader));
            }
        }
        
        LOG_INFO(Format("ResourceManager initialized with {} loaders", _loaders.size()));
    }

    void ResourceManager::RegisterLoader(IntrusivePtr<IResourceLoader> loader) {
        if (!loader) {
            LOG_ERROR("Attempted to register null loader");
            return;
        }
        
        _loaders.push_back(std::move(loader));
    }

    void ResourceManager::ClearCache() {
        LOG_INFO(Format("Clearing resource cache ({} resources)", _cache.GetCount()));
        _cache.Clear();
    }

    const ResourceCache& ResourceManager::GetCache() const {
        return _cache;
    }

    IResourceLoader* ResourceManager::FindLoader(eastl::string_view extension) const {
        for (const auto& loader : _loaders) {
            if (loader->CanLoad(extension)) {
                return loader.Get();
            }
        }
        return nullptr;
    }

    eastl::string_view ResourceManager::GetExtension(eastl::string_view path) const {
        auto pos = path.find_last_of('.');
        if (pos == eastl::string_view::npos) {
            return eastl::string_view{};
        }
        return path.substr(pos);
    }

}  // namespace BECore
