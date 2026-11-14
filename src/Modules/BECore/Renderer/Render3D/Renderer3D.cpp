#include <BECore/Renderer/IRenderTarget.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/Render3D/Renderer3D.h>

namespace BECore {

    namespace {

        constexpr uint32_t kLitTextureSlot = 0;
        constexpr uint8_t kWhiteByte = 255;
        constexpr uint32_t kWhitePixelBytes = 4;

        template <typename T>
        eastl::span<const std::byte> BytesOf(const T& value) {
            return {reinterpret_cast<const std::byte*>(&value), sizeof(T)};
        }

    }  // namespace

    PipelineDesc Renderer3D::MakeLitPipelineDesc() {
        PipelineDesc desc;
        desc.shader = "Lit3D"_intern;
        desc.vertexLayout = MakeVertex3DLayout();
        desc.cullMode = CullMode::Back;
        desc.depthTest = true;
        desc.depthWrite = true;
        desc.depthCompare = DepthCompare::Less;
        desc.blend = BlendMode::Alpha;
        desc.depthFormat = DepthFormat::Depth32Float;
        desc.frameConstantsSize = sizeof(FrameConstants);
        desc.drawConstantsSize = sizeof(DrawConstants);
        desc.debugName = "Lit3D"_intern;
        return desc;
    }

    Mat4 Renderer3D::ViewProjection(const Camera3D& camera, float aspect) {
        return camera.ProjectionMatrix(aspect) * camera.ViewMatrix();
    }

    FrameConstants Renderer3D::MakeFrameConstants(const Scene3D& scene, float aspect) {
        FrameConstants constants;
        constants.viewProjection = ViewProjection(scene.camera, aspect);
        constants.lightDirection = Vec4(-scene.light.direction.Normalized(), 0.0f);
        const Vec4 light = ColorToLinear(scene.light.color);
        constants.lightColor = Vec4(light.XYZ() * scene.light.intensity, 1.0f);
        const Vec4 ambient = ColorToLinear(scene.ambient.color);
        constants.ambientColor = Vec4(ambient.XYZ() * scene.ambient.intensity, 1.0f);
        return constants;
    }

    DrawConstants Renderer3D::MakeDrawConstants(const MeshInstance& instance) {
        return DrawConstants{instance.model, ColorToLinear(instance.material.tint)};
    }

    void Renderer3D::Reset() {
        _white = {};
        _depth = {};
        _pipeline = {};
        _device = {};
    }

    std::expected<void, String> Renderer3D::Prepare(IRenderer& renderer, IGpuDevice& device, uint32_t width, uint32_t height) {
        if (_device.Get() != &device) {
            Reset();
            _device = IntrusivePtr<IGpuDevice>(&device);
        }
        if (!_pipeline) {
            std::expected<IntrusivePtr<IGpuPipeline>, String> pipeline = device.CreatePipeline(MakeLitPipelineDesc());
            if (!pipeline) {
                return std::unexpected(Format("Renderer3D: the lit pipeline was refused: {}", pipeline.error()));
            }
            _pipeline = std::move(*pipeline);
        }
        if (!_depth || _depth->GetWidth() != width || _depth->GetHeight() != height) {
            _depth = device.CreateDepthTexture(width, height);
            if (!_depth) {
                return std::unexpected(Format("Renderer3D: no depth texture of {} x {}", width, height));
            }
        }
        if (!_white) {
            const std::array<uint8_t, kWhitePixelBytes> pixel = {kWhiteByte, kWhiteByte, kWhiteByte, kWhiteByte};
            _white = renderer.CreateTexture(pixel.data(), 1, 1);
            if (!_white) {
                return std::unexpected(String("Renderer3D: no white texture"));
            }
        }
        return {};
    }

    std::expected<void, String> Renderer3D::Render(IRenderer& renderer, const Scene3D& scene, const PassDesc& base) {
        std::expected<IGpuDevice*, String> device = RequireGpuDevice(renderer);
        if (!device) {
            return std::unexpected(device.error());
        }

        const float width = base.target ? static_cast<float>(base.target->GetWidth()) : renderer.GetOutputWidth();
        const float height = base.target ? static_cast<float>(base.target->GetHeight()) : renderer.GetOutputHeight();
        if (width < 1.0f || height < 1.0f) {
            return std::unexpected(String("Renderer3D: the target has no size"));
        }
        if (std::expected<void, String> prepared = Prepare(renderer, **device, static_cast<uint32_t>(width), static_cast<uint32_t>(height)); !prepared) {
            return prepared;
        }

        IGpuDevice& gpu = **device;
        PassDesc pass = base;
        pass.depth = PassDepth{_depth.Get(), PassLoadOp::Clear, 1.0f};
        gpu.BeginPass(pass);
        gpu.SetPipeline(*_pipeline);
        const FrameConstants frame = MakeFrameConstants(scene, width / height);
        gpu.SetConstants(ConstantsSlot::Frame, BytesOf(frame));
        for (const MeshInstance& instance : scene.instances) {
            if (instance.mesh == nullptr) {
                continue;
            }
            gpu.SetVertexBuffer(instance.mesh->GetVertexBuffer());
            gpu.SetIndexBuffer(instance.mesh->GetIndexBuffer(), Mesh::GetIndexType());
            gpu.SetTexture(kLitTextureSlot, instance.material.texture ? *instance.material.texture : *_white);
            const DrawConstants draw = MakeDrawConstants(instance);
            gpu.SetConstants(ConstantsSlot::Draw, BytesOf(draw));
            gpu.DrawIndexed(instance.mesh->IndexCount(), 0, 0);
        }
        gpu.EndPass();
        return {};
    }

}  // namespace BECore
