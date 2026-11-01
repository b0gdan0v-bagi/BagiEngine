#pragma once

#include <vulkan/vulkan.h>

namespace BECore {

    class VulkanRendererBackend;
    class VulkanTexture;

    /**
     * Turns RGBA8 pixels in memory into a VulkanTexture: staging buffer, device-local VkImage, view, sampler and
     * descriptor set. Shared by VulkanTextureLoader (pixels decoded from a file) and VulkanRendererBackend::CreateTexture()
     * (pixels made by the caller).
     *
     * Records and submits a command buffer of its own, so it runs on the main thread only.
     */
    class VulkanTextureUpload {
    public:
        VulkanTextureUpload() = delete;

        /// @p rgba holds width * height * 4 bytes, R,G,B,A in memory order. @p path names the texture in logs and in GetPath().
        /// The texture is in the Failed state when any step fails, never empty.
        static IntrusivePtr<VulkanTexture> Upload(VulkanRendererBackend& backend, PoolString path, const uint8_t* rgba, uint32_t width, uint32_t height);
    };

}  // namespace BECore
