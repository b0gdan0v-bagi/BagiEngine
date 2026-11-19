#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>

namespace BECore {

    /// One shader stage of a 2D pipeline: SPIR-V words (the `k*Spv` arrays of the generated headers) and the
    /// specialization constants of the stage, if it has any. The pointers must stay valid until CreatePipeline2D returns.
    struct VulkanShaderStage {
        const uint32_t* code = nullptr;
        uint32_t sizeInBytes = 0;
        const VkSpecializationInfo* specialization = nullptr;
    };

    /**
     * Makes a graphics pipeline with the state every 2D pipeline of the backend shares: triangle list, dynamic
     * viewport and scissor, filled triangles without culling, one sample, no depth or stencil, one colour attachment
     * with alpha blending (the colour is straight alpha, the alpha is the "over" operator: an offscreen target is
     * sampled with blending by ImGui::Image, so its alpha must be what the draws made, not just the last source alpha).
     * A pipeline gives only what differs: the stages, the vertex input, the layout and the render pass.
     * Returns VK_NULL_HANDLE when a shader module or the pipeline cannot be made; the cause is logged.
     */
    [[nodiscard]] VkPipeline CreatePipeline2D(VkDevice device, VkRenderPass renderPass, VkPipelineLayout layout, const VulkanShaderStage& vertex, const VulkanShaderStage& fragment,
                                              const VkPipelineVertexInputStateCreateInfo& vertexInput);

}  // namespace BECore
