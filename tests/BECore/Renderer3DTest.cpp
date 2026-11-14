#include <BECore/Renderer/Render3D/Camera3D.h>
#include <BECore/Renderer/Render3D/Mesh.h>
#include <BECore/Renderer/Render3D/Renderer3D.h>
#include <BECore/Renderer/Render3D/Scene3D.h>
#include <Common/RecordingGpuDevice.h>
#include <Common/RecordingRenderer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-5f;
        constexpr size_t kCubeVertexCount = 24;
        constexpr size_t kCubeIndexCount = 36;
        constexpr size_t kTriangleIndexCount = 3;
        constexpr float kAspect = 4.0f / 3.0f;

        float Length(const Vec3& v) {
            return v.Length();
        }

        /// Inside the clip volume of Vulkan, Metal and D3D: x and y in -w..w, z in 0..w, and in front of the camera (w > 0)
        bool IsInsideClip(const Vec4& clip) {
            return clip.w > 0.0f && std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w && clip.z >= 0.0f && clip.z <= clip.w;
        }

    }  // namespace

    TEST(Renderer3DCubeTest, HasOneQuadPerFace) {
        const MeshData cube = MakeUnitCube();
        EXPECT_EQ(cube.vertices.size(), kCubeVertexCount);
        EXPECT_EQ(cube.indices.size(), kCubeIndexCount);
    }

    TEST(Renderer3DCubeTest, EveryIndexIsInRange) {
        const MeshData cube = MakeUnitCube();
        for (const uint32_t index : cube.indices) {
            EXPECT_LT(index, cube.vertices.size());
        }
    }

    TEST(Renderer3DCubeTest, VerticesAreTheCornersOfTheUnitCube) {
        for (const Vertex3D& vertex : MakeUnitCube().vertices) {
            EXPECT_FLOAT_EQ(std::abs(vertex.position.x), 0.5f);
            EXPECT_FLOAT_EQ(std::abs(vertex.position.y), 0.5f);
            EXPECT_FLOAT_EQ(std::abs(vertex.position.z), 0.5f);
        }
    }

    TEST(Renderer3DCubeTest, NormalsAreUnitAndPointAwayFromTheCenter) {
        for (const Vertex3D& vertex : MakeUnitCube().vertices) {
            EXPECT_NEAR(Length(vertex.normal), 1.0f, kTolerance);
            // The vertex lies on the face its normal names: the center is behind that face
            EXPECT_FLOAT_EQ(Dot(vertex.normal, vertex.position), 0.5f);
        }
    }

    TEST(Renderer3DCubeTest, WindingAgreesWithTheNormals) {
        const MeshData cube = MakeUnitCube();
        for (size_t i = 0; i < cube.indices.size(); i += kTriangleIndexCount) {
            const Vertex3D& a = cube.vertices[cube.indices[i]];
            const Vertex3D& b = cube.vertices[cube.indices[i + 1]];
            const Vertex3D& c = cube.vertices[cube.indices[i + 2]];
            const Vec3 faceNormal = Cross(b.position - a.position, c.position - a.position);
            EXPECT_GT(faceNormal.Length(), 0.0f) << "a degenerate triangle at index " << i;
            // Counter-clockwise from outside: the cross product points the way the normal does, so it is parallel and not opposed
            EXPECT_NEAR(Dot(faceNormal.Normalized(), a.normal), 1.0f, kTolerance) << "triangle at index " << i;
            EXPECT_EQ(a.normal, b.normal);
            EXPECT_EQ(a.normal, c.normal);
        }
    }

    TEST(Renderer3DCubeTest, EachFaceHasOneUvSquare) {
        const MeshData cube = MakeUnitCube();
        for (size_t face = 0; face < kCubeVertexCount; face += 4) {
            float minU = 1.0f;
            float maxU = 0.0f;
            float minV = 1.0f;
            float maxV = 0.0f;
            for (size_t corner = 0; corner < 4; ++corner) {
                const Vec2 uv = cube.vertices[face + corner].uv;
                minU = std::min(minU, uv.x);
                maxU = std::max(maxU, uv.x);
                minV = std::min(minV, uv.y);
                maxV = std::max(maxV, uv.y);
            }
            EXPECT_FLOAT_EQ(minU, 0.0f);
            EXPECT_FLOAT_EQ(maxU, 1.0f);
            EXPECT_FLOAT_EQ(minV, 0.0f);
            EXPECT_FLOAT_EQ(maxV, 1.0f);
        }
    }

    TEST(Renderer3DVertexTest, LayoutMatchesTheStruct) {
        const VertexLayout layout = MakeVertex3DLayout();
        EXPECT_EQ(layout.stride, 32u);
        ASSERT_EQ(layout.attributeCount, 3u);
        EXPECT_EQ(layout.attributes[0].location, kVertex3DPositionLocation);
        EXPECT_EQ(layout.attributes[0].format, VertexFormat::Float3);
        EXPECT_EQ(layout.attributes[0].offset, 0u);
        EXPECT_EQ(layout.attributes[1].location, kVertex3DNormalLocation);
        EXPECT_EQ(layout.attributes[1].format, VertexFormat::Float3);
        EXPECT_EQ(layout.attributes[1].offset, 12u);
        EXPECT_EQ(layout.attributes[2].location, kVertex3DUvLocation);
        EXPECT_EQ(layout.attributes[2].format, VertexFormat::Float2);
        EXPECT_EQ(layout.attributes[2].offset, 24u);
        PipelineDesc desc;
        desc.shader = "Test"_intern;
        desc.vertexLayout = layout;
        EXPECT_TRUE(ValidatePipelineDesc(desc).has_value());
    }

    TEST(Renderer3DMeshTest, UploadMakesBuffersOfTheRightSize) {
        RecordingGpuDevice device;
        const MeshData cube = MakeUnitCube();
        const IntrusivePtr<Mesh> mesh = Mesh::Create(device, cube, "cube"_intern);
        ASSERT_TRUE(mesh);
        EXPECT_EQ(mesh->IndexCount(), kCubeIndexCount);
        EXPECT_EQ(device.buffersCreated, 2u);

        const auto& vertexBuffer = static_cast<const RecordedGpuBuffer&>(mesh->GetVertexBuffer());
        const auto& indexBuffer = static_cast<const RecordedGpuBuffer&>(mesh->GetIndexBuffer());
        EXPECT_EQ(vertexBuffer.GetDesc().usage, BufferUsage::Vertex);
        EXPECT_EQ(indexBuffer.GetDesc().usage, BufferUsage::Index);
        EXPECT_EQ(vertexBuffer.GetBytes().size(), cube.vertices.size() * sizeof(Vertex3D));
        EXPECT_EQ(indexBuffer.GetBytes().size(), cube.indices.size() * 4);
        EXPECT_EQ(std::memcmp(vertexBuffer.GetBytes().data(), cube.vertices.data(), vertexBuffer.GetBytes().size()), 0);
        EXPECT_EQ(std::memcmp(indexBuffer.GetBytes().data(), cube.indices.data(), indexBuffer.GetBytes().size()), 0);
        EXPECT_TRUE(device.errors.empty());
    }

    TEST(Renderer3DMeshTest, NoTrianglesGivesNoMesh) {
        RecordingGpuDevice device;
        EXPECT_FALSE(Mesh::Create(device, MeshData{}));
        EXPECT_EQ(device.buffersCreated, 0u);
    }

    TEST(Renderer3DCameraTest, ProjectsWhatIsInFrontInsideTheClipVolume) {
        const Camera3D camera;  // at (0, 0, 5) looking at the origin
        const Mat4 viewProjection = camera.ProjectionMatrix(kAspect) * camera.ViewMatrix();
        EXPECT_TRUE(IsInsideClip(viewProjection * Vec4(0.0f, 0.0f, 0.0f, 1.0f)));
        for (const Vertex3D& vertex : MakeUnitCube().vertices) {
            EXPECT_TRUE(IsInsideClip(viewProjection * Vec4(vertex.position, 1.0f)));
        }
    }

    TEST(Renderer3DCameraTest, DoesNotProjectWhatIsBehindInsideTheClipVolume) {
        const Camera3D camera;
        const Mat4 viewProjection = camera.ProjectionMatrix(kAspect) * camera.ViewMatrix();
        EXPECT_FALSE(IsInsideClip(viewProjection * Vec4(0.0f, 0.0f, 10.0f, 1.0f)));
        const Vec4 pastFar = viewProjection * Vec4(0.0f, 0.0f, -(camera.farPlane + 1.0f), 1.0f);
        EXPECT_FALSE(IsInsideClip(pastFar));
    }

    TEST(Renderer3DCameraTest, UpIsUpOnTheScreen) {
        const Camera3D camera;
        const Mat4 viewProjection = camera.ProjectionMatrix(kAspect) * camera.ViewMatrix();
        EXPECT_GT((viewProjection * Vec4(0.0f, 1.0f, 0.0f, 1.0f)).y, 0.0f);
        EXPECT_GT((viewProjection * Vec4(1.0f, 0.0f, 0.0f, 1.0f)).x, 0.0f);
    }

    namespace {

        /// A renderer with a recording device, and a scene of one lit cube lit from a known side
        struct Renderer3DFixture {
            Renderer3DFixture() : device(New<RecordingGpuDevice>()) {
                renderer.SetGpuDevice(device);
                cube = Mesh::Create(*device, MakeUnitCube(), "cube"_intern);
                scene.light.direction = Vec3{-1.0f, -2.0f, -2.0f};
                scene.light.intensity = 2.0f;
                scene.ambient.intensity = 0.5f;
                scene.instances.push_back(MeshInstance{cube.Get(), Material{}, Mat4::Identity()});
            }

            RecordingRenderer renderer;
            IntrusivePtr<RecordingGpuDevice> device;
            IntrusivePtr<Mesh> cube;
            Scene3D scene;
            Renderer3D renderer3D;
        };

        Array<float, Mat4::kElementCount> ElementsOf(const Mat4& matrix) {
            Array<float, Mat4::kElementCount> elements{};
            std::copy(matrix.Data(), matrix.Data() + Mat4::kElementCount, elements.begin());
            return elements;
        }

        void ExpectNear(const Mat4& actual, const Mat4& expected) {
            const auto a = ElementsOf(actual);
            const auto e = ElementsOf(expected);
            for (size_t i = 0; i < Mat4::kElementCount; ++i) {
                EXPECT_NEAR(a[i], e[i], kTolerance) << "element " << i;
            }
        }

        void ExpectNear(const Vec4& actual, const Vec4& expected) {
            EXPECT_NEAR(actual.x, expected.x, kTolerance);
            EXPECT_NEAR(actual.y, expected.y, kTolerance);
            EXPECT_NEAR(actual.z, expected.z, kTolerance);
            EXPECT_NEAR(actual.w, expected.w, kTolerance);
        }

    }  // namespace

    TEST(Renderer3DColorTest, SrgbToLinearIsTheStandardCurve) {
        EXPECT_FLOAT_EQ(SrgbToLinear(0), 0.0f);
        EXPECT_FLOAT_EQ(SrgbToLinear(255), 1.0f);
        EXPECT_NEAR(SrgbToLinear(10), 10.0f / 255.0f / 12.92f, kTolerance);  // the linear toe
        EXPECT_NEAR(SrgbToLinear(128), 0.2158605f, kTolerance);              // mid grey is darker than half
        EXPECT_LT(SrgbToLinear(100), SrgbToLinear(101));
    }

    TEST(Renderer3DColorTest, AlphaIsNotConverted) {
        const Vec4 linear = ColorToLinear(Color{255, 128, 0, 51});
        ExpectNear(linear, Vec4(1.0f, 0.2158605f, 0.0f, 0.2f));
    }

    TEST(Renderer3DLightTest, LambertTermIsTheClampedDotProduct) {
        const Vec3 toLight = Vec3{1.0f, 2.0f, 2.0f} / 3.0f;
        EXPECT_NEAR(LambertTerm(Vec3{0, 1, 0}, toLight), 2.0f / 3.0f, kTolerance);
        EXPECT_NEAR(LambertTerm(Vec3{1, 0, 0}, toLight), 1.0f / 3.0f, kTolerance);
        EXPECT_FLOAT_EQ(LambertTerm(Vec3{0, -1, 0}, toLight), 0.0f);
        EXPECT_FLOAT_EQ(LambertTerm(Vec3{-1, 0, 0}, toLight), 0.0f);
    }

    TEST(Renderer3DRenderTest, ALitCubeIsOnePassWithDepth) {
        Renderer3DFixture f;
        PassDesc base;
        base.clearColor = Color{10, 20, 30, 255};
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, base).has_value());

        EXPECT_TRUE(f.device->errors.empty());
        EXPECT_FALSE(f.device->IsPassOpen());
        EXPECT_EQ(f.device->CountCommands(GpuCommandKind::BeginPass), 1u);
        EXPECT_EQ(f.device->CountCommands(GpuCommandKind::EndPass), 1u);
        EXPECT_EQ(f.device->pipelinesCreated, 1u);
        EXPECT_EQ(f.device->depthTexturesCreated, 1u);
        ASSERT_EQ(f.device->draws.size(), 1u);
        EXPECT_EQ(f.device->draws[0].indexCount, 36u);
        EXPECT_EQ(f.device->draws[0].indexType, IndexType::Uint32);

        const PassDesc& pass = f.device->CommandsOf(GpuCommandKind::BeginPass)[0]->pass;
        ASSERT_TRUE(pass.depth.has_value());
        EXPECT_EQ(pass.depth->loadOp, PassLoadOp::Clear);
        EXPECT_FLOAT_EQ(pass.depth->clearValue, 1.0f);
        EXPECT_EQ(pass.depth->texture->GetWidth(), 800u);
        EXPECT_EQ(pass.depth->texture->GetHeight(), 600u);
        EXPECT_EQ(pass.target, nullptr);
        EXPECT_EQ(pass.clearColor.b, 30);
    }

    TEST(Renderer3DRenderTest, TheCallerPicksTheColorLoadOpOfALaterPass) {
        Renderer3DFixture f;
        PassDesc base;
        base.colorLoadOp = PassLoadOp::Load;
        base.depth = PassDepth{nullptr, PassLoadOp::Load, 0.0f};  // ignored: the renderer owns the depth attachment
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, base).has_value());
        const PassDesc& pass = f.device->CommandsOf(GpuCommandKind::BeginPass)[0]->pass;
        EXPECT_EQ(pass.colorLoadOp, PassLoadOp::Load);
        ASSERT_TRUE(pass.depth.has_value());
        EXPECT_NE(pass.depth->texture, nullptr);
        EXPECT_EQ(pass.depth->loadOp, PassLoadOp::Clear);
    }

    TEST(Renderer3DRenderTest, ThePipelineIsTheLitOne) {
        Renderer3DFixture f;
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());
        const PipelineDesc& desc = f.device->draws[0].pipeline->GetDesc();
        EXPECT_EQ(desc.shader, "Lit3D"_intern);
        EXPECT_TRUE(desc.depthTest);
        EXPECT_TRUE(desc.depthWrite);
        EXPECT_EQ(desc.depthCompare, DepthCompare::Less);
        EXPECT_EQ(desc.depthFormat, DepthFormat::Depth32Float);
        EXPECT_EQ(desc.cullMode, CullMode::Back);
        EXPECT_EQ(desc.blend, BlendMode::Alpha);
        EXPECT_EQ(desc.frameConstantsSize, 112u);
        EXPECT_EQ(desc.drawConstantsSize, 80u);
        EXPECT_EQ(desc.vertexLayout.stride, sizeof(Vertex3D));
        EXPECT_EQ(desc.vertexLayout.attributeCount, 3u);
    }

    TEST(Renderer3DRenderTest, FrameConstantsHoldTheCameraAndTheLight) {
        Renderer3DFixture f;
        f.scene.camera.position = Vec3{2.0f, 3.0f, 6.0f};
        f.scene.camera.target = Vec3{0.0f, 0.5f, 0.0f};
        f.scene.light.color = Color{255, 128, 0, 255};
        f.scene.ambient.color = Color{128, 128, 128, 255};
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());

        const FrameConstants frame = f.device->draws[0].ConstantsAs<FrameConstants>(ConstantsSlot::Frame);
        const Camera3D& camera = f.scene.camera;
        // Built here from the pieces, not through Camera3D: the aspect is the 800 x 600 output
        const Mat4 expected =
            Mat4::Perspective(camera.fovYRadians, 800.0f / 600.0f, camera.nearPlane, camera.farPlane) * Mat4::LookAt(Vec3{2.0f, 3.0f, 6.0f}, Vec3{0.0f, 0.5f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
        ExpectNear(frame.viewProjection, expected);
        // The light travels along (-1, -2, -2), so the direction TO it is the opposite, unit
        ExpectNear(frame.lightDirection, Vec4(1.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.0f));
        EXPECT_NEAR(frame.lightDirection.XYZ().Length(), 1.0f, kTolerance);
        ExpectNear(frame.lightColor, Vec4(2.0f, 2.0f * 0.2158605f, 0.0f, 1.0f));
        ExpectNear(frame.ambientColor, Vec4(0.5f * 0.2158605f, 0.5f * 0.2158605f, 0.5f * 0.2158605f, 1.0f));
        EXPECT_EQ(f.device->draws[0].constants[0].size(), sizeof(FrameConstants));
    }

    TEST(Renderer3DRenderTest, TwoInstancesAreTwoDrawsAndOneFrameBlock) {
        Renderer3DFixture f;
        const Mat4 second = Mat4::Translation(Vec3{2.0f, 0.0f, 0.0f}) * Mat4::RotationY(0.5f);
        f.scene.instances[0].model = Mat4::Translation(Vec3{-2.0f, 0.0f, 0.0f});
        f.scene.instances[0].material.tint = Color{255, 0, 0, 255};
        f.scene.instances.push_back(MeshInstance{f.cube.Get(), Material{{}, Color{0, 255, 0, 51}}, second});
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());

        ASSERT_EQ(f.device->draws.size(), 2u);
        size_t frameBlocks = 0;
        for (const GpuCommand* command : f.device->CommandsOf(GpuCommandKind::SetConstants)) {
            frameBlocks += command->constantsSlot == ConstantsSlot::Frame ? 1 : 0;
        }
        EXPECT_EQ(frameBlocks, 1u);
        const DrawConstants first = f.device->draws[0].ConstantsAs<DrawConstants>(ConstantsSlot::Draw);
        const DrawConstants other = f.device->draws[1].ConstantsAs<DrawConstants>(ConstantsSlot::Draw);
        ExpectNear(first.model, Mat4::Translation(Vec3{-2.0f, 0.0f, 0.0f}));
        ExpectNear(first.tint, Vec4(1.0f, 0.0f, 0.0f, 1.0f));
        ExpectNear(other.model, second);
        ExpectNear(other.tint, Vec4(0.0f, 1.0f, 0.0f, 0.2f));
        // Both draws saw the same frame block
        EXPECT_EQ(f.device->draws[0].constants[0], f.device->draws[1].constants[0]);
    }

    TEST(Renderer3DRenderTest, AnInstanceWithoutATextureGetsOneSharedWhiteTexture) {
        Renderer3DFixture f;
        const Array<uint8_t, 4> pixel = {1, 2, 3, 4};
        IntrusivePtr<ITexture> own = f.renderer.CreateTexture(pixel.data(), 1, 1);
        f.scene.instances.push_back(MeshInstance{f.cube.Get(), Material{own, Color{255, 255, 255, 255}}, Mat4::Identity()});
        f.scene.instances.push_back(MeshInstance{f.cube.Get(), Material{}, Mat4::Identity()});
        f.renderer.texturesCreated = 0;
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());

        ASSERT_EQ(f.device->draws.size(), 3u);
        EXPECT_EQ(f.renderer.texturesCreated, 1u);  // the white one, once
        const ITexture* white = f.device->draws[0].textures[0];
        ASSERT_NE(white, nullptr);
        EXPECT_EQ(static_cast<const RecordedTexture*>(white)->GetPixels(), (Vector<uint8_t>{255, 255, 255, 255}));
        EXPECT_EQ(f.device->draws[1].textures[0], own.Get());
        EXPECT_EQ(f.device->draws[2].textures[0], white);
    }

    TEST(Renderer3DRenderTest, AnInstanceWithoutAMeshIsSkipped) {
        Renderer3DFixture f;
        f.scene.instances.push_back(MeshInstance{nullptr, Material{}, Mat4::Identity()});
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());
        EXPECT_EQ(f.device->draws.size(), 1u);
        EXPECT_TRUE(f.device->errors.empty());
    }

    TEST(Renderer3DRenderTest, AnEmptySceneStillClearsThroughAPass) {
        Renderer3DFixture f;
        f.scene.instances.clear();
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());
        EXPECT_EQ(f.device->CountCommands(GpuCommandKind::BeginPass), 1u);
        EXPECT_TRUE(f.device->draws.empty());
    }

    TEST(Renderer3DRenderTest, ThePipelineAndTheDepthTextureAreMadeOnce) {
        Renderer3DFixture f;
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());
        EXPECT_EQ(f.device->pipelinesCreated, 1u);
        EXPECT_EQ(f.device->depthTexturesCreated, 1u);
        EXPECT_EQ(f.device->draws.size(), 2u);
        EXPECT_EQ(f.device->draws[0].pipeline, f.device->draws[1].pipeline);
    }

    TEST(Renderer3DRenderTest, AResizeMakesANewDepthTexture) {
        Renderer3DFixture f;
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());
        f.renderer.width = 1024.0f;
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());
        EXPECT_EQ(f.device->depthTexturesCreated, 2u);
        EXPECT_EQ(f.device->pipelinesCreated, 1u);
        const PassDesc& pass = f.device->CommandsOf(GpuCommandKind::BeginPass)[1]->pass;
        EXPECT_EQ(pass.depth->texture->GetWidth(), 1024u);
        // The aspect of the frame constants follows the size
        const FrameConstants frame = f.device->draws[1].ConstantsAs<FrameConstants>(ConstantsSlot::Frame);
        ExpectNear(frame.viewProjection, Renderer3D::ViewProjection(f.scene.camera, 1024.0f / 600.0f));
    }

    TEST(Renderer3DRenderTest, AnotherDeviceGetsItsOwnPipeline) {
        Renderer3DFixture f;
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());

        RecordingRenderer otherRenderer;
        IntrusivePtr<RecordingGpuDevice> otherDevice = New<RecordingGpuDevice>();
        otherRenderer.SetGpuDevice(otherDevice);
        const IntrusivePtr<Mesh> otherCube = Mesh::Create(*otherDevice, MakeUnitCube());
        Scene3D otherScene;
        otherScene.instances.push_back(MeshInstance{otherCube.Get(), Material{}, Mat4::Identity()});
        ASSERT_TRUE(f.renderer3D.Render(otherRenderer, otherScene, PassDesc{}).has_value());
        EXPECT_EQ(f.device->pipelinesCreated, 1u);
        EXPECT_EQ(otherDevice->pipelinesCreated, 1u);
        EXPECT_EQ(otherDevice->draws.size(), 1u);
        EXPECT_TRUE(otherDevice->errors.empty());
    }

    TEST(Renderer3DRenderTest, ARendererWithoutADeviceGetsTheErrorAndNothingIsRecorded) {
        Renderer3DFixture f;
        RecordingRenderer bare;
        const std::expected<void, String> result = f.renderer3D.Render(bare, f.scene, PassDesc{});
        ASSERT_FALSE(result.has_value());
        EXPECT_NE(result.error().find("recording renderer has no GPU device"), String::npos);
        EXPECT_TRUE(f.device->commands.empty());
        EXPECT_EQ(bare.texturesCreated, 0u);
    }

    TEST(Renderer3DRenderTest, ATargetWithNoSizeIsRefused) {
        Renderer3DFixture f;
        f.renderer.height = 0.0f;
        EXPECT_FALSE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());
        EXPECT_TRUE(f.device->commands.empty());
    }

    TEST(Renderer3DRenderTest, ResetLetsGoOfTheDevice) {
        Renderer3DFixture f;
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());
        const int32_t references = f.device->GetRefCount();
        f.renderer3D.Reset();
        EXPECT_LT(f.device->GetRefCount(), references);
        ASSERT_TRUE(f.renderer3D.Render(f.renderer, f.scene, PassDesc{}).has_value());
        EXPECT_EQ(f.device->pipelinesCreated, 2u);
    }

    TEST(Renderer3DShaderMathTest, EveryCubeVertexIsInsideTheClipVolumeAndTheLightHitsThreeFaces) {
        Renderer3DFixture f;
        const FrameConstants frame = Renderer3D::MakeFrameConstants(f.scene, 800.0f / 600.0f);
        const Vec3 toLight = frame.lightDirection.XYZ();
        size_t lit = 0;
        size_t dark = 0;
        for (const Vertex3D& vertex : MakeUnitCube().vertices) {
            // What the vertex shader computes: clip = viewProjection * model * position
            EXPECT_TRUE(IsInsideClip(frame.viewProjection * Vec4(vertex.position, 1.0f)));
            const float term = LambertTerm(vertex.normal, toLight);
            // The light comes from +X, +Y, +Z (toLight = (1, 2, 2) / 3): the faces with a positive normal axis are lit, the opposite ones are not
            const bool facesLight = vertex.normal.x + vertex.normal.y + vertex.normal.z > 0.0f;
            if (facesLight) {
                EXPECT_GT(term, 0.0f);
                ++lit;
            } else {
                EXPECT_FLOAT_EQ(term, 0.0f);
                ++dark;
            }
        }
        EXPECT_EQ(lit, 12u);
        EXPECT_EQ(dark, 12u);
    }

}  // namespace BECore::Tests
