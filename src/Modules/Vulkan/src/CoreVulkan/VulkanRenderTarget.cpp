#include "VulkanRenderTarget.h"

#include <BECore/Logger/LogEvent.h>
#include <backends/imgui_impl_vulkan.h>

namespace BECore {

    VulkanRenderTarget::~VulkanRenderTarget() {
        if (_device == VK_NULL_HANDLE) {
            return;
        }

        vkDeviceWaitIdle(_device);

        if (_imguiTexId) {
            ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(_imguiTexId));
            _imguiTexId = nullptr;
        }
        if (_framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(_device, _framebuffer, nullptr);
        }
        if (_renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(_device, _renderPass, nullptr);
        }
        if (_sampler != VK_NULL_HANDLE) {
            vkDestroySampler(_device, _sampler, nullptr);
        }
        if (_imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(_device, _imageView, nullptr);
        }
        if (_image != VK_NULL_HANDLE) {
            vkDestroyImage(_device, _image, nullptr);
        }
        if (_memory != VK_NULL_HANDLE) {
            vkFreeMemory(_device, _memory, nullptr);
        }
    }

    bool VulkanRenderTarget::Initialize(VkDevice device, VkPhysicalDevice physDevice,
                                         VkFormat format, uint32_t width, uint32_t height) {
        _device = device;
        _width  = width;
        _height = height;

        // --- Image ---
        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_2D;
        imgInfo.format        = format;
        imgInfo.extent        = {width, height, 1};
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &imgInfo, nullptr, &_image) != VK_SUCCESS) {
            LOG_ERROR("VulkanRenderTarget: vkCreateImage failed");
            return false;
        }

        // --- Memory ---
        VkMemoryRequirements memReqs{};
        vkGetImageMemoryRequirements(device, _image, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physDevice, memReqs.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (allocInfo.memoryTypeIndex == UINT32_MAX) {
            LOG_ERROR("VulkanRenderTarget: no suitable memory type");
            return false;
        }
        if (vkAllocateMemory(device, &allocInfo, nullptr, &_memory) != VK_SUCCESS) {
            LOG_ERROR("VulkanRenderTarget: vkAllocateMemory failed");
            return false;
        }
        vkBindImageMemory(device, _image, _memory, 0);

        // --- Image view ---
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = _image;
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = format;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &_imageView) != VK_SUCCESS) {
            LOG_ERROR("VulkanRenderTarget: vkCreateImageView failed");
            return false;
        }

        // --- Render pass ---
        // finalLayout = SHADER_READ_ONLY_OPTIMAL so the image is sampling-ready after EndRenderPass.
        VkAttachmentDescription colorAtt{};
        colorAtt.format         = format;
        colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments    = &colorAtt;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dep;
        if (vkCreateRenderPass(device, &rpInfo, nullptr, &_renderPass) != VK_SUCCESS) {
            LOG_ERROR("VulkanRenderTarget: vkCreateRenderPass failed");
            return false;
        }

        // --- Framebuffer ---
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = _renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = &_imageView;
        fbInfo.width           = width;
        fbInfo.height          = height;
        fbInfo.layers          = 1;
        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &_framebuffer) != VK_SUCCESS) {
            LOG_ERROR("VulkanRenderTarget: vkCreateFramebuffer failed");
            return false;
        }

        // --- Sampler ---
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter    = VK_FILTER_LINEAR;
        samplerInfo.minFilter    = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &_sampler) != VK_SUCCESS) {
            LOG_ERROR("VulkanRenderTarget: vkCreateSampler failed");
            return false;
        }

        // --- ImGui descriptor set ---
        _imguiTexId = ImGui_ImplVulkan_AddTexture(_sampler, _imageView,
                                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!_imguiTexId) {
            LOG_ERROR("VulkanRenderTarget: ImGui_ImplVulkan_AddTexture failed");
            return false;
        }

        return true;
    }

    uint32_t VulkanRenderTarget::FindMemoryType(VkPhysicalDevice physDevice,
                                                  uint32_t typeFilter,
                                                  VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return UINT32_MAX;
    }

}  // namespace BECore
