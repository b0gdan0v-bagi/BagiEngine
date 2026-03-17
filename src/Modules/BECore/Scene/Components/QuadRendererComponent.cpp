#include "QuadRendererComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/SceneEvents.h>
#include <Generated/QuadRendererComponent.gen.hpp>

namespace BECore {

    void QuadRendererComponent::Initialize() {
        if (!_node)
            return;
        Subscribe<SceneEvents::SceneDrawEvent, &QuadRendererComponent::OnDraw>(this);
    }

    void QuadRendererComponent::OnDraw(const SceneEvents::SceneDrawEvent&) {
        auto transform = GetNode().GetComponent<TransformComponent>();
        if (!transform)
            return;
        CoreManager::GetRenderer()->DrawFilledRect(transform->_x, transform->_y, transform->_width, transform->_height, _color);
    }

}  // namespace BECore
