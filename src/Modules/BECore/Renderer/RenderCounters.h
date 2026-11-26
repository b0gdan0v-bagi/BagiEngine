#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// Why a batch was submitted (IRenderer::Flush()). `Texture`: the next quad has another texture, or none against one. `Blend`: the same
    /// texture under another blend mode. `Full`: kMaxBatchQuads. `Clip`: the clip changes. `Unbatched`: a draw made at once follows.
    /// `Boundary`: everything else (the end of a pass, frame or target, a clear, a present, a capture, ImGui, a counters pause, batching off).
    CORE_ENUM(BatchBreak, uint8_t, Texture, Blend, Full, Clip, Unbatched, Boundary)

    inline constexpr size_t kBatchBreakCount = 6;
    static_assert(kBatchBreakCount == EnumUtils<BatchBreak>::Count(), "kBatchBreakCount follows BatchBreak");

    /// What a renderer drew and uploaded since the counters were last taken (IRenderer::TakeRenderCounters)
    struct RenderCounters {
        uint32_t drawCalls = 0;                           // what reached the backend: one per submission, so a batch of many quads is one
        uint64_t primitives = 0;                          // what the callers asked for: a rect, a textured quad or a DrawGeometry call each; equal to drawCalls without batching
        uint64_t triangles = 0;                           // a rect or a textured quad is 2, DrawGeometry its whole triangles
        uint32_t textureSwitches = 0;                     // a textured draw whose texture is not the one of the textured draw before it
        Array<uint32_t, kBatchBreakCount> batchBreaks{};  // by BatchBreak: each batch submission under one reason, so drawCalls minus their sum is the unbatched draws
        uint64_t geometryUploadBytes = 0;                 // vertex and index data a backend wrote or handed over. Each backend has its own layout, and a draw that hands over
                                                          // none (SDL's immediate quads) adds 0, so the number compares only within one backend
        uint64_t textureUploadBytes = 0;                  // RGBA8 size of every texture filled with pixels; a render target is not filled

        [[nodiscard]] uint32_t GetBatchBreaks(BatchBreak reason) const {
            return batchBreaks[+reason];
        }
    };

}  // namespace BECore
