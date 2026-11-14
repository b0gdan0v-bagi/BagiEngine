#pragma once

namespace BECore {

    /// What a renderer drew since the counters were last taken (IRenderer::TakeRenderCounters)
    struct RenderCounters {
        uint32_t drawCalls = 0;
        uint64_t triangles = 0;        // a rect or a textured quad is 2, DrawGeometry its whole triangles
        uint32_t textureSwitches = 0;  // a textured draw whose texture is not the one of the textured draw before it
    };

}  // namespace BECore
