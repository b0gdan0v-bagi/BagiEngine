#include "UIImage.h"

#include <BECore/Renderer/IRenderer.h>
#include <BECore/UI/UIThemes.h>
#include <Generated/UIImage.gen.hpp>

namespace BECore {

    void UIImage::ApplyTheme(UIThemes& themes) {
        UIElement::ApplyTheme(themes);
        themes.Resolve(tint);
    }

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
        const Rect target = stretch ? rect : UISprite::FitInto(rect, source);
        renderer.DrawTexture(*texture, &source, target.x, target.y, target.w, target.h, tint);
    }

}  // namespace BECore
