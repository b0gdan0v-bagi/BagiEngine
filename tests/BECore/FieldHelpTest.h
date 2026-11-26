#pragma once

#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::Tests {

    // Reflected test data of FieldHelpTest and FieldHelpTooltipTest; the .gen.hpp is included in FieldHelpTest.cpp only

    /// The parent of the probe: one field with a `tooltip` and a `range`, one with neither
    class HelpProbeBase : public IComponent {
        BE_CLASS(HelpProbeBase)
    public:
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 10), tooltip("How fast it goes")) float speed = 1.0f;
        BE_REFLECT_FIELD bool active = true;

        bool HasOnDraw() const override {
            return false;
        }
    };

    class HelpProbeComponent : public HelpProbeBase {
        BE_CLASS(HelpProbeComponent)
    public:
        BE_REFLECT_FIELD String title{"probe"};
        BE_REFLECT_FIELD int32_t count = 3;
    };

}  // namespace BECore::Tests
