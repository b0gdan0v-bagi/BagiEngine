#include "IComponent.h"

#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Generated/IComponent.gen.hpp>

namespace BECore {

    // Every component of every scene pays for each byte here, so a new member has to be worth it; the bound is in pointers, so it holds on 32-bit too.
    static_assert(sizeof(IComponent) <= 16 * sizeof(void*));

    Scene* IComponent::TryGetScene() const {
        return _node != nullptr ? _node->_scene : nullptr;
    }

    Scene& IComponent::GetScene() const {
        Scene* const scene = TryGetScene();
        ASSERT(scene != nullptr);
        return *scene;
    }

    SceneNode& IComponent::GetNode() const {
        ASSERT(_node != nullptr);
        return *_node;
    }

    bool IComponent::IsSceneActive() const {
        const Scene* const scene = TryGetScene();
        return scene != nullptr && scene->IsActive();
    }

    void IComponent::SetEnabled(bool enabled) {
        if (_enabled == enabled) {
            return;
        }
        _enabled = enabled;
        OnEnabledChanged();
    }

    void IComponent::RegisterSceneUpdate(SceneUpdateKind kind, SceneUpdateInvoker invoke) {
        GetScene().AddUpdater(kind, *this, invoke, {});
    }

}  // namespace BECore
