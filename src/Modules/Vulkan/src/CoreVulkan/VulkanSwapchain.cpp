#include "VulkanSwapchain.h"
#include "VulkanDevice.h"

#include <BECore/Logger/LogEvent.h>

#include <optional>

namespace BECore {

    bool VulkanSwapchain::Initialize(VulkanDevice& device, VkSurfaceKHR surface, int pixelWidth, int pixelHeight) {
        if (!CreateSwapchain(device, surface, pixelWidth, pixelHeight, std::nullopt, VK_NULL_HANDLE, _chain)) {
            LOG_ERROR("Vulkan: Failed to create swapchain");
            return false;
        }
        if (!CreateImageViews(device, _chain)) {
            LOG_ERROR("Vulkan: Failed to create swapchain image views");
            return false;
        }
        if (!CreateRenderPass(device)) {
            LOG_ERROR("Vulkan: Failed to create render pass");
            return false;
        }
        if (!CreateFramebuffers(device, _chain)) {
            LOG_ERROR("Vulkan: Failed to create framebuffers");
            return false;
        }
        return true;
    }

    void VulkanSwapchain::Destroy(VulkanDevice& device) {
        const VkDevice vkDevice = device.GetDevice();

        // Framebuffers first (they reference the render pass and the views), the swapchain last
        for (VkFramebuffer fb : _chain.framebuffers) {
            vkDestroyFramebuffer(vkDevice, fb, nullptr);
        }
        _chain.framebuffers.clear();

        if (_renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(vkDevice, _renderPass, nullptr);
            _renderPass = VK_NULL_HANDLE;
        }

        DestroyChain(device, _chain);
    }

    void VulkanSwapchain::DestroyChain(VulkanDevice& device, Chain& chain) {
        const VkDevice vkDevice = device.GetDevice();

        for (VkFramebuffer fb : chain.framebuffers) {
            vkDestroyFramebuffer(vkDevice, fb, nullptr);
        }
        chain.framebuffers.clear();

        for (VkImageView view : chain.imageViews) {
            vkDestroyImageView(vkDevice, view, nullptr);
        }
        chain.imageViews.clear();

        if (chain.swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(vkDevice, chain.swapchain, nullptr);
            chain.swapchain = VK_NULL_HANDLE;
        }

        chain.images.clear();
    }

    SwapchainRecreateResult VulkanSwapchain::Recreate(VulkanDevice& device, VkSurfaceKHR surface, int pixelWidth,
                                                      int pixelHeight) {
        if (VulkanSwapchainPolicy::IsMinimized(pixelWidth, pixelHeight)) {
            return SwapchainRecreateResult::ZeroExtent;
        }

        // The surface may report a zero extent before the window's pixel size does (a window that is being minimized)
        VkSurfaceCapabilitiesKHR caps{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.GetPhysicalDevice(), surface, &caps) != VK_SUCCESS) {
            LOG_ERROR("Vulkan: Failed to query the surface capabilities for the recreated swapchain");
            return SwapchainRecreateResult::Failed;
        }
        const VkExtent2D extent = VulkanSwapchainPolicy::ChooseExtent(caps, pixelWidth, pixelHeight);
        if (extent.width == 0 || extent.height == 0) {
            return SwapchainRecreateResult::ZeroExtent;
        }

        Chain next;
        if (!CreateSwapchain(device, surface, pixelWidth, pixelHeight, _chain.format, _chain.swapchain, next)) {
            return SwapchainRecreateResult::Failed;
        }
        if (!CreateImageViews(device, next) || !CreateFramebuffers(device, next)) {
            LOG_ERROR("Vulkan: Failed to create image views or framebuffers for the recreated swapchain");
            DestroyChain(device, next);
            return SwapchainRecreateResult::Failed;
        }

