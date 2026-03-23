#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace SceneEvents {
        struct SceneDrawEvent;
    }

    class QuadRendererComponent : public IComponent {
        BE_CLASS(QuadRendererComponent)
    public:
        QuadRendererComponent() = default;
        ~QuadRendererComponent() override = default;

        BE_REFLECT_FIELD Color _color{255, 0, 0, 255};

        void OnAttached() override;

    private:
        void OnDraw(const SceneEvents::SceneDrawEvent&);
    };

}  // namespace BECore
