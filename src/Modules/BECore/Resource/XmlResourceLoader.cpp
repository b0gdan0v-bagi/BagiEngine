#include <BECore/Resource/XmlResourceLoader.h>
#include <Generated/XmlResourceLoader.gen.hpp>
#include <TaskSystem/Awaitables.h>

namespace BECore {

    bool XmlResourceLoader::CanLoad(StringView extension) const {
        return extension == ".xml";
    }

    Task<IntrusivePtr<IResource>> XmlResourceLoader::LoadAsync(PoolString path) {
        co_await SwitchToBackground(TaskPriority::Normal, BE_TASK_LABEL("Load XML resource"));

        auto resource = LoadInternal(path);
        co_return resource;
    }

    IntrusivePtr<IResource> XmlResourceLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    IntrusivePtr<XmlResource> XmlResourceLoader::LoadInternal(PoolString path) {
        auto resource = XmlResource::Create();

        auto config = XmlConfig::Create();
        if (!config.LoadFromVirtualPath(path.ToStringView())) {
            LOG_ERROR("Failed to load XML resource: {}", path.ToStringView());
            resource->SetFailed(path);
            return resource;
        }

        resource->SetLoaded(path, std::move(config));
        LOG_INFO("Loaded XML resource: {}", path.ToStringView());

        return resource;
    }

}  // namespace BECore
