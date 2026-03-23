#pragma once

#include <BECore/Renderer/IRenderTarget.h>
#include <vulkan/vulkan.h>

namespace BECore {

    // Vulkan offscreen render target for render-to-texture.
    // Created by VulkanRendererBackend::CreateRenderTarget().
    class VulkanRenderTarget : public IRenderTarget {
    public:
        VulkanRenderTarget() = default;
        ~VulkanRenderTarget() override;

        bool Initialize(VkDevice device, VkPhysicalDevice physDevice,
                        VkFormat format, uint32_t width, uint32_t height);

        uint32_t GetWidth()  const override { return _width; }
        uint32_t GetHeight() const override { return _height; }
        void*    GetImGuiTextureId() const override { return _imguiTexId; }

        VkImage       GetImage()       const { return _image; }
        VkRenderPass  GetRenderPass()  const { return _renderPass; }
        VkFramebuffer GetFramebuffer() const { return _framebuffer; }
        VkExtent2D    GetExtent()      const { return {_width, _height}; }

    private:
        static uint32_t FindMemoryType(VkPhysicalDevice physDevice,
                                       uint32_t typeFilter,
                                       VkMemoryPropertyFlags properties);

        VkDevice      _device      = VK_NULL_HANDLE;
        VkImage       _image       = VK_NULL_HANDLE;
        VkDeviceMemory _memory     = VK_NULL_HANDLE;
        VkImageView   _imageView   = VK_NULL_HANDLE;
        VkSampler     _sampler     = VK_NULL_HANDLE;
        VkRenderPass  _renderPass  = VK_NULL_HANDLE;
        VkFramebuffer _framebuffer = VK_NULL_HANDLE;
        void*         _imguiTexId  = nullptr;
        uint32_t      _width       = 0;
        uint32_t      _height      = 0;
    };

}  // namespace BECore
