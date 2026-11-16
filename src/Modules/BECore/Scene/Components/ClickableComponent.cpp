#include "ClickableComponent.h"

#include <BECore/Input/ActionsLibrary.h>
#include <Events/InputEvents.h>
#include <Generated/ClickableComponent.gen.hpp>

namespace BECore {

    void ClickableComponent::OnTargetAttached() {
        if (!_actionName.Empty()) {
            ActionsLibrary::GetInstance().Add(_actionName);
        }
    }

    bool ClickableComponent::ContainsScenePoint(float sceneX, float sceneY) const {
        return !_actionName.Empty() && PointerTargetComponent::ContainsScenePoint(sceneX, sceneY);
    }

    bool ClickableComponent::OnPointerPress(const PointerEvent& /*event*/) {
        const PoolString action = _actionName;  // a copy: a handler of the event may switch the scene and destroy this component
        InputEvents::ActionEvent::Emit(action);
        return true;
    }

}  // namespace BECore
