#include "SpriteRendererComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/SceneEvents.h>
#include <Generated/SpriteRendererComponent.gen.hpp>

namespace BECore {

    void SpriteRendererComponent::Initialize() {
        if (!_node || _texturePath.Empty()) {
            return;
        }
        _texture = CoreManager::GetResourceManager().Load<ITexture>(_texturePath.ToStringView());
        if (!_texture) {
            LOG_ERROR(Format("SpriteRendererComponent: failed to load texture '{}'", _texturePath.ToStringView()).c_str());
            return;
        }
        Subscribe<SceneEvents::SceneDrawEvent, &SpriteRendererComponent::OnDraw>(this);
    }

    void SpriteRendererComponent::OnDraw(const SceneEvents::SceneDrawEvent&) {
        if (!_texture) {
            return;
        }
        auto transform = GetNode().GetComponent<TransformComponent>();
        if (!transform) {
            return;
        }
        const Rect* src = _srcRect.IsEmpty() ? nullptr : &_srcRect;
        CoreManager::GetRenderer()->DrawTexture(*_texture, src, transform->_x, transform->_y, transform->_width, transform->_height);
    }

}  // namespace BECore
