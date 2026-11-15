#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore::ReflectionControlData {

    // =========================================================================
    // Reflected test data of the by-name control methods (ReflectionControlTest). The .gen.hpp is generated from this header and
    // included in ReflectionControlTest.cpp only. The names are unique among all test headers: a type is found by its simple name.
    // =========================================================================

    CORE_ENUM(RcMode, uint8_t, Off, Mid, On)

    struct RcLeaf {
        BE_CLASS(RcLeaf)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD String label;
    };

    /// One field of each kind a client can read or write, and the guarded ones
    struct RcBox {
        BE_CLASS(RcBox)

        BE_REFLECT_FIELD bool flag = false;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 10)) int32_t _count = 3;
        BE_REFLECT_FIELD int64_t big = 0;
        BE_REFLECT_FIELD uint64_t huge = 0;
        BE_REFLECT_FIELD float ratio = 0.5f;
        BE_REFLECT_FIELD double wide = 0.25;
        BE_REFLECT_FIELD String text = "text";
        BE_REFLECT_FIELD PoolString tag;
        BE_REFLECT_FIELD RcMode mode = RcMode::Mid;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly) int32_t _locked = 7;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(hidden) int32_t _secret = 8;
        BE_REFLECT_FIELD RcLeaf leaf;
        BE_REFLECT_FIELD Vector<int32_t> numbers;
    };

}  // namespace BECore::ReflectionControlData
