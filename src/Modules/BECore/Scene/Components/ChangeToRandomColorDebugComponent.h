#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }  // namespace InputEvents

    class ChangeToRandomColorDebugComponent : public IComponent {
        BE_CLASS(ChangeToRandomColorDebugComponent, ICON = bug, CATEGORY = logic)
    public:
        ChangeToRandomColorDebugComponent() = default;
        ~ChangeToRandomColorDebugComponent() override = default;
        bool HasOnDraw() const override {
            return false;
        }

        BE_REFLECT_FIELD PoolString _actionFilter;

        void OnAttached() override;

    private:
        void OnAction(const InputEvents::ActionEvent& e);
    };

}  // namespace BECore
