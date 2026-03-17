#pragma once

#include <vulkan/vulkan.h>

namespace BECore {

    class Color;

    /**
     * Minimal Vulkan graphics pipeline for drawing axis-aligned filled rectangles.
     *
     * Uses push constants exclusively — no vertex buffers. The vertex shader
     * generates a quad (two triangles, 6 vertices) from gl_VertexIndex and the
     * rect/color/screenSize push constants.
     *
     * Push constant layout (40 bytes, matches rect.vert.glsl / rect.frag.glsl):
     *   offset  0 : vec4  rect       — x, y, w, h in pixels
     *   offset 16 : vec4  color      — r, g, b, a in [0, 1]
     *   offset 32 : vec2  screenSize — viewport width, height in pixels
     */
    class VulkanRectPipeline {
    public:
        struct PushConstants {
            float rectX, rectY, rectW, rectH;
            float colorR, colorG, colorB, colorA;
            float screenW, screenH;
        };

        VulkanRectPipeline() = default;
        ~VulkanRectPipeline() = default;

        VulkanRectPipeline(const VulkanRectPipeline&) = delete;
        VulkanRectPipeline& operator=(const VulkanRectPipeline&) = delete;

        bool Initialize(VkDevice device, VkRenderPass renderPass);
        void Destroy(VkDevice device);

        void Draw(VkCommandBuffer cmd, float x, float y, float w, float h,
                  const Color& color, VkExtent2D extent) const;

    private:
        VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, uint32_t sizeInBytes) const;

        VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
        VkPipeline _pipeline = VK_NULL_HANDLE;
    };

}  // namespace BECore
