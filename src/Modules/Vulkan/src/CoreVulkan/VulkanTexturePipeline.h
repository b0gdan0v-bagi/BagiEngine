#pragma once

#include <vulkan/vulkan.h>

namespace BECore {

    /**
     * Vulkan graphics pipeline for drawing axis-aligned textured quads.
     *
     * Like VulkanRectPipeline, geometry is generated entirely in the vertex
     * shader (6 vertices, no VBO). The texture is bound via descriptor set 0,
     * binding 0 (combined image sampler).
     *
     * Push constant layout (40 bytes — same total as VulkanRectPipeline):
     *   offset  0 : vec4  dstRect    — x, y, w, h in pixels
     *   offset 16 : vec4  srcUV      — u0, v0, u1, v1 in [0, 1]
     *   offset 32 : vec2  screenSize — viewport width, height in pixels
     */
    class VulkanTexturePipeline {
    public:
        struct PushConstants {
            float dstX, dstY, dstW, dstH;
            float u0, v0, u1, v1;
            float screenW, screenH;
        };

        VulkanTexturePipeline() = default;
        ~VulkanTexturePipeline() = default;

        VulkanTexturePipeline(const VulkanTexturePipeline&) = delete;
        VulkanTexturePipeline& operator=(const VulkanTexturePipeline&) = delete;

        bool Initialize(VkDevice device, VkRenderPass renderPass);
        void Destroy(VkDevice device);

        // Allocates and writes a descriptor set for the given image view + sampler.
        // The pool uses VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT so the
        // returned set can be freed individually (see VulkanTexture destructor).
        VkDescriptorSet AllocateDescriptorSet(VkDevice device, VkImageView imageView, VkSampler sampler);

        VkDescriptorSetLayout GetDescriptorSetLayout() const {
            return _descriptorSetLayout;
        }
        VkDescriptorPool GetDescriptorPool() const {
            return _descriptorPool;
        }

        void Draw(VkCommandBuffer cmd, VkDescriptorSet descriptorSet, const PushConstants& pc, VkExtent2D extent) const;

    private:
        VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, uint32_t sizeInBytes) const;

        VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
        VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
    };

}  // namespace BECore
