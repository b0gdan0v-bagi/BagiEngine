#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Resource/IResourceLoader.h>
#include <BECore/Resource/XmlResource.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief Loads `.xml` files into XmlResource.
     *
     * A file that does not load gives an XmlResource in state Failed, not nullptr (which IResourceLoader allows),
     * so ResourceManager caches it like any other.
     */
    class XmlResourceLoader : public IResourceLoader {
        BE_CLASS(XmlResourceLoader)
    public:
        XmlResourceLoader() = default;
        ~XmlResourceLoader() override = default;

        bool CanLoad(StringView extension) const override;

        Task<IntrusivePtr<IResource>> LoadAsync(PoolString path) override;

        IntrusivePtr<IResource> LoadSync(PoolString path) override;

    private:
        IntrusivePtr<XmlResource> LoadInternal(PoolString path);
    };

}  // namespace BECore
