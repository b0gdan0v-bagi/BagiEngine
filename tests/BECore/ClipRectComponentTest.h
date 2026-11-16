#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>

namespace BECore::Tests {

    /// What the renderer's clip was at one OnDraw() of a ClipProbeTestComponent
    struct ClipSample {
        size_t depth = 0;
        Optional<Rect> clip;
    };

    /// Appends the clip in effect at its place in the draw order to `samples`
    class ClipProbeTestComponent : public IComponent {
        BE_CLASS(ClipProbeTestComponent)
    public:
        ClipProbeTestComponent() = default;
        ~ClipProbeTestComponent() override = default;

        Vector<ClipSample>* samples = nullptr;

        void OnDraw() override;
    };

}  // namespace BECore::Tests
