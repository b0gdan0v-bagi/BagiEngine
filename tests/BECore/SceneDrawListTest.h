#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore::Tests {

    /// Overrides OnDraw(): in the draw list by the default of HasOnDraw()
    class OnDrawTestComponent : public IComponent {
        BE_CLASS(OnDrawTestComponent)
    public:
        OnDrawTestComponent() = default;
        ~OnDrawTestComponent() override = default;

        void OnDraw() override {}
    };

    /// Overrides nothing: in the draw list too, false is an opt-out
    class PlainTestComponent : public IComponent {
        BE_CLASS(PlainTestComponent)
    public:
        PlainTestComponent() = default;
        ~PlainTestComponent() override = default;
    };

}  // namespace BECore::Tests
