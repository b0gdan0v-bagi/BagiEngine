#include "UIDocumentLoader.h"

#include <BECore/UI/UIDocument.h>
#include <Generated/UIDocumentLoader.gen.hpp>
#include <TaskSystem/Awaitables.h>

namespace BECore {

    bool UIDocumentLoader::CanLoad(eastl::string_view extension) const {
        return extension == ".ui.xml";
    }

    Task<IntrusivePtr<IResource>> UIDocumentLoader::LoadAsync(PoolString path) {
        co_await SwitchToBackground(TaskPriority::Normal, BE_TASK_LABEL("Load UI document"));
        co_return LoadInternal(path);
    }

    IntrusivePtr<IResource> UIDocumentLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    IntrusivePtr<UIDocument> UIDocumentLoader::LoadInternal(PoolString path) {
        auto document = UIDocument::Create();

        auto config = XmlConfig::Create();
        if (!config.LoadFromVirtualPath(path.ToStringView())) {
            LOG_ERROR("UIDocumentLoader: cannot load '{}'"_format(path.ToStringView()));
            document->SetFailed(path);
            return document;
        }

        document->SetLoaded(path, std::move(config));
        LOG_INFO("UIDocumentLoader: loaded '{}'"_format(path.ToStringView()));
        return document;
    }

}  // namespace BECore
