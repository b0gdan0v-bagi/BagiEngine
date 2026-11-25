#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore::Tests {

    CORE_ENUM(PanelProbeMode, uint8_t, Idle, Busy, Failed)

    /// An enum four bytes wide: the writer stores as many bytes as the field has
    CORE_ENUM(PanelProbeLevel, uint32_t, Low, Mid, High, Max)

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

    /// A cell of the writer tests: embedded in `PanelProbeNumbers` and a row of its list
    struct PanelProbeCells {
        BE_CLASS(PanelProbeCells)

        BE_REFLECT_FIELD int16_t before = 0;
        BE_REFLECT_FIELD bool flag = false;
        BE_REFLECT_FIELD uint8_t small = 0;
        BE_REFLECT_FIELD float ratio = 0.0f;
        BE_REFLECT_FIELD PanelProbeLevel level = PanelProbeLevel::Low;
        BE_REFLECT_FIELD int16_t after = 0;
    };

    /// The source object of the writer tests: every integer width, both floats, a bool and enums of 1 and 4 bytes, each between
    /// neighbours a wrong width would overwrite
    struct PanelProbeNumbers {
        BE_CLASS(PanelProbeNumbers)

        BE_REFLECT_FIELD int8_t i8 = 0;
        BE_REFLECT_FIELD uint8_t u8 = 0;
        BE_REFLECT_FIELD int16_t i16 = 0;
        BE_REFLECT_FIELD uint16_t u16 = 0;
        BE_REFLECT_FIELD int32_t i32 = 0;
        BE_REFLECT_FIELD uint32_t u32 = 0;
        BE_REFLECT_FIELD int64_t i64 = 0;
        BE_REFLECT_FIELD uint64_t u64 = 0;
        BE_REFLECT_FIELD float f32 = 0.0f;
        BE_REFLECT_FIELD double f64 = 0.0;
        BE_REFLECT_FIELD bool flag = false;
        BE_REFLECT_FIELD PanelProbeMode mode = PanelProbeMode::Idle;
        BE_REFLECT_FIELD PanelProbeLevel level = PanelProbeLevel::Low;
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD PanelProbeCells inner;
        BE_REFLECT_FIELD Vector<PanelProbeCells> rows;
        BE_REFLECT_FIELD Vector<int32_t> numbers;
        BE_REFLECT_FIELD Optional<int32_t> maybe;
    };

    /// Has a field of the name `count` like PanelProbe, of another type: a source whose object changes type at run time
    struct PanelProbeOther {
        BE_CLASS(PanelProbeOther)

        BE_REFLECT_FIELD float count = 0.0f;
    };

}  // namespace BECore::Tests
