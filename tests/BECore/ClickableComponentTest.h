#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore::Tests {

    /// Pushes a clip rect (output pixels) in OnDraw(): put it before the component whose area it clips on one node
    class ClipPushTestComponent : public IComponent {
        BE_CLASS(ClipPushTestComponent)
    public:
        ClipPushTestComponent() = default;
        ~ClipPushTestComponent() override = default;

        BE_REFLECT_FIELD float _x = 0.0f;
        BE_REFLECT_FIELD float _y = 0.0f;
        BE_REFLECT_FIELD float _width = 0.0f;
        BE_REFLECT_FIELD float _height = 0.0f;

        void OnDraw() override;
    };

    /// Pops the clip ClipPushTestComponent pushed: put it after the clipped component on the same node
    class ClipPopTestComponent : public IComponent {
        BE_CLASS(ClipPopTestComponent)
    public:
        ClipPopTestComponent() = default;
        ~ClipPopTestComponent() override = default;

        void OnDraw() override;
    };

}  // namespace BECore::Tests
