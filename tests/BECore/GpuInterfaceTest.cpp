#include <Common/RecordingGpuDevice.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kFrameBytes = 112;
        constexpr uint32_t kDrawBytes = 80;
        constexpr uint32_t kVertexStride = 32;
        constexpr uint32_t kTargetSize = 256;
        constexpr uint32_t kCubeIndexCount = 36;
        constexpr float kTolerance = 1e-6f;

        PipelineDesc MakeLitDesc() {
            PipelineDesc desc;
            desc.shader = "Lit3D"_intern;
            desc.vertexLayout.stride = kVertexStride;
            desc.vertexLayout.AddAttribute(0, VertexFormat::Float3, 0);
            desc.vertexLayout.AddAttribute(1, VertexFormat::Float3, 12);
            desc.vertexLayout.AddAttribute(2, VertexFormat::Float2, 24);
            desc.cullMode = CullMode::Back;
            desc.depthTest = true;
            desc.depthWrite = true;
            desc.depthFormat = DepthFormat::Depth32Float;
            desc.frameConstantsSize = kFrameBytes;
            desc.drawConstantsSize = kDrawBytes;
            desc.debugName = "TestLit"_intern;
            return desc;
        }

        eastl::vector<std::byte> MakeBytes(size_t count, uint8_t fill) {
            return eastl::vector<std::byte>(count, std::byte{fill});
        }

        /// A device with a pipeline, a vertex and an index buffer and a depth texture, ready to record a pass
        struct Fixture {
            Fixture() {
                auto made = device.CreatePipeline(MakeLitDesc());
                EXPECT_TRUE(made.has_value());
                pipeline = made.value();
                const eastl::vector<std::byte> vertices(kVertexStride * 4, std::byte{1});
                const eastl::vector<std::byte> indices(kCubeIndexCount * sizeof(uint32_t), std::byte{2});
                vertexBuffer = device.CreateBuffer(BufferDesc{BufferUsage::Vertex, static_cast<uint32_t>(vertices.size()), "vertices"_intern}, {vertices.data(), vertices.size()});
                indexBuffer = device.CreateBuffer(BufferDesc{BufferUsage::Index, static_cast<uint32_t>(indices.size()), "indices"_intern}, {indices.data(), indices.size()});
                depth = device.CreateDepthTexture(kTargetSize, kTargetSize);
            }

            PassDesc MakeDepthPass() const {
                PassDesc pass;
                pass.depth = PassDepth{depth.Get(), PassLoadOp::Clear, 1.0f};
                return pass;
            }

            void BeginBound() {
                device.BeginPass(MakeDepthPass());
                device.SetPipeline(*pipeline);
                device.SetVertexBuffer(*vertexBuffer);
                device.SetIndexBuffer(*indexBuffer, IndexType::Uint32);
            }

            RecordingGpuDevice device;
            IntrusivePtr<IGpuPipeline> pipeline;
            IntrusivePtr<IGpuBuffer> vertexBuffer;
            IntrusivePtr<IGpuBuffer> indexBuffer;
            IntrusivePtr<IGpuDepthTexture> depth;
        };

    }  // namespace

    TEST(GpuInterfaceDescTest, PipelineDescDefaultsAreTheTwoDDefaults) {
        const PipelineDesc desc;

        EXPECT_FALSE(desc.depthTest);
        EXPECT_FALSE(desc.depthWrite);
        EXPECT_EQ(desc.depthFormat, DepthFormat::None);
        EXPECT_EQ(desc.blend, BlendMode::Alpha);
        EXPECT_EQ(desc.cullMode, CullMode::None);
        EXPECT_EQ(desc.frameConstantsSize, 0u);
        EXPECT_EQ(desc.drawConstantsSize, 0u);
    }

    TEST(GpuInterfaceDescTest, PassDescDefaultsToTheBackBufferWithoutDepth) {
        const PassDesc pass;

        EXPECT_EQ(pass.target, nullptr);
        EXPECT_FALSE(pass.depth.has_value());
        EXPECT_EQ(pass.colorLoadOp, PassLoadOp::Clear);
    }

    TEST(GpuInterfaceDescTest, VertexLayoutTakesAtMostTheMaxAttributes) {
        VertexLayout layout;
        for (size_t i = 0; i < kMaxVertexAttributes; ++i) {
            EXPECT_TRUE(layout.AddAttribute(static_cast<uint32_t>(i), VertexFormat::Float4, static_cast<uint32_t>(i) * 16));
        }
        EXPECT_FALSE(layout.AddAttribute(kMaxVertexAttributes, VertexFormat::Float4, 0));
        EXPECT_EQ(layout.attributeCount, kMaxVertexAttributes);
    }

    TEST(GpuInterfaceDescTest, FormatSizes) {
        EXPECT_EQ(VertexFormatSize(VertexFormat::Float2), 8u);
        EXPECT_EQ(VertexFormatSize(VertexFormat::Float3), 12u);
        EXPECT_EQ(VertexFormatSize(VertexFormat::Float4), 16u);
        EXPECT_EQ(VertexFormatSize(VertexFormat::Unorm8x4), 4u);
        EXPECT_EQ(IndexTypeSize(IndexType::Uint16), 2u);
        EXPECT_EQ(IndexTypeSize(IndexType::Uint32), 4u);
    }

    TEST(GpuInterfacePipelineTest, CreatesAPipelineFromADescription) {
        RecordingGpuDevice device;

        auto made = device.CreatePipeline(MakeLitDesc());

        ASSERT_TRUE(made.has_value());
        EXPECT_EQ((*made)->GetDesc().shader, "Lit3D"_intern);
        EXPECT_EQ((*made)->GetDesc().frameConstantsSize, kFrameBytes);
        EXPECT_EQ(device.pipelinesCreated, 1u);
    }

    TEST(GpuInterfacePipelineTest, ConstantsSizeThatIsNotAMultipleOfSixteenIsRefused) {
        RecordingGpuDevice device;
        PipelineDesc desc = MakeLitDesc();
        desc.drawConstantsSize = kDrawBytes - 4;

        auto made = device.CreatePipeline(desc);

        ASSERT_FALSE(made.has_value());
        EXPECT_NE(made.error().find("multiple"), eastl::string::npos);
        EXPECT_EQ(device.pipelinesCreated, 0u);
    }

    TEST(GpuInterfacePipelineTest, ConstantsSizeOverTheLimitIsRefused) {
        RecordingGpuDevice device;
        PipelineDesc desc = MakeLitDesc();
        desc.frameConstantsSize = kMaxConstantsBytes + kConstantsAlignment;

        EXPECT_FALSE(device.CreatePipeline(desc).has_value());

        desc.frameConstantsSize = kMaxConstantsBytes;
        EXPECT_TRUE(device.CreatePipeline(desc).has_value());
    }

    TEST(GpuInterfacePipelineTest, DepthStateNeedsADepthFormat) {
        RecordingGpuDevice device;
        PipelineDesc desc = MakeLitDesc();
        desc.depthFormat = DepthFormat::None;

        EXPECT_FALSE(device.CreatePipeline(desc).has_value());

        desc.depthTest = false;
        desc.depthWrite = false;
        EXPECT_TRUE(device.CreatePipeline(desc).has_value());
    }

    TEST(GpuInterfacePipelineTest, VertexLayoutMustFitItsStrideWithDistinctLocations) {
        RecordingGpuDevice device;
        PipelineDesc tooWide = MakeLitDesc();
        tooWide.vertexLayout.stride = 16;
        PipelineDesc twice = MakeLitDesc();
        twice.vertexLayout.attributes[1].location = 0;

        EXPECT_FALSE(device.CreatePipeline(tooWide).has_value());
        EXPECT_FALSE(device.CreatePipeline(twice).has_value());
    }

    TEST(GpuInterfacePipelineTest, AShaderIsRequired) {
        RecordingGpuDevice device;
        PipelineDesc desc = MakeLitDesc();
        desc.shader = PoolString{};

        EXPECT_FALSE(device.CreatePipeline(desc).has_value());
    }

    TEST(GpuInterfaceBufferTest, KeepsTheInitialBytesAndZeroFillsTheRest) {
        RecordingGpuDevice device;
        const eastl::vector<std::byte> initial = MakeBytes(4, 7);

        IntrusivePtr<IGpuBuffer> buffer = device.CreateBuffer(BufferDesc{BufferUsage::Vertex, 8, "b"_intern}, {initial.data(), initial.size()});

        ASSERT_TRUE(buffer);
        const auto& bytes = static_cast<RecordedGpuBuffer&>(*buffer).GetBytes();
        ASSERT_EQ(bytes.size(), 8u);
        EXPECT_EQ(bytes[3], std::byte{7});
        EXPECT_EQ(bytes[4], std::byte{0});
        EXPECT_EQ(device.buffersCreated, 1u);
    }

    TEST(GpuInterfaceBufferTest, InitialDataLargerThanTheBufferIsRefused) {
        RecordingGpuDevice device;
        const eastl::vector<std::byte> initial = MakeBytes(16, 1);

        EXPECT_FALSE(device.CreateBuffer(BufferDesc{BufferUsage::Index, 8, "b"_intern}, {initial.data(), initial.size()}));
        EXPECT_FALSE(device.errors.empty());
    }

    TEST(GpuInterfaceDepthTextureTest, HasTheSizeAndFormatItWasMadeWith) {
        RecordingGpuDevice device;

        IntrusivePtr<IGpuDepthTexture> depth = device.CreateDepthTexture(640, 480);

        ASSERT_TRUE(depth);
        EXPECT_EQ(depth->GetWidth(), 640u);
        EXPECT_EQ(depth->GetHeight(), 480u);
        EXPECT_EQ(depth->GetFormat(), DepthFormat::Depth32Float);
        EXPECT_FALSE(device.CreateDepthTexture(0, 480));
    }

    TEST(GpuInterfacePassTest, APassWithDepthIsRecordedWithItsDescription) {
        Fixture f;
        PassDesc pass = f.MakeDepthPass();
        pass.colorLoadOp = PassLoadOp::Load;

        f.device.BeginPass(pass);
        EXPECT_TRUE(f.device.IsPassOpen());
        f.device.EndPass();

        ASSERT_EQ(f.device.commands.size(), 2u);
        const GpuCommand& begin = f.device.commands[0];
        EXPECT_EQ(begin.kind, GpuCommandKind::BeginPass);
        EXPECT_EQ(begin.pass.colorLoadOp, PassLoadOp::Load);
        ASSERT_TRUE(begin.pass.depth.has_value());
        EXPECT_EQ(begin.pass.depth->texture, f.depth.Get());
        EXPECT_EQ(begin.pass.depth->loadOp, PassLoadOp::Clear);
        EXPECT_NEAR(begin.pass.depth->clearValue, 1.0f, kTolerance);
        EXPECT_EQ(f.device.commands[1].kind, GpuCommandKind::EndPass);
        EXPECT_FALSE(f.device.IsPassOpen());
        EXPECT_TRUE(f.device.errors.empty());
    }

    TEST(GpuInterfacePassTest, ADepthClearValueOutsideZeroToOneIsRefused) {
        Fixture f;
        PassDesc pass = f.MakeDepthPass();
        pass.depth->clearValue = 2.0f;

        f.device.BeginPass(pass);

        EXPECT_FALSE(f.device.IsPassOpen());
        EXPECT_EQ(f.device.errors.size(), 1u);
    }

    TEST(GpuInterfacePassTest, APipelineForAnotherDepthAttachmentCannotBeBound) {
        Fixture f;

        f.device.BeginPass(PassDesc{});
        f.device.SetPipeline(*f.pipeline);

        EXPECT_EQ(f.device.errors.size(), 1u);
        EXPECT_EQ(f.device.CountCommands(GpuCommandKind::SetPipeline), 0u);
    }

    TEST(GpuInterfacePassTest, CallsOutsideAPassAreRefused) {
        Fixture f;

        f.device.SetPipeline(*f.pipeline);
        f.device.EndPass();
        f.device.DrawIndexed(kCubeIndexCount, 0, 0);

        EXPECT_EQ(f.device.errors.size(), 3u);
        EXPECT_TRUE(f.device.commands.empty());
    }

    TEST(GpuInterfaceDrawTest, ABoundPipelineAndConstantsMakeADraw) {
        Fixture f;
        f.BeginBound();
        const eastl::vector<std::byte> frame = MakeBytes(kFrameBytes, 3);
        const eastl::vector<std::byte> draw = MakeBytes(kDrawBytes, 4);

        f.device.SetConstants(ConstantsSlot::Frame, {frame.data(), frame.size()});
        f.device.SetConstants(ConstantsSlot::Draw, {draw.data(), draw.size()});
        f.device.DrawIndexed(kCubeIndexCount, 0, 0);
        f.device.EndPass();

        EXPECT_TRUE(f.device.errors.empty());
        ASSERT_EQ(f.device.draws.size(), 1u);
        const RecordedDraw& recorded = f.device.draws[0];
        EXPECT_EQ(recorded.pipeline, f.pipeline.Get());
        EXPECT_EQ(recorded.vertexBuffer, f.vertexBuffer.Get());
        EXPECT_EQ(recorded.indexBuffer, f.indexBuffer.Get());
        EXPECT_EQ(recorded.indexCount, kCubeIndexCount);
        EXPECT_EQ(recorded.constants[static_cast<size_t>(ConstantsSlot::Frame)], frame);
        EXPECT_EQ(recorded.constants[static_cast<size_t>(ConstantsSlot::Draw)], draw);
    }

    TEST(GpuInterfaceDrawTest, TwoDrawsShareTheFrameConstantsAndOwnTheirDrawConstants) {
        Fixture f;
        f.BeginBound();
        const eastl::vector<std::byte> frame = MakeBytes(kFrameBytes, 3);
        const eastl::vector<std::byte> first = MakeBytes(kDrawBytes, 4);
        const eastl::vector<std::byte> second = MakeBytes(kDrawBytes, 5);

        f.device.SetConstants(ConstantsSlot::Frame, {frame.data(), frame.size()});
        f.device.SetConstants(ConstantsSlot::Draw, {first.data(), first.size()});
        f.device.DrawIndexed(kCubeIndexCount, 0, 0);
        f.device.SetConstants(ConstantsSlot::Draw, {second.data(), second.size()});
        f.device.DrawIndexed(kCubeIndexCount, 0, 0);
        f.device.EndPass();

        EXPECT_TRUE(f.device.errors.empty());
        ASSERT_EQ(f.device.draws.size(), 2u);
        EXPECT_EQ(f.device.draws[0].constants[static_cast<size_t>(ConstantsSlot::Draw)], first);
        EXPECT_EQ(f.device.draws[1].constants[static_cast<size_t>(ConstantsSlot::Draw)], second);
        EXPECT_EQ(f.device.draws[0].constants[static_cast<size_t>(ConstantsSlot::Frame)], f.device.draws[1].constants[static_cast<size_t>(ConstantsSlot::Frame)]);
        EXPECT_EQ(f.device.CountCommands(GpuCommandKind::SetConstants), 3u);
    }

    TEST(GpuInterfaceDrawTest, ABlockOverTheLimitIsRefusedAndNotRecorded) {
        Fixture f;
        f.BeginBound();
        const eastl::vector<std::byte> tooBig = MakeBytes(kMaxConstantsBytes + kConstantsAlignment, 1);

        f.device.SetConstants(ConstantsSlot::Draw, {tooBig.data(), tooBig.size()});

        ASSERT_EQ(f.device.errors.size(), 1u);
        EXPECT_NE(f.device.errors[0].find("over the limit"), eastl::string::npos);
        EXPECT_EQ(f.device.CountCommands(GpuCommandKind::SetConstants), 0u);
    }

    TEST(GpuInterfaceDrawTest, ABlockOfADifferentSizeThanDeclaredIsRefused) {
        Fixture f;
        f.BeginBound();
        const eastl::vector<std::byte> wrong = MakeBytes(kDrawBytes, 1);

        f.device.SetConstants(ConstantsSlot::Frame, {wrong.data(), wrong.size()});

        EXPECT_EQ(f.device.errors.size(), 1u);
        EXPECT_EQ(f.device.CountCommands(GpuCommandKind::SetConstants), 0u);
    }

    TEST(GpuInterfaceDrawTest, ADrawWithoutAPipelineIsRefused) {
        Fixture f;
        f.device.BeginPass(f.MakeDepthPass());
        f.device.SetVertexBuffer(*f.vertexBuffer);
        f.device.SetIndexBuffer(*f.indexBuffer, IndexType::Uint32);

        f.device.DrawIndexed(kCubeIndexCount, 0, 0);

        ASSERT_EQ(f.device.errors.size(), 1u);
        EXPECT_NE(f.device.errors[0].find("without a pipeline"), eastl::string::npos);
        EXPECT_TRUE(f.device.draws.empty());
    }

    TEST(GpuInterfaceDrawTest, ADrawNeedsEveryDeclaredConstantsBlock) {
        Fixture f;
        f.BeginBound();
        const eastl::vector<std::byte> frame = MakeBytes(kFrameBytes, 3);
        f.device.SetConstants(ConstantsSlot::Frame, {frame.data(), frame.size()});

        f.device.DrawIndexed(kCubeIndexCount, 0, 0);

        EXPECT_EQ(f.device.errors.size(), 1u);
        EXPECT_TRUE(f.device.draws.empty());
    }

    TEST(GpuInterfaceDrawTest, SettingAPipelineAgainInvalidatesTheConstants) {
        Fixture f;
        f.BeginBound();
        const eastl::vector<std::byte> frame = MakeBytes(kFrameBytes, 3);
        const eastl::vector<std::byte> draw = MakeBytes(kDrawBytes, 4);
        f.device.SetConstants(ConstantsSlot::Frame, {frame.data(), frame.size()});
        f.device.SetConstants(ConstantsSlot::Draw, {draw.data(), draw.size()});

        f.device.SetPipeline(*f.pipeline);
        f.device.DrawIndexed(kCubeIndexCount, 0, 0);

        EXPECT_EQ(f.device.errors.size(), 1u);
        EXPECT_TRUE(f.device.draws.empty());
    }

    TEST(GpuInterfaceDrawTest, BuffersOfTheWrongUsageAreRefused) {
        Fixture f;
        f.device.BeginPass(f.MakeDepthPass());

        f.device.SetVertexBuffer(*f.indexBuffer);
        f.device.SetIndexBuffer(*f.vertexBuffer, IndexType::Uint16);

        EXPECT_EQ(f.device.errors.size(), 2u);
    }

    TEST(GpuInterfaceDrawTest, TexturesViewportAndScissorAreKeptWithTheDraw) {
        Fixture f;
        f.BeginBound();
        const eastl::vector<uint8_t> pixels(4, 255);
        const IntrusivePtr<RecordedTexture> texture = New<RecordedTexture>(pixels, 1, 1);
        const eastl::vector<std::byte> frame = MakeBytes(kFrameBytes, 3);
        const eastl::vector<std::byte> draw = MakeBytes(kDrawBytes, 4);
        f.device.SetConstants(ConstantsSlot::Frame, {frame.data(), frame.size()});
        f.device.SetConstants(ConstantsSlot::Draw, {draw.data(), draw.size()});

        f.device.SetTexture(0, *texture);
        f.device.SetViewport(Rect{0.0f, 0.0f, 100.0f, 50.0f});
        f.device.SetScissor(Rect{10.0f, 10.0f, 20.0f, 20.0f});
        f.device.DrawIndexed(6, 3, -2);
        f.device.SetTexture(kMaxTextureSlots, *texture);

        ASSERT_EQ(f.device.draws.size(), 1u);
        const RecordedDraw& recorded = f.device.draws[0];
        EXPECT_EQ(recorded.textures[0], texture.Get());
        EXPECT_NEAR(recorded.viewport.w, 100.0f, kTolerance);
        EXPECT_NEAR(recorded.scissor.x, 10.0f, kTolerance);
        EXPECT_EQ(recorded.firstIndex, 3u);
        EXPECT_EQ(recorded.vertexOffset, -2);
        EXPECT_EQ(f.device.errors.size(), 1u);
    }

}  // namespace BECore::Tests
