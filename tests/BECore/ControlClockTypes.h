#pragma once

#include <BECore/Control/ControlClock.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// An answer that derives the clock; the keys sort before, between and after the clock's own
    struct ControlClockTestAnswer : ControlClock {
        BE_CLASS(ControlClockTestAnswer)

        BE_REFLECT_FIELD int32_t zz = 3;
        BE_REFLECT_FIELD int32_t a = 1;
        BE_REFLECT_FIELD int32_t fz = 2;
    };

}  // namespace BECore
