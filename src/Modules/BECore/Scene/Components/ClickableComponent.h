#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace SDLEvents {
        struct MouseButtonDownEvent;
    }  // namespace SDLEvents

    class ClickableComponent : public IComponent {
        BE_CLASS(ClickableComponent)
    public:
        ClickableComponent() = default;
        ~ClickableComponent() override = default;

        BE_REFLECT_FIELD PoolString _actionName;

        void OnAttached() override;

    private:
        void OnMouseButtonDown(const SDLEvents::MouseButtonDownEvent& e);
    };

}  // namespace BECore
