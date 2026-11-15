#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief How much detail a renderer wants from generated geometry (the soft shadows and glows of UIBoxMesh), see IRenderer::GetGeometryDetail().
     *
     * Full is the mesh as designed. Reduced is for a renderer whose cost grows with the number of triangles, as the SDL software renderer's does (a
     * surface and a blit for each blended triangle): fewer blur rings and a coarser corner.
     */
    CORE_ENUM(GeometryDetail, uint8_t, Full, Reduced)

}  // namespace BECore
