#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/XmlResourceLoader.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Logger/Logger.h>

#include <Generated/IResourceLoader.gen.hpp>
#include <Generated/XmlResourceLoader.gen.hpp>

namespace BECore {

    void ResourceManager::Initialize(FileSystem* fileSystem) {
        LOG_INFO("Initializing ResourceManager...");
        
        _fileSystem = fileSystem;
        
        // Register default loaders
        RegisterLoader(New<XmlResourceLoader>());
        
        LOG_INFO(Format("ResourceManager initialized with {} loaders", _loaders.size()).c_str());
    }

    void ResourceManager::RegisterLoader(IntrusivePtr<IResourceLoader> loader) {
        if (!loader) {
            LOG_ERROR("Attempted to register null loader");
            return;
        }
        
        _loaders.push_back(std::move(loader));
    }

    void ResourceManager::ClearCache() {
        LOG_INFO(Format("Clearing resource cache ({} resources)", _cache.GetCount()).c_str());
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
