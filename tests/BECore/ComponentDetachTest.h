#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }  // namespace InputEvents

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }  // namespace SceneEvents

}  // namespace BECore

namespace BECore::Tests {

    /// Subscribes only when the test calls Join(), so a test can do it while the component is detached
    class LateJoinProbe : public IComponent {
        BE_CLASS(LateJoinProbe)
    public:
        LateJoinProbe() = default;
        ~LateJoinProbe() override = default;

        int updates = 0;
        int actions = 0;

        void Join();

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
        void OnAction(const InputEvents::ActionEvent& event);
    };

}  // namespace BECore::Tests
