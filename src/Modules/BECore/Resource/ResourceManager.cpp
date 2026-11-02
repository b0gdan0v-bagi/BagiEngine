#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Resource/IResourceLoader.h>
#include <BECore/Resource/ResourceManager.h>

namespace BECore {

    void ResourceManager::Initialize() {
        LOG_INFO("Initializing ResourceManager...");

        // Create all registered loaders via the self-registration factory.
        // Each concrete loader registered itself when its .gen.hpp was included
        // in its own .cpp, so no EnumXxx.gen.hpp include is needed here.
        for (const auto& meta : AbstractFactory<IResourceLoader>::GetInstance().GetRegisteredTypes()) {
            auto loader = AbstractFactory<IResourceLoader>::GetInstance().Create(meta);
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

    IResourceLoader* ResourceManager::FindLoaderForPath(eastl::string_view path) const {
        const size_t nameStart = path.find_last_of("/\\");
        const eastl::string_view name = nameStart == eastl::string_view::npos ? path : path.substr(nameStart + 1);
        for (size_t dot = name.find('.'); dot != eastl::string_view::npos; dot = name.find('.', dot + 1)) {
            if (auto* loader = FindLoader(name.substr(dot))) {
                return loader;
            }
        }
        return nullptr;
    }

}  // namespace BECore
