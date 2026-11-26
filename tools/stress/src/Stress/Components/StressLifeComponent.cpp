#include "StressLifeComponent.h"

#include <Events/SceneEvents.h>
#include <Generated/StressLifeComponent.gen.hpp>

namespace BECore::Stress {

    void StressLifeComponent::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &StressLifeComponent::OnUpdate>(this);
    }

    void StressLifeComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& event) {
        _age += event.deltaTime;
    }

}  // namespace BECore::Stress
