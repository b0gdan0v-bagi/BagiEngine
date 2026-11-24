#include "IComponent.h"

#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Generated/IComponent.gen.hpp>

namespace BECore {

    // The scene's update list holds no flag per component, so the class must not grow by a member; the bound is in pointers, so it holds on 32-bit too.
    static_assert(sizeof(IComponent) <= 16 * sizeof(void*));

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
