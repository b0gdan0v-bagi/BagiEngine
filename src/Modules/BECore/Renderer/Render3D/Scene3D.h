#pragma once

#include <BECore/Renderer/Render3D/Camera3D.h>
#include <BECore/Renderer/Render3D/Lighting.h>
#include <BECore/Renderer/Render3D/Material.h>
#include <BECore/Renderer/Render3D/Mesh.h>

namespace BECore {

    /// One mesh drawn with a material at a place
    struct MeshInstance {
        Mesh* mesh = nullptr;  ///< not owned; alive during Renderer3D::Render()
        Material material;
        Mat4 model;  ///< model to world
    };

    /// Everything one 3D pass draws: a camera, the lights and the instances in the order they are drawn
    struct Scene3D {
        Camera3D camera;
        DirectionalLight light;
        Ambient ambient;
        Vector<MeshInstance> instances;
    };

}  // namespace BECore
