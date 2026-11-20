#pragma once

#include <BECore/Math/Color.h>
#include <BECore/Math/IVec2.h>
#include <BECore/Math/Quat.h>
#include <BECore/Math/Vec2.h>
#include <BECore/Math/Vec3.h>
#include <BECore/Math/Vec4.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/ValueTestTypes.h>
#include <cstdint>

namespace BECore::ProgramRowData {

    // The reflected context object of the hand-assembled programs of the native set (tests/NativePrograms): its .gen.hpp is included once per executable, by
    // NativeProgramEqualityTest.cpp and by the tool. A type is found by its simple name, so the name is unique among all test headers.

    /// A field of every type a program computes, a second one of the types a `Pack` needs four components of, and an enum field of each width a program loads one with
    struct ProgramRowOperands {
        BE_CLASS(ProgramRowOperands)

        BE_REFLECT_FIELD bool flag = false;
        BE_REFLECT_FIELD int32_t i32 = 0;
        BE_REFLECT_FIELD int32_t i32b = 0;
        BE_REFLECT_FIELD int64_t i64 = 0;
        BE_REFLECT_FIELD float f32 = 0.0f;
        BE_REFLECT_FIELD float f32b = 0.0f;
        BE_REFLECT_FIELD double f64 = 0.0;
        BE_REFLECT_FIELD Vec2 v2;
        BE_REFLECT_FIELD Vec3 v3;
        BE_REFLECT_FIELD Vec4 v4;
        BE_REFLECT_FIELD IVec2 iv2;
        BE_REFLECT_FIELD Quat quat;
        BE_REFLECT_FIELD Color color;
        BE_REFLECT_FIELD PoolString text;
        BE_REFLECT_FIELD ValueTestData::ValueNarrowEnum narrow = ValueTestData::ValueNarrowEnum::Low;
        BE_REFLECT_FIELD ValueTestData::ValueShortEnum wide16 = ValueTestData::ValueShortEnum::Low;
        BE_REFLECT_FIELD ValueTestData::ValueWideEnum wide32 = ValueTestData::ValueWideEnum::Low;
        /// No `CORE_ENUM` has eight bytes; `LoadSlotEnum` reads the field by its size and never asks what the field is
        BE_REFLECT_FIELD uint64_t enum64 = 0;
    };

    /// A level whose `i32` is somewhere else than in `ProgramRowOperands`: a formula compiled over it reads another offset, so it is another program
    struct ProgramRowShifted {
        BE_CLASS(ProgramRowShifted)

        BE_REFLECT_FIELD double padding = 0.0;
        BE_REFLECT_FIELD int32_t i32 = 0;
    };

}  // namespace BECore::ProgramRowData
