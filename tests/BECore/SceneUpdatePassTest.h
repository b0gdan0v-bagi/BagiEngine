#pragma once

#include <BECore/Scene/IComponent.h>
#include <BECore/Utils/MoveOnlyFunction.h>

namespace BECore {

    namespace SceneEvents {
        struct SceneUpdateEvent;
        struct SceneFixedUpdateEvent;
    }  // namespace SceneEvents

}  // namespace BECore

namespace BECore::Tests {

    /**
     * @brief Joins the update lists of its scene from OnAttached() and records its calls.
     *
     * A call is logged by the node's name ("Name" for the frame update, "Name/fixed" for a fixed step) into the log the test
     * set (see SceneUpdatePassTest.cpp), so the order of a pass is readable. @c action runs inside the frame handler.
     * Reflected so that DuplicateNode and a node file can create it by type name.
     */
    class UpdateProbe : public IComponent {
        BE_CLASS(UpdateProbe)
    public:
        UpdateProbe() = default;
        ~UpdateProbe() override = default;

        /// Which lists it joins; set before AddComponent()
        bool frame = true;
        bool fixed = false;

        int updates = 0;
        int fixedSteps = 0;
        float lastDeltaTime = 0.0f;
        float lastFixedDeltaTime = 0.0f;
        const void* lastEvent = nullptr;
        const void* lastFixedEvent = nullptr;
        MoveOnlyFunction<void(UpdateProbe&)> action;

        void OnAttached() override;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
        void OnFixedUpdate(const SceneEvents::SceneFixedUpdateEvent& event);
    };

    /// Subscribes in Initialize(), which a component read from a file runs before it has a scene: the channel path of SubscribeInScene
    class EarlyUpdateProbe : public IComponent {
        BE_CLASS(EarlyUpdateProbe)
    public:
        EarlyUpdateProbe() = default;
        ~EarlyUpdateProbe() override = default;

        int updates = 0;

        void Initialize() override;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
    };

}  // namespace BECore::Tests
