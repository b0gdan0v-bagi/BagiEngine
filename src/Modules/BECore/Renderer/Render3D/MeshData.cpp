#include <BECore/Renderer/Render3D/MeshData.h>

namespace BECore {

    namespace {

        constexpr float kHalfSide = 0.5f;
        constexpr size_t kCubeFaceCount = 6;
        constexpr size_t kFaceCorners = 4;

        /// A face is its outward normal and two axes with `u x v == normal`, so the corners (-u -v), (+u -v), (+u +v), (-u +v) run counter-clockwise seen from outside
        struct CubeFace {
            Vec3 normal;
            Vec3 u;
            Vec3 v;
        };

        constexpr Array<CubeFace, kCubeFaceCount> kCubeFaces = {{
            {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
            {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
            {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
            {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
            {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
            {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
        }};

        struct CubeCorner {
            float u;
            float v;
            Vec2 uv;
        };

        /// v up in the face is the top of the texture
        constexpr Array<CubeCorner, kFaceCorners> kCubeCorners = {{
            {-kHalfSide, -kHalfSide, {0.0f, 1.0f}},
            {kHalfSide, -kHalfSide, {1.0f, 1.0f}},
            {kHalfSide, kHalfSide, {1.0f, 0.0f}},
            {-kHalfSide, kHalfSide, {0.0f, 0.0f}},
        }};

        constexpr Array<uint32_t, 6> kFaceTriangleCorners = {0, 1, 2, 0, 2, 3};

    }  // namespace

    MeshData MakeUnitCube() {
        MeshData mesh;
        mesh.vertices.reserve(kCubeFaceCount * kFaceCorners);
        mesh.indices.reserve(kCubeFaceCount * kFaceTriangleCorners.size());
        for (const CubeFace& face : kCubeFaces) {
            const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
            for (const CubeCorner& corner : kCubeCorners) {
                mesh.vertices.push_back(Vertex3D{face.normal * kHalfSide + face.u * corner.u + face.v * corner.v, face.normal, corner.uv});
            }
            for (const uint32_t cornerIndex : kFaceTriangleCorners) {
                mesh.indices.push_back(first + cornerIndex);
            }
        }
        return mesh;
    }

}  // namespace BECore
