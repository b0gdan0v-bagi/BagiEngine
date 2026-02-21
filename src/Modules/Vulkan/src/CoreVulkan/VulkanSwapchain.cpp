#include "VulkanSwapchain.h"
#include "VulkanDevice.h"

#include <BECore/Logger/LogEvent.h>

#include <algorithm>
#include <limits>

namespace BECore {

    bool VulkanSwapchain::Initialize(VulkanDevice& device, VkSurfaceKHR surface, int width, int height) {
        if (!CreateSwapchain(device, surface, width, height)) {
            LOG_ERROR("Vulkan: Failed to create swapchain");
            return false;
        }
        if (!CreateImageViews(device)) {
            LOG_ERROR("Vulkan: Failed to create swapchain image views");
            return false;
        }
        if (!CreateRenderPass(device)) {
            LOG_ERROR("Vulkan: Failed to create render pass");
            return false;
        }
        if (!CreateFramebuffers(device)) {
            LOG_ERROR("Vulkan: Failed to create framebuffers");
            return false;
        }
        return true;
    }

    void VulkanSwapchain::Destroy(VulkanDevice& device) {
        VkDevice vkDevice = device.GetDevice();

        for (VkFramebuffer fb : _framebuffers) {
            vkDestroyFramebuffer(vkDevice, fb, nullptr);
        }
        _framebuffers.clear();

        if (_renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(vkDevice, _renderPass, nullptr);
            _renderPass = VK_NULL_HANDLE;
        }

        for (VkImageView view : _imageViews) {
            vkDestroyImageView(vkDevice, view, nullptr);
        }
        _imageViews.clear();

        if (_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(vkDevice, _swapchain, nullptr);
            _swapchain = VK_NULL_HANDLE;
        }

        _images.clear();
    }

    bool VulkanSwapchain::AcquireNextImage(VulkanDevice& device, VkSemaphore imageAvailableSemaphore,
                                            uint32_t& outImageIndex) {
        const VkResult result = vkAcquireNextImageKHR(
            device.GetDevice(), _swapchain,
            UINT64_MAX,               // no timeout
            imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &outImageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return false;  // swapchain needs recreation
        }
        return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
    }

    bool VulkanSwapchain::Present(VulkanDevice& device, VkSemaphore renderFinishedSemaphore,
                                   uint32_t imageIndex) {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = &renderFinishedSemaphore;
        presentInfo.swapchainCount     = 1;
        presentInfo.pSwapchains        = &_swapchain;
        presentInfo.pImageIndices      = &imageIndex;

        const VkResult result = vkQueuePresentKHR(device.GetPresentQueue(), &presentInfo);
        return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
    }

    bool VulkanSwapchain::CreateSwapchain(VulkanDevice& device, VkSurfaceKHR surface,
                                           int width, int height) {
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.GetPhysicalDevice(), surface, &caps);

        const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(device, surface);
        const VkPresentModeKHR   presentMode   = ChoosePresentMode(device, surface);
        const VkExtent2D         extent        = ChooseExtent(caps, width, height);

        // Request one more image than the minimum to avoid waiting on the driver
        uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
            imageCount = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface          = surface;
        createInfo.minImageCount    = imageCount;
        createInfo.imageFormat      = surfaceFormat.format;
        createInfo.imageColorSpace  = surfaceFormat.colorSpace;
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
        createInfo.oldSwapchain   = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(device.GetDevice(), &createInfo, nullptr, &_swapchain) != VK_SUCCESS) {
            return false;
        }

        // Retrieve swapchain images (driver may allocate more than requested)
        uint32_t actualCount = 0;
        vkGetSwapchainImagesKHR(device.GetDevice(), _swapchain, &actualCount, nullptr);
        _images.resize(actualCount);
        vkGetSwapchainImagesKHR(device.GetDevice(), _swapchain, &actualCount, _images.data());

        _imageFormat = surfaceFormat.format;
        _extent      = extent;
        return true;
    }

    bool VulkanSwapchain::CreateImageViews(VulkanDevice& device) {
        _imageViews.resize(_images.size());
        for (size_t i = 0; i < _images.size(); ++i) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image    = _images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format   = _imageFormat;

            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel   = 0;
            viewInfo.subresourceRange.levelCount     = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount     = 1;

            if (vkCreateImageView(device.GetDevice(), &viewInfo, nullptr, &_imageViews[i]) != VK_SUCCESS) {
                return false;
            }
        }
        return true;
    }

    bool VulkanSwapchain::CreateRenderPass(VulkanDevice& device) {
        // Single color attachment, cleared at load, stored at end
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = _imageFormat;
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

    bool VulkanSwapchain::CreateFramebuffers(VulkanDevice& device) {
        _framebuffers.resize(_imageViews.size());
        for (size_t i = 0; i < _imageViews.size(); ++i) {
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass      = _renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments    = &_imageViews[i];
            fbInfo.width           = _extent.width;
            fbInfo.height          = _extent.height;
            fbInfo.layers          = 1;

            if (vkCreateFramebuffer(device.GetDevice(), &fbInfo, nullptr, &_framebuffers[i]) != VK_SUCCESS) {
                return false;
            }
        }
        return true;
    }

    VkSurfaceFormatKHR VulkanSwapchain::ChooseSurfaceFormat(
        VulkanDevice& device, VkSurfaceKHR surface) const {

        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device.GetPhysicalDevice(), surface, &count, nullptr);
        eastl::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device.GetPhysicalDevice(), surface, &count, formats.data());

        // Prefer standard SRGB with BGRA8 (most common on desktop)
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return f;
            }
        }
        return formats[0];
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

    VkExtent2D VulkanSwapchain::ChooseExtent(
        const VkSurfaceCapabilitiesKHR& caps, int width, int height) const {

        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return caps.currentExtent;
        }

        VkExtent2D extent{
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };
        extent.width  = std::clamp(extent.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
        extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        return extent;
    }

}  // namespace BECore
