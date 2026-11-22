#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::StampTestData {

    // =========================================================================
    // Reflected test data of ContentStampTest. The .gen.hpp is generated from this header and included in ContentStampTest.cpp only.
    // The names are unique among all test headers: a type is found by its simple name.
    // =========================================================================

    struct StampRoot {
        BE_CLASS(StampRoot)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD String label;
        BE_REFLECT_FIELD PoolString kind;
        BE_REFLECT_FIELD Vector<int32_t> items;
    };

}  // namespace BECore::StampTestData
