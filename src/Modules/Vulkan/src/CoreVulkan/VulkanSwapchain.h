#pragma once

#include "VulkanSwapchainPolicy.h"

#include <vulkan/vulkan.h>
#include <EASTL/vector.h>

#include <optional>

namespace BECore {

    class VulkanDevice;

    using VulkanSwapchainPolicy::SwapchainRecreateResult;
    using VulkanSwapchainPolicy::SwapchainStatus;

    /**
     * Manages swapchain, image views, render pass, and framebuffers.
     *
     * The swapchain is the bridge between Vulkan rendering and the OS window.
     * Each swapchain image is wrapped with:
     *   - VkImageView    - how the GPU sees the image
     *   - VkFramebuffer  - ties the image view to a render pass
     *
     * The render pass is created once in Initialize() and kept by Recreate(): the pipelines (and ImGui's) were built
     * against it, so a recreated swapchain must have the same image format. The decisions (result classification,
     * extent, format) are pure functions in VulkanSwapchainPolicy.h.
     */
    class VulkanSwapchain {
    public:
        VulkanSwapchain() = default;
        ~VulkanSwapchain() = default;

        /// Sizes are in pixels of the window's surface (SDL_GetWindowSizeInPixels), not in window units.
        bool Initialize(VulkanDevice& device, VkSurfaceKHR surface, int pixelWidth, int pixelHeight);
        void Destroy(VulkanDevice& device);

        /// Rebuilds the swapchain for a new size from the old one (oldSwapchain), keeping the render pass. The old
        /// objects are destroyed only after the new ones exist. On Failed the new objects are released. The old
        /// swapchain stays in place, but once vkCreateSwapchainKHR has been called with it the spec retires it even if
        /// that call fails: the next acquire reports it out of date and the caller tries again. A Failed before that
        /// call (no surface format, capabilities not readable) leaves it untouched. The caller must call
        /// vkDeviceWaitIdle first: the old framebuffers and image views may still be in use by frames in flight. Sizes
        /// are in pixels.
        SwapchainRecreateResult Recreate(VulkanDevice& device, VkSurfaceKHR surface, int pixelWidth, int pixelHeight);

        /// Acquire the next swapchain image index. OutOfDate: recreate; the semaphore is not signaled then.
        /// Suboptimal: the image is valid and the semaphore signaled, so the frame must go on.
        SwapchainStatus AcquireNextImage(VulkanDevice& device, VkSemaphore imageAvailableSemaphore,
                                         uint32_t& outImageIndex);

        /// Present the rendered image
        SwapchainStatus Present(VulkanDevice& device, VkSemaphore renderFinishedSemaphore, uint32_t imageIndex);

        VkSwapchainKHR          GetSwapchain()      const { return _chain.swapchain; }
        VkRenderPass            GetRenderPass()     const { return _renderPass; }
        VkFramebuffer           GetFramebuffer(uint32_t index) const { return _chain.framebuffers[index]; }
        VkExtent2D              GetExtent()         const { return _chain.extent; }
        VkFormat                GetImageFormat()    const { return _chain.format; }
        uint32_t                GetImageCount()     const { return static_cast<uint32_t>(_chain.images.size()); }

    private:
        /// What a swapchain owns apart from the render pass
        struct Chain {
            VkSwapchainKHR               swapchain = VK_NULL_HANDLE;
            VkFormat                     format    = VK_FORMAT_UNDEFINED;
            VkExtent2D                   extent    = {};
            Vector<VkImage>       images;
            Vector<VkImageView>   imageViews;
            Vector<VkFramebuffer> framebuffers;
        };

        bool CreateSwapchain(VulkanDevice& device, VkSurfaceKHR surface, int pixelWidth, int pixelHeight,
                             Optional<VkFormat> keepFormat, VkSwapchainKHR oldSwapchain, Chain& out) const;
        bool CreateImageViews(VulkanDevice& device, Chain& chain) const;
        bool CreateRenderPass(VulkanDevice& device);
        bool CreateFramebuffers(VulkanDevice& device, Chain& chain) const;
        static void DestroyChain(VulkanDevice& device, Chain& chain);

        Optional<VkSurfaceFormatKHR> ChooseSurfaceFormat(VulkanDevice& device, VkSurfaceKHR surface,
                                                              Optional<VkFormat> keepFormat) const;
        VkPresentModeKHR                  ChoosePresentMode(VulkanDevice& device, VkSurfaceKHR surface) const;

        VkRenderPass _renderPass = VK_NULL_HANDLE;
        Chain        _chain;
    };

}  // namespace BECore
