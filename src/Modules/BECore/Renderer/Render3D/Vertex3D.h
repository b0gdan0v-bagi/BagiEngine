#pragma once

#include <BECore/Math/Vec2.h>
#include <BECore/Math/Vec3.h>
#include <BECore/Renderer/Gpu/GpuTypes.h>

namespace BECore {

    /// One vertex of a 3D mesh. The attribute locations below are what the vertex shader declares (the lit shader of the build).
    struct Vertex3D {
        Vec3 position;
        Vec3 normal;  ///< unit, in the model's space
        Vec2 uv;      ///< (0, 0) is the top left of the texture
    };

    static_assert(std::is_standard_layout_v<Vertex3D> && sizeof(Vertex3D) == 8 * sizeof(float), "Vertex3D goes to the GPU as it is: eight tightly packed floats");

    constexpr uint32_t kVertex3DPositionLocation = 0;
    constexpr uint32_t kVertex3DNormalLocation = 1;
    constexpr uint32_t kVertex3DUvLocation = 2;

    /// The vertex buffer layout of Vertex3D
    [[nodiscard]] inline VertexLayout MakeVertex3DLayout() {
        VertexLayout layout;
        layout.stride = sizeof(Vertex3D);
        layout.AddAttribute(kVertex3DPositionLocation, VertexFormat::Float3, offsetof(Vertex3D, position));
        layout.AddAttribute(kVertex3DNormalLocation, VertexFormat::Float3, offsetof(Vertex3D, normal));
        layout.AddAttribute(kVertex3DUvLocation, VertexFormat::Float2, offsetof(Vertex3D, uv));
        return layout;
    }

}  // namespace BECore
