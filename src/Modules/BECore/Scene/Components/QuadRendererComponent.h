#pragma once

#include <BECore/Renderer/BlendMode.h>
#include <BECore/Scene/IComponent.h>

namespace BECore {

    /// A filled rect at the node's transform. It holds the field; SceneDrawSystem draws it from the registry (SceneRenderData.h).
    class QuadRendererComponent : public IComponent {
        BE_CLASS(QuadRendererComponent)
    public:
        QuadRendererComponent() = default;
        ~QuadRendererComponent() override = default;

        BE_REFLECT_FIELD Color _color{255, 0, 0, 255};
        BE_REFLECT_FIELD BlendMode _blend = BlendMode::Alpha;  ///< how the quad blends with what is behind it; Alpha is straight alpha
    };

}  // namespace BECore
