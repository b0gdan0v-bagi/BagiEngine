#pragma once

#include <BECore/Memory/Pooled.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/IComponent.h>

namespace BECore::SceneEvents {
    struct SceneUpdateEvent;
}

namespace BECore::Stress {

    /**
     * @brief The behaviour of a sprite that can despawn (`--stress-churn`): an update that sums the sprite's age.
     *
     * It does next to nothing on purpose. It is there so that a despawn and a spawn change the update list of the scene as well as its
     * draw list, the way a game's objects do.
     */
    class StressLifeComponent : public IComponent {
        BE_CLASS(StressLifeComponent)
    public:
        BE_POOLED_SINGLE_THREAD(StressLifeComponent)

        StressLifeComponent() = default;
        ~StressLifeComponent() override = default;
        bool HasOnDraw() const override {
            return false;
        }

        void OnAttached() override;

        /// The seconds of the updates this component has had
        float GetAge() const {
            return _age;
        }

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);

        float _age = 0.0f;
    };

}  // namespace BECore::Stress
