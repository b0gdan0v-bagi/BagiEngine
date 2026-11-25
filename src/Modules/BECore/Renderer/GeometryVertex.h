#pragma once

#include <BECore/Math/Color.h>

namespace BECore {

    /// One corner of a triangle for IRenderer::DrawGeometry()
    struct GeometryVertex {
        float x = 0.0f;  ///< the position, in the units of DrawFilledRect() (the translation and the view transform are applied to it)
        float y = 0.0f;
        Color color;     ///< straight alpha; it multiplies the texel of a textured draw, or is the color itself of an untextured one
        float u = 0.0f;  ///< the position in the texture, 0 to 1 over the whole texture
        float v = 0.0f;
    };

}  // namespace BECore
