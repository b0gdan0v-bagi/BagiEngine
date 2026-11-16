#include "ClickableComponent.h"

#include <BECore/Input/ActionsLibrary.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/InputEvents.h>
#include <Generated/ClickableComponent.gen.hpp>

namespace BECore {

    void ClickableComponent::OnTargetAttached() {
        if (!_actionName.Empty()) {
            ActionsLibrary::GetInstance().Add(_actionName);
        }
    }

    bool ClickableComponent::ContainsScenePoint(float sceneX, float sceneY) const {
        if (_actionName.Empty() && !_blocksInput) {
            return false;
        }
        if (_shape == ClickableShape::Ellipse) {
            const auto transform = GetNode().GetComponent<TransformComponent>();
            return transform && PointerShape{PointerShapeKind::Ellipse}.Contains(Rect{transform->_x, transform->_y, transform->_width, transform->_height}, sceneX, sceneY);
        }
        return PointerTargetComponent::ContainsScenePoint(sceneX, sceneY);
    }

    bool ClickableComponent::OnPointerPress(const PointerEvent& /*event*/) {
        const PoolString action = _actionName;  // a copy: a handler of the event may switch the scene and destroy this component
        if (!action.Empty()) {
            InputEvents::ActionEvent::Emit(action);
        }
        return true;
    }

}  // namespace BECore
