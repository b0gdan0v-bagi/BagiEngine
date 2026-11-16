#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief The renderer's clip as it was at a component's place in the draw order, kept so a pointer test can honour it later.
     *
     * A scene target is asked about a pointer long after it drew, when the renderer's clip stack is gone, so it copies the
     * clip while it draws (HitClip::FromRenderer in OnDraw). A point outside the clip is not on the target: the part the
     * clip hides is not drawn and must not take a click.
     */
    struct HitClip {
        Optional<Rect> rect;  ///< the clip rect in output pixels; nothing when no clip was pushed
        bool empty = false;   ///< the pushed clips do not overlap: nothing is visible, so nothing is hit

        /// @param renderer may be null (a place drawn without a renderer): no clip
        [[nodiscard]] static HitClip FromRenderer(const IRenderer* renderer) {
            HitClip clip;
            if (renderer != nullptr) {
                clip.rect = renderer->GetClipRect();
                clip.empty = renderer->IsClipEmpty();
            }
            return clip;
        }

        /// Edges count as inside, like the rect test of a scene target
        [[nodiscard]] bool Contains(float x, float y) const {
            if (empty) {
                return false;
            }
            if (!rect.has_value()) {
                return true;
            }
            return x >= rect->x && x <= rect->x + rect->w && y >= rect->y && y <= rect->y + rect->h;
        }
    };

}  // namespace BECore
