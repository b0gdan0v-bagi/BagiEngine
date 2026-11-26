#pragma once

#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }  // namespace InputEvents

    namespace SceneEvents {
        struct SceneUpdateEvent;
        struct SceneFixedUpdateEvent;
    }  // namespace SceneEvents

}  // namespace BECore

namespace BECore::Tests {

    /// Counts everything the engine can call on a component: both update lists, a SubscribeInScene handler and a plain one, OnDraw() and the hooks
    class EnabledProbeComponent : public IComponent {
        BE_CLASS(EnabledProbeComponent)
    public:
        EnabledProbeComponent() = default;
        ~EnabledProbeComponent() override = default;

        int attached = 0;
        int updates = 0;
        int fixedSteps = 0;
        int actions = 0;
        int plainActions = 0;
        int draws = 0;
        int enabledChanges = 0;
        Vector<String> written;             ///< the fields OnPropertyWritten() was told of
        MoveOnlyFunction<void()> onUpdate;  ///< runs inside the frame handler

        void OnAttached() override;
        void OnDraw() override;
        void OnPropertyWritten(StringView field) override;

    protected:
        void OnEnabledChanged() override;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
        void OnFixedUpdate(const SceneEvents::SceneFixedUpdateEvent& event);
        void OnAction(const InputEvents::ActionEvent& event);
        void OnPlainAction(const InputEvents::ActionEvent& event);
    };

}  // namespace BECore::Tests
