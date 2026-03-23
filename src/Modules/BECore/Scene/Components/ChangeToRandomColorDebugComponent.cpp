#include "ChangeToRandomColorDebugComponent.h"

#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/InputEvents.h>
#include <Generated/ChangeToRandomColorDebugComponent.gen.hpp>

#include <cstdlib>

namespace BECore {

    void ChangeToRandomColorDebugComponent::OnAttached() {
        Subscribe<InputEvents::ActionEvent, &ChangeToRandomColorDebugComponent::OnAction>(this);
    }

    void ChangeToRandomColorDebugComponent::OnAction(const InputEvents::ActionEvent& e) {
        if (!_actionFilter.Empty() && e.action != _actionFilter) {
            return;
        }

        auto quad = GetNode().GetComponent<QuadRendererComponent>();
        if (!quad) {
            return;
        }

        quad->_color = Color{
            static_cast<uint8_t>(rand() % 256),
            static_cast<uint8_t>(rand() % 256),
            static_cast<uint8_t>(rand() % 256),
            255
        };
    }

}  // namespace BECore
