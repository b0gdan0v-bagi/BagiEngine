#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>

namespace BECore {

    class ParticleEmitterComponent;

    namespace InputEvents {
        struct ActionEvent;
    }  // namespace InputEvents

}  // namespace BECore

namespace BECore::Demo {

    /**
     * @brief The state behind the demo's panel: whether the tweens run (DemoNames::animateVar) and how much the looping emitter emits
     * (DemoNames::emissionVar).
     *
     * A widget only asks: this component answers the toggle's action by setting the variable and the slider's by setting the emitter's rate.
     * The emission variable follows the emitter, not the slider, so a rate edited in the inspector shows on the panel too. The emitter is
     * looked up by its node's name at every use: a node the editor removed or renamed is simply not found.
     */
    class DemoControlsComponent : public IComponent {
        BE_CLASS(DemoControlsComponent, ICON = sliders - horizontal)
    public:
        DemoControlsComponent() = default;
        ~DemoControlsComponent() override = default;

        BE_REFLECT_FIELD BE_FIELD_ATTRS(tooltip("The node of the looping emitter that the emission slider drives")) PoolString _emitterNode;

        bool HasOnDraw() const override {
            return false;
        }

        void OnAttached() override;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent& e);
        void OnAction(const InputEvents::ActionEvent& e);
        [[nodiscard]] IntrusivePtr<ParticleEmitterComponent> FindEmitter() const;
        /// Publishes the rate of @p emitter when it is not the published one, and restarts a looping schedule whose rate went down
        void FollowRate(ParticleEmitterComponent& emitter);

        Optional<float> _publishedRate;
    };

}  // namespace BECore::Demo
