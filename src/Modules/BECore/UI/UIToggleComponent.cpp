#include "UIToggleComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIVars.h>
#include <Events/InputEvents.h>
#include <Generated/UIToggleComponent.gen.hpp>

namespace BECore {

    namespace {

        struct Ids {
            static constexpr auto on = "true"_intern;
            static constexpr auto off = "false"_intern;
        };

    }  // namespace

    UIToggleComponent::UIToggleComponent() {
        onValue = Ids::on;
        offValue = Ids::off;
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
