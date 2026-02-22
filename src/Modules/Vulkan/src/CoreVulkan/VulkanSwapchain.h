#pragma once

#include <vulkan/vulkan.h>
#include <EASTL/vector.h>

namespace BECore {

    class VulkanDevice;

    /**
     * Manages swapchain, image views, render pass, and framebuffers.
     *
     * The swapchain is the bridge between Vulkan rendering and the OS window.
     * Each swapchain image is wrapped with:
     *   - VkImageView    - how the GPU sees the image
     *   - VkFramebuffer  - ties the image view to a render pass
     */
    class VulkanSwapchain {
    public:
        VulkanSwapchain() = default;
        ~VulkanSwapchain() = default;

        bool Initialize(VulkanDevice& device, VkSurfaceKHR surface, int width, int height);
        void Destroy(VulkanDevice& device);

        // Acquire the next swapchain image index. Returns false on out-of-date swapchain.
        bool AcquireNextImage(VulkanDevice& device, VkSemaphore imageAvailableSemaphore,
                              uint32_t& outImageIndex);

        // Present the rendered image. Returns false on out-of-date swapchain.
        bool Present(VulkanDevice& device, VkSemaphore renderFinishedSemaphore,
                     uint32_t imageIndex);

        VkSwapchainKHR          GetSwapchain()      const { return _swapchain; }
        VkRenderPass            GetRenderPass()     const { return _renderPass; }
        VkFramebuffer           GetFramebuffer(uint32_t index) const { return _framebuffers[index]; }
        VkExtent2D              GetExtent()         const { return _extent; }
        VkFormat                GetImageFormat()    const { return _imageFormat; }
        uint32_t                GetImageCount()     const { return static_cast<uint32_t>(_images.size()); }

    private:
        bool CreateSwapchain(VulkanDevice& device, VkSurfaceKHR surface, int width, int height);
        bool CreateImageViews(VulkanDevice& device);
        bool CreateRenderPass(VulkanDevice& device);
        bool CreateFramebuffers(VulkanDevice& device);

        VkSurfaceFormatKHR ChooseSurfaceFormat(VulkanDevice& device, VkSurfaceKHR surface) const;
        VkPresentModeKHR   ChoosePresentMode(VulkanDevice& device, VkSurfaceKHR surface) const;
        VkExtent2D         ChooseExtent(const VkSurfaceCapabilitiesKHR& caps, int width, int height) const;

        VkSwapchainKHR             _swapchain   = VK_NULL_HANDLE;
        VkRenderPass               _renderPass  = VK_NULL_HANDLE;
        VkFormat                   _imageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D                 _extent      = {};
        eastl::vector<VkImage>     _images;
        eastl::vector<VkImageView> _imageViews;
        eastl::vector<VkFramebuffer> _framebuffers;
    };

}  // namespace BECore
