#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    class TransformComponent : public IComponent {
        BE_CLASS(TransformComponent)
    public:
        TransformComponent() = default;
        ~TransformComponent() override = default;

        BE_REFLECT_FIELD float _x = 0.0f;
        BE_REFLECT_FIELD float _y = 0.0f;
        BE_REFLECT_FIELD float _width = 1.0f;
        BE_REFLECT_FIELD float _height = 1.0f;
    };

}  // namespace BECore
