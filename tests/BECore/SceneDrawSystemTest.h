#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore::Tests {

    /// Draws a recognisable rect in OnDraw(): x = @c marker, a unit size. Used to see where in a node's component order the system calls OnDraw().
    class DrawMarkerComponent : public IComponent {
        BE_CLASS(DrawMarkerComponent)
    public:
        DrawMarkerComponent() = default;
        ~DrawMarkerComponent() override = default;

        float marker = 0.0f;

        void OnDraw() override;
    };

}  // namespace BECore::Tests
