#pragma once

#include <BECore/Resource/IResourceLoader.h>

namespace BECore {

    class VulkanTexture;

    /**
     * Resource loader that creates VulkanTexture objects from image files.
     *
     * CanLoad() returns true only when the active renderer is VulkanRendererBackend,
     * so this loader yields to SDLTextureLoader when running the SDL backend.
     *
     * Images are decoded via SDL3_image (IMG_Load), converted to RGBA8, and
     * uploaded to device-local VkImage memory through a staging buffer.
     */
    class VulkanTextureLoader : public IResourceLoader {
        BE_CLASS(VulkanTextureLoader)
    public:
        VulkanTextureLoader() = default;
        ~VulkanTextureLoader() override = default;

        bool CanLoad(eastl::string_view extension) const override;
        Task<IntrusivePtr<IResource>> LoadAsync(PoolString path) override;
        IntrusivePtr<IResource> LoadSync(PoolString path) override;

    private:
        IntrusivePtr<VulkanTexture> LoadInternal(PoolString path);
    };

}  // namespace BECore
