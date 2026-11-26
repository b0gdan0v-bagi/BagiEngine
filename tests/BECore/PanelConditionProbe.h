#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore::Tests {

    CORE_ENUM(ConditionProbeMode, uint8_t, Idle, Busy, Failed)

    /// The source object of the condition tests: a bool, a number, a float, a text and an enum
    struct ConditionProbe {
        BE_CLASS(ConditionProbe)

        BE_REFLECT_FIELD bool flag = false;
        BE_REFLECT_FIELD int32_t count = 0;
        BE_REFLECT_FIELD float ratio = 0.0f;
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD ConditionProbeMode mode = ConditionProbeMode::Idle;
    };

}  // namespace BECore::Tests
