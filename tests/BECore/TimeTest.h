#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace SceneEvents {
        struct SceneUpdateEvent;
        struct SceneFixedUpdateEvent;
    }  // namespace SceneEvents

}  // namespace BECore

namespace BECore::Tests {

    /**
     * @brief Moves at a constant speed the way a game component should: distance = speed * dt.
     *
     * Reflected so a SceneConfig-style XML can create it by type name (see TimeTest.cpp).
     */
    class SpeedProbe : public IComponent {
        BE_CLASS(SpeedProbe)
    public:
        SpeedProbe() = default;
        ~SpeedProbe() override = default;

        static constexpr float kSpeed = 100.0f;

        float distance = 0.0f;       // integrated on SceneUpdateEvent with the frame time
        float fixedDistance = 0.0f;  // integrated on SceneFixedUpdateEvent with the fixed step
        float lastDeltaTime = 0.0f;
        int updateCount = 0;
        int fixedStepCount = 0;
        int eventOrder = 0;      // counts every event the probe received
        int lastFixedOrder = 0;  // eventOrder at the last fixed step
        int updateOrder = 0;     // eventOrder at the last update

        void OnAttached() override;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
        void OnFixedUpdate(const SceneEvents::SceneFixedUpdateEvent& event);
    };

}  // namespace BECore::Tests
