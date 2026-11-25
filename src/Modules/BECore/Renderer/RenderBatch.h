#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Renderer/BlendMode.h>
#include <BECore/Renderer/GeometryVertex.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// The most quads one batch holds before the renderer submits it: its vertex count stays inside a 16-bit index
    inline constexpr uint32_t kMaxBatchQuads = 8192;

    /**
     * @brief Quads of one texture and one blend mode that a backend submits as a single draw (IRenderer::SubmitBatch()).
     *
     * The vertices are already in output pixels: the view transform and the opacity were applied when each quad was appended,
     * and the tint is the vertex color (straight alpha). A quad is four vertices, indexed {0, 1, 2, 2, 1, 3} from its first.
     * The clip is not part of the batch: the renderer submits before the clip changes, so the clip in effect at submission is
     * the one every quad was drawn under.
     */
    struct RenderBatch {
        IntrusivePtr<ITexture> texture;  ///< held for the life of the batch so a texture released before the submission does not dangle; null for untextured quads
        BlendMode blend = BlendMode::Alpha;
        Vector<GeometryVertex> vertices;
        Vector<int> indices;
        uint32_t quads = 0;

        /// Empties the batch and keeps the storage, so a steady frame allocates nothing
        void Reset() {
            texture.Reset();
            vertices.clear();
            indices.clear();
            quads = 0;
        }
    };

}  // namespace BECore
