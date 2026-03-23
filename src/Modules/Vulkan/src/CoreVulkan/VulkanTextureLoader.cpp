#include "VulkanTextureLoader.h"

#include <BECore/GameManager/CoreManager.h>
#include <CoreVulkan/VulkanDevice.h>
#include <CoreVulkan/VulkanRendererBackend.h>
#include <CoreVulkan/VulkanTexture.h>
#include <CoreVulkan/VulkanTexturePipeline.h>
#include <Generated/VulkanTextureLoader.gen.hpp>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace BECore {

    bool VulkanTextureLoader::CanLoad(eastl::string_view extension) const {
        if (extension != ".bmp" && extension != ".png" && extension != ".jpg" && extension != ".jpeg") {
            return false;
        }
        // Only handle textures when the active renderer is the Vulkan backend.
        const auto& renderer = CoreManager::GetRenderer();
        return renderer && dynamic_cast<VulkanRendererBackend*>(renderer.Get()) != nullptr;
    }

    Task<IntrusivePtr<IResource>> VulkanTextureLoader::LoadAsync(PoolString path) {
        co_return LoadInternal(path);
    }

    IntrusivePtr<IResource> VulkanTextureLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    namespace {

        struct StagingBuffer {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
        };

        StagingBuffer CreateStagingBuffer(VkDevice device, const VulkanDevice& vkDevice, VkDeviceSize size, const void* data) {
            StagingBuffer staging{};

            VkBufferCreateInfo bufInfo{};
            bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufInfo.size = size;
            bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device, &bufInfo, nullptr, &staging.buffer) != VK_SUCCESS) {
                return staging;
            }

            VkMemoryRequirements memReqs{};
            vkGetBufferMemoryRequirements(device, staging.buffer, &memReqs);

            const uint32_t memType = vkDevice.FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            if (memType == UINT32_MAX) {
                vkDestroyBuffer(device, staging.buffer, nullptr);
                staging.buffer = VK_NULL_HANDLE;
                return staging;
            }

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReqs.size;
            allocInfo.memoryTypeIndex = memType;

            if (vkAllocateMemory(device, &allocInfo, nullptr, &staging.memory) != VK_SUCCESS) {
                vkDestroyBuffer(device, staging.buffer, nullptr);
                staging.buffer = VK_NULL_HANDLE;
                return staging;
            }

            vkBindBufferMemory(device, staging.buffer, staging.memory, 0);

            void* mapped = nullptr;
            vkMapMemory(device, staging.memory, 0, size, 0, &mapped);
            memcpy(mapped, data, static_cast<size_t>(size));
            vkUnmapMemory(device, staging.memory);

            return staging;
        }

        void TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkPipelineStageFlags srcStage = 0;
            VkPipelineStageFlags dstStage = 0;

            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }

            vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

    }  // namespace

    IntrusivePtr<VulkanTexture> VulkanTextureLoader::LoadInternal(PoolString path) {
        auto texture = New<VulkanTexture>();

        auto* backend = dynamic_cast<VulkanRendererBackend*>(CoreManager::GetRenderer().Get());
        if (!backend) {
            LOG_ERROR(Format("VulkanTextureLoader: renderer is not VulkanRendererBackend when loading '{}'", path.ToStringView()).c_str());
            texture->SetFailed(path);
            return texture;
        }

        const auto realPath = CoreManager::GetFileSystem().ResolvePath(path.ToStringView());
        SDL_Surface* rawSurface = IMG_Load(realPath.string().c_str());
        if (!rawSurface) {
            LOG_ERROR(Format("VulkanTextureLoader: failed to load image '{}': {}", path.ToStringView(), SDL_GetError()).c_str());
            texture->SetFailed(path);
            return texture;
        }

        // Always work with RGBA8 to match VK_FORMAT_R8G8B8A8_SRGB.
        SDL_Surface* surface = SDL_ConvertSurface(rawSurface, SDL_PIXELFORMAT_RGBA8888);
        SDL_DestroySurface(rawSurface);
        if (!surface) {
            LOG_ERROR(Format("VulkanTextureLoader: failed to convert surface '{}': {}", path.ToStringView(), SDL_GetError()).c_str());
            texture->SetFailed(path);
            return texture;
        }

        const uint32_t texWidth = static_cast<uint32_t>(surface->w);
        const uint32_t texHeight = static_cast<uint32_t>(surface->h);
        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;

        VkDevice vkDevice = backend->GetVkDevice();
        const VulkanDevice& deviceObj = backend->GetVulkanDevice();
        VkCommandPool commandPool = backend->GetCommandPool();
        VulkanTexturePipeline& pipeline = backend->GetTexturePipeline();

        // ---- Staging buffer -------------------------------------------------
        StagingBuffer staging = CreateStagingBuffer(vkDevice, deviceObj, imageSize, surface->pixels);
        SDL_DestroySurface(surface);

        if (staging.buffer == VK_NULL_HANDLE) {
            LOG_ERROR(Format("VulkanTextureLoader: failed to create staging buffer for '{}'", path.ToStringView()).c_str());
            texture->SetFailed(path);
            return texture;
        }

        // ---- Device-local image ---------------------------------------------
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.extent = {texWidth, texHeight, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImage image = VK_NULL_HANDLE;
        if (vkCreateImage(vkDevice, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            vkDestroyBuffer(vkDevice, staging.buffer, nullptr);
            vkFreeMemory(vkDevice, staging.memory, nullptr);
            LOG_ERROR(Format("VulkanTextureLoader: failed to create VkImage for '{}'", path.ToStringView()).c_str());
            texture->SetFailed(path);
            return texture;
        }

        VkMemoryRequirements memReqs{};
        vkGetImageMemoryRequirements(vkDevice, image, &memReqs);

        const uint32_t memType = deviceObj.FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memType == UINT32_MAX) {
            vkDestroyImage(vkDevice, image, nullptr);
            vkDestroyBuffer(vkDevice, staging.buffer, nullptr);
            vkFreeMemory(vkDevice, staging.memory, nullptr);
            LOG_ERROR(Format("VulkanTextureLoader: no suitable memory type for '{}'", path.ToStringView()).c_str());
            texture->SetFailed(path);
            return texture;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = memType;

        VkDeviceMemory imageMemory = VK_NULL_HANDLE;
        if (vkAllocateMemory(vkDevice, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
            vkDestroyImage(vkDevice, image, nullptr);
            vkDestroyBuffer(vkDevice, staging.buffer, nullptr);
            vkFreeMemory(vkDevice, staging.memory, nullptr);
            LOG_ERROR(Format("VulkanTextureLoader: failed to allocate image memory for '{}'", path.ToStringView()).c_str());
            texture->SetFailed(path);
            return texture;
        }
        vkBindImageMemory(vkDevice, image, imageMemory, 0);

        // ---- Upload via single-time command buffer --------------------------
        {
            VkCommandBuffer cmd = deviceObj.BeginSingleTimeCommands(commandPool);

            TransitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkBufferImageCopy copyRegion{};
            copyRegion.bufferOffset = 0;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.mipLevel = 0;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageOffset = {0, 0, 0};
            copyRegion.imageExtent = {texWidth, texHeight, 1};

            vkCmdCopyBufferToImage(cmd, staging.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            TransitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            deviceObj.EndSingleTimeCommands(commandPool, cmd);
        }

        vkDestroyBuffer(vkDevice, staging.buffer, nullptr);
        vkFreeMemory(vkDevice, staging.memory, nullptr);

        // ---- Image view -----------------------------------------------------
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView = VK_NULL_HANDLE;
        if (vkCreateImageView(vkDevice, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            vkDestroyImage(vkDevice, image, nullptr);
            vkFreeMemory(vkDevice, imageMemory, nullptr);
            LOG_ERROR(Format("VulkanTextureLoader: failed to create image view for '{}'", path.ToStringView()).c_str());
            texture->SetFailed(path);
            return texture;
        }

        // ---- Sampler --------------------------------------------------------
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        VkSampler sampler = VK_NULL_HANDLE;
        if (vkCreateSampler(vkDevice, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
            vkDestroyImageView(vkDevice, imageView, nullptr);
            vkDestroyImage(vkDevice, image, nullptr);
            vkFreeMemory(vkDevice, imageMemory, nullptr);
            LOG_ERROR(Format("VulkanTextureLoader: failed to create sampler for '{}'", path.ToStringView()).c_str());
            texture->SetFailed(path);
            return texture;
        }

        // ---- Descriptor set -------------------------------------------------
        VkDescriptorSet descriptorSet = pipeline.AllocateDescriptorSet(vkDevice, imageView, sampler);
        if (descriptorSet == VK_NULL_HANDLE) {
            vkDestroySampler(vkDevice, sampler, nullptr);
            vkDestroyImageView(vkDevice, imageView, nullptr);
            vkDestroyImage(vkDevice, image, nullptr);
            vkFreeMemory(vkDevice, imageMemory, nullptr);
            texture->SetFailed(path);
            return texture;
        }

        texture->SetLoaded(path, vkDevice, pipeline.GetDescriptorPool(), image, imageMemory, imageView, sampler, descriptorSet, static_cast<float>(texWidth), static_cast<float>(texHeight));

        LOG_INFO(Format("VulkanTextureLoader: loaded texture '{}' ({}x{})", path.ToStringView(), texWidth, texHeight).c_str());
        return texture;
    }

}  // namespace BECore
