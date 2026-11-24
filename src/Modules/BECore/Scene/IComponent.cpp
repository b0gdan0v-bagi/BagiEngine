#include "IComponent.h"

#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Generated/IComponent.gen.hpp>

namespace BECore {

    // The scene's update list holds no flag per component, so the class must not grow (a one-byte member makes it 136 bytes).
    static_assert(sizeof(IComponent) == 128);

    Scene& IComponent::GetScene() const {
        ASSERT(_scene != nullptr);
        return *_scene;
    }

    SceneNode& IComponent::GetNode() const {
        ASSERT(_node != nullptr);
        return *_node;
    }

    bool IComponent::IsSceneActive() const {
        return _scene != nullptr && _scene->IsActive();
    }

    void IComponent::RegisterSceneUpdate(SceneUpdateKind kind, SceneUpdateInvoker invoke) {
        ASSERT(_scene != nullptr);
        _scene->AddUpdater(kind, *this, invoke, {});
    }

}  // namespace BECore
