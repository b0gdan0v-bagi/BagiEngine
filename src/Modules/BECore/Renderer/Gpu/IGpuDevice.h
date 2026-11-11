#pragma once

#include <BECore/Renderer/Gpu/IGpuBuffer.h>
#include <BECore/Renderer/Gpu/IGpuDepthTexture.h>
#include <BECore/Renderer/Gpu/IGpuPipeline.h>

namespace BECore {

    class ITexture;

    /**
     * @brief What a backend implements for the one 3D renderer above it (Renderer3D): resources, and commands recorded into passes.
     *
     * The device only carries out descriptions: it has no scene, no material and no camera. Main thread only, and the recording calls
     * (BeginPass() to EndPass()) are made between BeginFrame() and EndFrame() of the owning IRenderer. Textures stay ITexture, made by
     * IRenderer::CreateTexture().
     *
     * **Constants.** A draw reads two blocks, ConstantsSlot::Frame and ConstantsSlot::Draw. Each is at most kMaxConstantsBytes and a multiple
     * of kConstantsAlignment; the pipeline declares the size of each, and SetConstants() takes exactly that many bytes. A backend may keep one
     * slot in push constants and the other in a uniform ring. Constants live until the pass ends or the next SetPipeline() (the layouts of two
     * pipelines may differ), and a draw needs every declared block set.
     *
     * **Conventions.** Front faces are counter-clockwise as seen from the camera; clip space has depth 0 to 1 and Y up (the Vulkan backend flips
     * the viewport, no matrix has a variant per backend); the viewport and the scissor default to the whole target at the start of a pass.
     *
     * **Errors.** A call that breaks the contract (a draw without a pipeline, a block of the wrong size, a pipeline for another depth
     * attachment) is dropped and reported in the backend's log; ValidateConstants() and ValidatePipelineDesc() hold the shared rules.
     */
    class IGpuDevice : public RefCounted {
    public:
        ~IGpuDevice() override = default;

        /// A buffer of @p desc.size bytes holding @p initial (at most that many, the rest zero). Empty when the backend fails or the size is zero.
        [[nodiscard]] virtual IntrusivePtr<IGpuBuffer> CreateBuffer(const BufferDesc& desc, eastl::span<const std::byte> initial) = 0;

        /// A pipeline of @p desc, or why not: a description that ValidatePipelineDesc() refuses, a shader the backend does not have
        [[nodiscard]] virtual std::expected<IntrusivePtr<IGpuPipeline>, eastl::string> CreatePipeline(const PipelineDesc& desc) = 0;

        /// A depth attachment of the given size in pixels (DepthFormat::Depth32Float). Empty for a zero size or when the backend fails.
        [[nodiscard]] virtual IntrusivePtr<IGpuDepthTexture> CreateDepthTexture(uint32_t width, uint32_t height) = 0;

        /// Opens a pass; no pass may be open. Clears or loads the attachments as @p desc says.
        virtual void BeginPass(const PassDesc& desc) = 0;
        virtual void EndPass() = 0;

        // The calls below are valid inside a pass.
        virtual void SetPipeline(IGpuPipeline& pipeline) = 0;
        virtual void SetVertexBuffer(IGpuBuffer& buffer) = 0;
        virtual void SetIndexBuffer(IGpuBuffer& buffer, IndexType type) = 0;
        /// Binds @p texture to @p slot (below kMaxTextureSlots); it stays bound until replaced or the pass ends
        virtual void SetTexture(uint32_t slot, ITexture& texture) = 0;
        virtual void SetConstants(ConstantsSlot slot, eastl::span<const std::byte> bytes) = 0;
        /// In pixels of the target, depth range 0 to 1
        virtual void SetViewport(const Rect& rect) = 0;
        virtual void SetScissor(const Rect& rect) = 0;
        /// Draws triangles of the bound pipeline and buffers. @p firstIndex is in indices (not bytes), @p vertexOffset is added to every index.
        virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) = 0;
    };

}  // namespace BECore
