#pragma once

#include <BECore/Renderer/Gpu/IGpuDevice.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Renderer/Render3D/Renderer3DConstants.h>
#include <BECore/Renderer/Render3D/Scene3D.h>

namespace BECore {

    class IRenderer;

    /**
     * @brief The 3D renderer, written once for any IGpuDevice: turns a Scene3D into one depth-tested pass.
     *
     * Needs a renderer that has a device (RequireGpuDevice(); the SDL 2D renderer has none, so Render() says why and records nothing). Keeps what it
     * made for the last device it drew on (the lit pipeline, a depth texture of the target's size, a white 1 x 1 texture), and makes it again for
     * another device. Main thread, between BeginFrame() and EndFrame() of the renderer; call Reset() before the renderer is destroyed.
     *
     * The pass is the 3D pass only and comes before the 2D scene and the UI: the caller opens those later with PassLoadOp::Load.
     */
    class Renderer3D {
    public:
        /**
         * @brief Draws @p scene on @p renderer's device.
         *
         * @p base says where (its `target`, null is the back buffer) and what the color attachment does on opening (`colorLoadOp`, `clearColor`);
         * its `depth` is ignored: the pass gets a depth attachment of the size of the target (the output size for the back buffer), cleared to 1.
         * An instance without a mesh is skipped; one without a texture is drawn with a white one, so its tint is its color.
         * @return why nothing was drawn (no device, no size, a pipeline or a resource that could not be made)
         */
        [[nodiscard]] std::expected<void, eastl::string> Render(IRenderer& renderer, const Scene3D& scene, const PassDesc& base);

        /// Lets go of the device and what was made on it
        void Reset();

        /// The matrix of the frame constants: `projection * view` for a target of @p aspect
        [[nodiscard]] static Mat4 ViewProjection(const Camera3D& camera, float aspect);

        /// The frame constants of @p scene for a target of @p aspect (width over height)
        [[nodiscard]] static FrameConstants MakeFrameConstants(const Scene3D& scene, float aspect);

        /// The draw constants of one instance
        [[nodiscard]] static DrawConstants MakeDrawConstants(const MeshInstance& instance);

        /// The description of the lit pipeline: the shader `Lit3D`, depth test Less with depth write, back faces culled, alpha blend, the Vertex3D layout
        [[nodiscard]] static PipelineDesc MakeLitPipelineDesc();

    private:
        std::expected<void, eastl::string> Prepare(IRenderer& renderer, IGpuDevice& device, uint32_t width, uint32_t height);

        IntrusivePtr<IGpuDevice> _device;
        IntrusivePtr<IGpuPipeline> _pipeline;
        IntrusivePtr<IGpuDepthTexture> _depth;
        IntrusivePtr<ITexture> _white;
    };

}  // namespace BECore
