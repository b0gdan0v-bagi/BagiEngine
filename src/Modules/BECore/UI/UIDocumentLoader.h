#pragma once

#include <BECore/Resource/IResourceLoader.h>

namespace BECore {

    class UIDocument;

    /**
     * @brief Loads `.ui.xml` files into UIDocument resources.
     *
     * Parses the XML only (no element is created and no GPU is touched), so it may run on a pool thread. ResourceManager picks it for "x.ui.xml" before
     * XmlResourceLoader, which takes the plain ".xml".
     */
    class UIDocumentLoader : public IResourceLoader {
        BE_CLASS(UIDocumentLoader)
    public:
        UIDocumentLoader() = default;
        ~UIDocumentLoader() override = default;

        bool CanLoad(StringView extension) const override;
        Task<IntrusivePtr<IResource>> LoadAsync(PoolString path) override;
        IntrusivePtr<IResource> LoadSync(PoolString path) override;

    private:
        IntrusivePtr<UIDocument> LoadInternal(PoolString path);
    };

}  // namespace BECore
