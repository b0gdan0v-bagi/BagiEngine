#include "UIPanel.h"

#include <BECore/Renderer/IRenderer.h>
#include <BECore/UI/UINineSlice.h>
#include <Generated/UIPanel.gen.hpp>

namespace BECore {

    void UIPanel::DrawSelf(IRenderer& renderer) {
        UIElement::DrawSelf(renderer);
        if (rect.IsEmpty() || spriteTint.a == 0) {
            return;
        }
        ITexture* texture = _sprite.Resolve(sprite);
        if (!texture) {
            return;
        }

        const Rect source = _sprite.GetSource();
        if (sliceBorder == UIEdges{}) {
            renderer.DrawTexture(*texture, &source, rect.x, rect.y, rect.w, rect.h, spriteTint);
            return;
        }
        for (const UINineSlicePiece& piece : UINineSlice::Compute(source, sliceBorder, rect)) {
            if (!piece.destination.IsEmpty()) {
                renderer.DrawTexture(*texture, &piece.source, piece.destination.x, piece.destination.y, piece.destination.w, piece.destination.h, spriteTint);
            }
        }
    }

}  // namespace BECore
