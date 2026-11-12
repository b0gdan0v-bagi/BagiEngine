#include "UIToggleComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIElement.h>
#include <Events/InputEvents.h>
#include <Generated/UIToggleComponent.gen.hpp>

namespace BECore {

    UIToggleComponent::UIToggleComponent() {
        onValue = PoolString::Intern("true");
        offValue = PoolString::Intern("false");
    }

    bool UIToggleComponent::IsOn() const {
        if (bind.Empty() || onValue.Empty()) {
            return false;
        }
        return CoreManager::GetUIVars().Get(bind) == onValue.ToStringView();
    }

    void UIToggleComponent::OnClick(UIElement& owner) {
        if (owner.IsDisabled() || action.Empty()) {
            return;
        }
        InputEvents::ActionEvent::Emit(action, IsOn() ? offValue : onValue);
    }

    bool UIToggleComponent::MakesInteractive() const {
        return true;
    }

    bool UIToggleComponent::IsChecked(const UIElement& /*owner*/) const {
        return IsOn();
    }

}  // namespace BECore
