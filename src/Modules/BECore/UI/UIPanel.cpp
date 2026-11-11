#include "UIPanel.h"

#include <BECore/Renderer/IRenderer.h>
#include <BECore/UI/UINineSlice.h>
#include <BECore/UI/UIThemes.h>
#include <Generated/UIPanel.gen.hpp>

namespace BECore {

    void UIPanel::ApplyTheme(UIThemes& themes) {
        UIElement::ApplyTheme(themes);
        themes.Resolve(spriteTint);
    }

    void UIPanel::DrawSelf(IRenderer& renderer) {
        // The box first (rounded, bordered, shadowed as authored), then the sprite over it. The sprite is a plain textured rect: it is not cut to the rounded corners, so a
        // panel with rounded corners and a sprite wants a sprite with transparent corners
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
