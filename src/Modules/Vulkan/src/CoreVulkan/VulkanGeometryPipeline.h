#pragma once

#include <vulkan/vulkan.h>

namespace BECore {

    /**
     * Vulkan graphics pipeline for IRenderer::DrawGeometry(): indexed triangles with a color and a UV per vertex.
     *
     * Unlike the rect and texture pipelines it reads a vertex buffer and an index buffer (VulkanGeometryBuffer). A
     * texture is always bound at descriptor set 0, binding 0 (an untextured draw binds a white texel), and its texel is
     * multiplied with the vertex color.
     *
     * Vertex layout (20 bytes, matches geometry.vert.glsl):
     *   offset  0 : vec2   position — output pixels
     *   offset  8 : 4 x u8 color    — R, G, B, A, normalized to [0, 1]
     *   offset 12 : vec2   uv       — [0, 1] over the whole texture
     * Push constants (8 bytes, vertex stage): vec2 screenSize, the viewport size in pixels.
     */
    class VulkanGeometryPipeline {
    public:
        struct Vertex {
            float x = 0.0f;
            float y = 0.0f;
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            uint8_t a = 0;
            float u = 0.0f;
            float v = 0.0f;
        };
        static_assert(sizeof(Vertex) == 20, "geometry.vert.glsl and the vertex input state read this layout");

        struct PushConstants {
            float screenW, screenH;
        };

        VulkanGeometryPipeline() = default;
        ~VulkanGeometryPipeline() = default;

        VulkanGeometryPipeline(const VulkanGeometryPipeline&) = delete;
        VulkanGeometryPipeline& operator=(const VulkanGeometryPipeline&) = delete;

        // @p descriptorSetLayout is the texture pipeline's (set 0, binding 0: a combined image sampler), so the descriptor
        // set of any VulkanTexture binds here
        bool Initialize(VkDevice device, VkRenderPass renderPass, VkDescriptorSetLayout descriptorSetLayout);
        void Destroy(VkDevice device);

        // @p buffer holds the vertices from @p vertexOffset and the uint32 indices from @p indexOffset. @p extent is the
        // framebuffer size, @p scissor the clip in framebuffer pixels (the whole extent when nothing is clipped)
        void Draw(VkCommandBuffer cmd, VkDescriptorSet descriptorSet, VkBuffer buffer, VkDeviceSize vertexOffset, VkDeviceSize indexOffset, uint32_t indexCount, VkExtent2D extent,
                  const VkRect2D& scissor) const;

    private:
        VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, uint32_t sizeInBytes) const;

        VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
    };

}  // namespace BECore
