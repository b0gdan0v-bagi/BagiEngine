#include <BECore/Resource/XmlResourceLoader.h>
#include <BECore/RefCounted/New.h>
#include <TaskSystem/Awaitables.h>

#include <Generated/IResourceLoader.gen.hpp>
#include <Generated/XmlResourceLoader.gen.hpp>

namespace BECore {

    bool XmlResourceLoader::CanLoad(eastl::string_view extension) const {
        return extension == ".xml";
    }

    Task<IntrusivePtr<IResource>> XmlResourceLoader::LoadAsync(PoolString path) {
        // Run loading on thread pool
        co_await SwitchToBackground();
        
        auto resource = LoadInternal(path);
        co_return resource;
    }

    IntrusivePtr<IResource> XmlResourceLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    IntrusivePtr<XmlResource> XmlResourceLoader::LoadInternal(PoolString path) {
        auto resource = XmlResource::Create();
        
        // Create and load XmlConfig
        auto config = XmlConfig::Create();
        if (!config.LoadFromVirtualPath(path.ToStringView())) {
            LOG_ERROR(Format("Failed to load XML resource: {}", path.ToStringView()).c_str());
            resource->SetFailed(path);
            return resource;
        }
        
        // Mark as loaded
        resource->SetLoaded(path, std::move(config));
        LOG_INFO(Format("Loaded XML resource: {}", path.ToStringView()).c_str());
        
        return resource;
    }

}  // namespace BECore
