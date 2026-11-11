#include <BECore/Renderer/Render3D/Camera3D.h>
#include <BECore/Renderer/Render3D/Mesh.h>
#include <BECore/Renderer/Render3D/Scene3D.h>
#include <Common/RecordingGpuDevice.h>
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

}  // namespace BECore::Tests
