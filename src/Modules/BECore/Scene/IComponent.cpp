#include "IComponent.h"

#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Generated/IComponent.gen.hpp>

namespace BECore {

    Scene& IComponent::GetScene() const {
        ASSERT(_scene != nullptr);
        return *_scene;
    }

    SceneNode& IComponent::GetNode() const {
        ASSERT(_node != nullptr);
        return *_node;
    }

}  // namespace BECore
