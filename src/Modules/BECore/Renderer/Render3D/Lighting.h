#pragma once

#include <BECore/Math/Vec3.h>

namespace BECore {

    /// A light that is far away: every point is lit from the same side
    struct DirectionalLight {
        Vec3 direction{-0.3f, -1.0f, -0.5f};  ///< the way the light travels (from the lamp into the scene), world space; need not be unit
        Color color{255, 255, 255, 255};      ///< sRGB bytes; the alpha is not used
        float intensity = 1.0f;
    };

    /// The light every surface gets, from any side
    struct Ambient {
        Color color{255, 255, 255, 255};
        float intensity = 0.2f;
    };

}  // namespace BECore
