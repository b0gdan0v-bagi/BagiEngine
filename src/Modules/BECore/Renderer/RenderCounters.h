#pragma once

#include <cstdint>

namespace BECore {

    /// What a renderer drew since the counters were last taken (IRenderer::TakeRenderCounters)
    struct RenderCounters {
        uint32_t drawCalls = 0;        // what reached the backend: one per submission, so a batch of many quads is one
        uint64_t primitives = 0;       // what the callers asked for: a rect, a textured quad or a DrawGeometry call each; equal to drawCalls without batching
        uint64_t triangles = 0;        // a rect or a textured quad is 2, DrawGeometry its whole triangles
        uint32_t textureSwitches = 0;  // a textured draw whose texture is not the one of the textured draw before it
    };

}  // namespace BECore
