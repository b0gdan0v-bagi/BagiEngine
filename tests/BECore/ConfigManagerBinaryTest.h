#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::BinaryConfigTestData {

    // =========================================================================
    // Reflected test data of ConfigManagerBinaryTest. The .gen.hpp is generated from this header and included in the .cpp only.
    // The names are unique among all test headers: a type is found by its simple name.
    // =========================================================================

    struct BinaryConfigPart {
        BE_CLASS(BinaryConfigPart)

        BE_REFLECT_FIELD String label;
        BE_REFLECT_FIELD float weight = 0.0f;
    };

    struct BinaryConfigRoot {
        BE_CLASS(BinaryConfigRoot)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD PoolString kind;
        BE_REFLECT_FIELD Vector<int32_t> items;
        BE_REFLECT_FIELD BinaryConfigPart part;
        BE_REFLECT_FIELD Vector<BinaryConfigPart> parts;
        BE_REFLECT_FIELD Optional<int32_t> extra;
    };

}  // namespace BECore::BinaryConfigTestData
