#include "UIInputClicker.h"

#include <BECore/UI/UIElement.h>
#include <Events/InputEvents.h>
#include <Generated/UIInputClicker.gen.hpp>

namespace BECore {

    void UIInputClicker::OnClick(UIElement& owner) {
        if (owner.IsDisabled() || action.Empty()) {
            return;
        }
        InputEvents::ActionEvent::Emit(action, value);
    }

    bool UIInputClicker::MakesInteractive() const {
        return true;
    }

    bool UIInputClicker::GivesStateLook() const {
        return true;
    }

}  // namespace BECore