        // The new objects exist: now the old ones can go (the caller has waited for the device to be idle)
        DestroyChain(device, _chain);
        _chain = eastl::move(next);
        return SwapchainRecreateResult::Done;
    }

    SwapchainStatus VulkanSwapchain::AcquireNextImage(VulkanDevice& device, VkSemaphore imageAvailableSemaphore,
                                                      uint32_t& outImageIndex) {
        const VkResult result = vkAcquireNextImageKHR(
            device.GetDevice(), _chain.swapchain,
            UINT64_MAX,               // no timeout
            imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &outImageIndex);

        return VulkanSwapchainPolicy::ClassifyResult(result);
    }

    SwapchainStatus VulkanSwapchain::Present(VulkanDevice& device, VkSemaphore renderFinishedSemaphore,
                                             uint32_t imageIndex) {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = &renderFinishedSemaphore;
        presentInfo.swapchainCount     = 1;
        presentInfo.pSwapchains        = &_chain.swapchain;
        presentInfo.pImageIndices      = &imageIndex;

        return VulkanSwapchainPolicy::ClassifyResult(vkQueuePresentKHR(device.GetPresentQueue(), &presentInfo));
    }

    bool VulkanSwapchain::CreateSwapchain(VulkanDevice& device, VkSurfaceKHR surface, int pixelWidth,
                                          int pixelHeight, Optional<VkFormat> keepFormat,
                                          VkSwapchainKHR oldSwapchain, Chain& out) const {
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.GetPhysicalDevice(), surface, &caps);

        const Optional<VkSurfaceFormatKHR> surfaceFormat = ChooseSurfaceFormat(device, surface, keepFormat);
        if (!surfaceFormat) {
            LOG_ERROR("Vulkan: the surface offers no formats");
            return false;
        }
        if (keepFormat && surfaceFormat->format != *keepFormat) {
            // The render pass and the pipelines were built for the old format and are not rebuilt
            LOG_ERROR("Vulkan: the surface no longer offers the swapchain format, cannot recreate the swapchain");
            return false;
        }
        const VkPresentModeKHR presentMode = ChoosePresentMode(device, surface);
        const VkExtent2D       extent      = VulkanSwapchainPolicy::ChooseExtent(caps, pixelWidth, pixelHeight);

        // Request one more image than the minimum to avoid waiting on the driver
        uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
            imageCount = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface          = surface;
        createInfo.minImageCount    = imageCount;
        createInfo.imageFormat      = surfaceFormat->format;
        createInfo.imageColorSpace  = surfaceFormat->colorSpace;
        createInfo.imageExtent      = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        const auto& qf = device.GetQueueFamilies();
        if (qf.graphicsFamily != qf.presentFamily) {
            // Images shared between graphics and present queues
            const uint32_t indices[] = {qf.graphicsFamily, qf.presentFamily};
            createInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices   = indices;
        } else {
            // Single queue family owns the images exclusively (better performance)
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform   = caps.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode    = presentMode;
        createInfo.clipped        = VK_TRUE;
        createInfo.oldSwapchain   = oldSwapchain;

        const VkResult created = vkCreateSwapchainKHR(device.GetDevice(), &createInfo, nullptr, &out.swapchain);
        if (created != VK_SUCCESS) {
            LOG_ERROR(Format("Vulkan: vkCreateSwapchainKHR failed (VkResult {})", static_cast<int>(created)).c_str());
            out.swapchain = VK_NULL_HANDLE;
            return false;
        }

        // Retrieve swapchain images (driver may allocate more than requested)
        uint32_t actualCount = 0;
        vkGetSwapchainImagesKHR(device.GetDevice(), out.swapchain, &actualCount, nullptr);
        out.images.resize(actualCount);
        vkGetSwapchainImagesKHR(device.GetDevice(), out.swapchain, &actualCount, out.images.data());

        out.format = surfaceFormat->format;
        out.extent = extent;
        return true;
    }

    bool VulkanSwapchain::CreateImageViews(VulkanDevice& device, Chain& chain) const {
        chain.imageViews.assign(chain.images.size(), VK_NULL_HANDLE);
        for (size_t i = 0; i < chain.images.size(); ++i) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image    = chain.images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format   = chain.format;

            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel   = 0;
            viewInfo.subresourceRange.levelCount     = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount     = 1;

            if (vkCreateImageView(device.GetDevice(), &viewInfo, nullptr, &chain.imageViews[i]) != VK_SUCCESS) {
                return false;
            }
        }
        return true;
    }

    bool VulkanSwapchain::CreateRenderPass(VulkanDevice& device) {
        // Single color attachment, cleared at load, stored at end
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = _chain.format;
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        // Dependency ensures layout transition happens after the swapchain image is acquired
        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments    = &colorAttachment;
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        return vkCreateRenderPass(device.GetDevice(), &renderPassInfo, nullptr, &_renderPass) == VK_SUCCESS;
    }

    bool VulkanSwapchain::CreateFramebuffers(VulkanDevice& device, Chain& chain) const {
        chain.framebuffers.assign(chain.imageViews.size(), VK_NULL_HANDLE);
        for (size_t i = 0; i < chain.imageViews.size(); ++i) {
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass      = _renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments    = &chain.imageViews[i];
            fbInfo.width           = chain.extent.width;
            fbInfo.height          = chain.extent.height;
            fbInfo.layers          = 1;

            if (vkCreateFramebuffer(device.GetDevice(), &fbInfo, nullptr, &chain.framebuffers[i]) != VK_SUCCESS) {
                return false;
            }
        }
        return true;
    }

    Optional<VkSurfaceFormatKHR> VulkanSwapchain::ChooseSurfaceFormat(
        VulkanDevice& device, VkSurfaceKHR surface, Optional<VkFormat> keepFormat) const {

        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device.GetPhysicalDevice(), surface, &count, nullptr);
        eastl::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device.GetPhysicalDevice(), surface, &count, formats.data());

        return VulkanSwapchainPolicy::ChooseSurfaceFormat({formats.data(), formats.size()}, keepFormat);
    }

    VkPresentModeKHR VulkanSwapchain::ChoosePresentMode(
        VulkanDevice& device, VkSurfaceKHR surface) const {

        uint32_t count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device.GetPhysicalDevice(), surface, &count, nullptr);
        eastl::vector<VkPresentModeKHR> modes(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device.GetPhysicalDevice(), surface, &count, modes.data());

        // Prefer mailbox (triple-buffered, low latency)
        for (const auto& mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
        // FIFO is always available (v-sync)
        return VK_PRESENT_MODE_FIFO_KHR;
    }

}  // namespace BECore
