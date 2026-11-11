#pragma once

#include <BECore/Renderer/ITexture.h>

namespace BECore {

    /// What a mesh instance looks like: the texture multiplied by the tint. No texture means a white one (Renderer3D supplies it), so the tint alone colors the mesh.
    struct Material {
        IntrusivePtr<ITexture> texture;
        Color tint{255, 255, 255, 255};  ///< sRGB bytes like every Color of the engine; the renderer converts to linear
    };

}  // namespace BECore
