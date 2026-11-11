#pragma once

#include <BECore/Renderer/Render3D/Vertex3D.h>

namespace BECore {

    /// A triangle mesh in memory: counter-clockwise triangles are the front faces (the pipeline culls the back ones)
    struct MeshData {
        eastl::vector<Vertex3D> vertices;
        eastl::vector<uint32_t> indices;  ///< three per triangle, into `vertices`
    };

    /// A cube of side 1 centered on the origin: 24 vertices (four per face, so each face has its own normal and one UV square) and 36 indices
    [[nodiscard]] MeshData MakeUnitCube();

}  // namespace BECore
