#include "ClickableComponent.h"

#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Generated/ClickableComponent.gen.hpp>

namespace BECore {

    void ClickableComponent::OnAttached() {
        Subscribe<SDLEvents::MouseButtonDownEvent, &ClickableComponent::OnMouseButtonDown>(this);
    }

    void ClickableComponent::OnMouseButtonDown(const SDLEvents::MouseButtonDownEvent& e) {
        if (_actionName.Empty()) {
            return;
        }

        auto transform = GetNode().GetComponent<TransformComponent>();
        if (!transform) {
            return;
        }

        const float x = e.button.x;
        const float y = e.button.y;
        if (x >= transform->_x && x <= transform->_x + transform->_width &&
            y >= transform->_y && y <= transform->_y + transform->_height) {
            InputEvents::ActionEvent::Emit(_actionName);
        }
    }

}  // namespace BECore
