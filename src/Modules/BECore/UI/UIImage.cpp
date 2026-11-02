#include "UIImage.h"

#include <BECore/Renderer/IRenderer.h>
#include <Generated/UIImage.gen.hpp>

namespace BECore {

    void UIImage::DrawSelf(IRenderer& renderer) {
        UIElement::DrawSelf(renderer);
        if (rect.IsEmpty() || tint.a == 0) {
            return;
        }
        ITexture* texture = _sprite.Resolve(sprite);
        if (!texture) {
            return;
        }

        const Rect source = _sprite.GetSource();
        Rect target = rect;
        if (!stretch && !source.IsEmpty()) {
            // The largest rect of the sprite's proportions that fits, centered
            const float scale = std::min(rect.w / source.w, rect.h / source.h);
            target.w = source.w * scale;
            target.h = source.h * scale;
            target.x = rect.x + (rect.w - target.w) * 0.5f;
            target.y = rect.y + (rect.h - target.h) * 0.5f;
        }
        renderer.DrawTexture(*texture, &source, target.x, target.y, target.w, target.h, tint);
    }

}  // namespace BECore
