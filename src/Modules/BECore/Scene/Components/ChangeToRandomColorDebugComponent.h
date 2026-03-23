#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }  // namespace InputEvents

    class ChangeToRandomColorDebugComponent : public IComponent {
        BE_CLASS(ChangeToRandomColorDebugComponent)
    public:
        ChangeToRandomColorDebugComponent() = default;
        ~ChangeToRandomColorDebugComponent() override = default;

        BE_REFLECT_FIELD PoolString _actionFilter;

        void OnAttached() override;

    private:
        void OnAction(const InputEvents::ActionEvent& e);
    };

}  // namespace BECore
