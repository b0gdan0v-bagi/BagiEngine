#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/IComponent.h>
#include <cstdint>

namespace BECore::Tests {

    /// Makes @c count children of its node in OnAttached, as a game component that builds its own parts does; they are generated nodes.
    /// Its own header: a test that loads the type needs its generated traits, and a `.gen.hpp` is included from one `.cpp` only.
    class GeneratingTestComponent : public IComponent {
        BE_CLASS(GeneratingTestComponent)
    public:
        GeneratingTestComponent() = default;
        ~GeneratingTestComponent() override = default;

        BE_REFLECT_FIELD int32_t count = 0;

        void OnAttached() override;
    };

}  // namespace BECore::Tests
