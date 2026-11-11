#include "UIImageComponent.h"

#include <BECore/Renderer/IRenderer.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIThemes.h>
#include <Generated/UIImageComponent.gen.hpp>

namespace BECore {

    Color UIImageComponent::GetStateTint(const UIStateLook::State& state) const {
        return UIStateLook::ResolveTint(tint, {hoverTint, pressedTint, disabledTint}, state);
    }

    void UIImageComponent::ApplyTheme(UIThemes& themes) {
        themes.Resolve(tint);
        themes.Resolve(hoverTint);
        themes.Resolve(pressedTint);
        themes.Resolve(disabledTint);
    }

    void UIImageComponent::DrawSelf(IRenderer& renderer, UIElement& owner) {
        if (owner.rect.IsEmpty() || tint.a == 0) {
            return;
        }
        ITexture* texture = _sprite.Resolve(sprite);
        if (!texture) {
            return;
        }
        const Color stateTint = GetStateTint(owner.GetVisualState());
        if (stateTint.a == 0) {
            return;
        }
        const Rect source = _sprite.GetSource();
        const Rect target = stretch ? owner.rect : UISprite::FitInto(owner.rect, source);
        renderer.DrawTexture(*texture, &source, target.x, target.y, target.w, target.h, stateTint);
    }

}  // namespace BECore
