#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore::Tests {

    CORE_ENUM(PanelProbeMode, uint8_t, Idle, Busy, Failed)

    /// A row of a bound list
    struct PanelProbeRow {
        BE_CLASS(PanelProbeRow)

        BE_REFLECT_FIELD String path;
        BE_REFLECT_FIELD int32_t size = 0;
        BE_REFLECT_FIELD bool done = false;
        BE_REFLECT_FIELD PanelProbeMode mode = PanelProbeMode::Idle;
    };

    /// The source object of the binding tests: one field of every kind a panel reads
    struct PanelProbe {
        BE_CLASS(PanelProbe)

        BE_REFLECT_FIELD bool flag = false;
        BE_REFLECT_FIELD int32_t count = 0;
        BE_REFLECT_FIELD uint8_t small = 0;
        BE_REFLECT_FIELD float ratio = 0.0f;
        BE_REFLECT_FIELD double wide = 0.0;
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD PoolString tag;
        BE_REFLECT_FIELD FixedString<16> code;
        BE_REFLECT_FIELD PanelProbeMode mode = PanelProbeMode::Idle;
        BE_REFLECT_FIELD PanelProbeRow inner;
        BE_REFLECT_FIELD Optional<int32_t> maybe;
        BE_REFLECT_FIELD Vector<PanelProbeRow> rows;
        BE_REFLECT_FIELD Vector<int32_t> numbers;
        BE_REFLECT_FIELD Vector<String> names;
        BE_REFLECT_FIELD Array<int32_t, 3> triple{};
    };

    /// Has a field of the name `count` like PanelProbe, of another type: a source whose object changes type at run time
    struct PanelProbeOther {
        BE_CLASS(PanelProbeOther)

        BE_REFLECT_FIELD float count = 0.0f;
    };

}  // namespace BECore::Tests
