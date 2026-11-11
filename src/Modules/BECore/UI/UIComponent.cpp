#include "UIComponent.h"

#include <BECore/UI/UIElement.h>
#include <Generated/UIComponent.gen.hpp>

namespace BECore {

    void UIComponent::DrawSelf(IRenderer& /*renderer*/, UIElement& /*owner*/) {}

    UIContentFit UIComponent::CheckContent(const UIElement& /*owner*/, float /*width*/, float /*height*/, float /*scale*/) const {
        return {};
    }

    void UIComponent::OnPress(UIElement& /*owner*/, float /*x*/, float /*y*/) {}

    void UIComponent::OnDrag(UIElement& /*owner*/, float /*x*/, float /*y*/) {}

    void UIComponent::OnClick(UIElement& /*owner*/) {}

    void UIComponent::ApplyTheme(UIThemes& /*themes*/) {}

    bool UIComponent::MakesInteractive() const {
        return false;
    }

    bool UIComponent::GivesStateLook() const {
        return false;
    }

}  // namespace BECore
