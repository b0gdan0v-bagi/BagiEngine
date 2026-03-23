#pragma once

#include <BECore/Resource/IResourceLoader.h>

namespace BECore {

    class SDLTexture;

    class SDLTextureLoader : public IResourceLoader {
        BE_CLASS(SDLTextureLoader)
    public:
        SDLTextureLoader() = default;
        ~SDLTextureLoader() override = default;

        bool CanLoad(eastl::string_view extension) const override;
        Task<IntrusivePtr<IResource>> LoadAsync(PoolString path) override;
        IntrusivePtr<IResource> LoadSync(PoolString path) override;

    private:
        IntrusivePtr<SDLTexture> LoadInternal(PoolString path);
    };

}  // namespace BECore
