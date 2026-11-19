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
     * Push constant layout (56 bytes, matches texture.vert.glsl / texture.frag.glsl):
     *   offset  0 : vec4  dstRect    — x, y, w, h in pixels
     *   offset 16 : vec4  srcUV      — u0, v0, u1, v1 in [0, 1]
     *   offset 32 : vec4  tint       — r, g, b, a in [0, 1], multiplied with the sampled texel
     *   offset 48 : vec2  screenSize — viewport width, height in pixels
     */
    class VulkanTexturePipeline {
    public:
        struct PushConstants {
            float dstX, dstY, dstW, dstH;
            float u0, v0, u1, v1;
            float tintR, tintG, tintB, tintA;
            float screenW, screenH;
        };

        VulkanTexturePipeline() = default;
        ~VulkanTexturePipeline() = default;

        VulkanTexturePipeline(const VulkanTexturePipeline&) = delete;
        VulkanTexturePipeline& operator=(const VulkanTexturePipeline&) = delete;

        /// A set and the pool it came from: a pool cannot be found from the set, and the set is freed to its own pool.
        struct DescriptorAllocation {
            VkDescriptorSet set = VK_NULL_HANDLE;
            VkDescriptorPool pool = VK_NULL_HANDLE;
        };

        /// How many sets one pool holds; a full pool is followed by another one (AllocateDescriptorSet).
        static constexpr uint32_t kSetsPerPool = 512;

        bool Initialize(VkDevice device, VkRenderPass renderPass, bool linearizeColors);

        /// Destroys the pools and the sampler: every set and every texture that samples with the shared sampler must be gone first.
        void Destroy(VkDevice device);

        /// Allocates and writes a set that samples @p imageView with the shared sampler. The pools have
        /// VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT so the owner frees the set alone (VulkanTexture destructor).
        /// A full pool makes a new one; an empty result (set is null) means the allocation failed and is logged.
        DescriptorAllocation AllocateDescriptorSet(VkDevice device, VkImageView imageView);

        VkDescriptorSetLayout GetDescriptorSetLayout() const {
            return _descriptorSetLayout;
        }

        // @p scissor is the clip in framebuffer pixels (the whole extent when nothing is clipped)
        void Draw(VkCommandBuffer cmd, VkDescriptorSet descriptorSet, const PushConstants& pc, const VkRect2D& scissor) const;

    private:
        VkDescriptorPool CreateDescriptorPool(VkDevice device);

        VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
        // The last one is the pool that is allocated from; earlier ones are full but still hold the sets they gave out
        Vector<VkDescriptorPool> _descriptorPools;
        // Linear, clamp to edge: the only mode of the textures, so one sampler serves every set
        VkSampler _sampler = VK_NULL_HANDLE;
        VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
    };

}  // namespace BECore
