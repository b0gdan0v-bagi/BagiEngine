#include "QuadRendererComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <Generated/QuadRendererComponent.gen.hpp>

namespace BECore {

    void QuadRendererComponent::OnDraw() {
        auto transform = GetNode().GetComponent<TransformComponent>();
        if (!transform) {
            return;
        }
        CoreManager::GetRenderer()->DrawFilledRect(transform->_x, transform->_y, transform->_width, transform->_height, _color);
    }

}  // namespace BECore
