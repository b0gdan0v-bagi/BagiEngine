#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Value.h>
#include <cstdint>

namespace BECore::Tests {

    CORE_ENUM(SingleProbeKind, uint8_t, Alpha, Beta)

    struct SingleProbeBase {
        BE_CLASS(SingleProbeBase)

        BE_REFLECT_FIELD int32_t baseValue = 1;
    };

    struct SingleProbeNested {
        BE_CLASS(SingleProbeNested)

        BE_REFLECT_FIELD int32_t first = 1;
        BE_REFLECT_FIELD String label;
    };

    /// A parent and one field of every shape the visit dispatch has hooks for
    struct SingleProbe : SingleProbeBase {
        BE_CLASS(SingleProbe)

        BE_REFLECT_FIELD bool flag = false;
        BE_REFLECT_FIELD int32_t count = 3;
        BE_REFLECT_FIELD String title = "t";
        BE_REFLECT_FIELD PoolString tag;
        BE_REFLECT_FIELD SingleProbeKind kind = SingleProbeKind::Beta;
        BE_REFLECT_FIELD SingleProbeNested nested;
        BE_REFLECT_FIELD Vector<SingleProbeNested> items;
        BE_REFLECT_FIELD Vector<int32_t> numbers;
        BE_REFLECT_FIELD Optional<int32_t> maybe;
        BE_REFLECT_FIELD Variant<int32_t, String> choice;
        BE_REFLECT_FIELD Value<int32_t> amount = 5;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(hidden) int32_t secret = 9;
    };

}  // namespace BECore::Tests
