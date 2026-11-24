#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::Tests {

    /// Reflected test data of CommandLineDeserializerTest: one field of each shape a flag can hold
    struct FlagProbe {
        BE_CLASS(FlagProbe)

        BE_REFLECT_FIELD bool _enabled = false;
        BE_REFLECT_FIELD bool _onByDefault = true;
        BE_REFLECT_FIELD uint16_t _port = 7;
        BE_REFLECT_FIELD int32_t _levelCount = -1;
        BE_REFLECT_FIELD float _scale = 1.0f;
        BE_REFLECT_FIELD String _label = "keep";
        BE_REFLECT_FIELD PoolString _title;
    };

    struct FlagProbeInner {
        BE_CLASS(FlagProbeInner)

        BE_REFLECT_FIELD int32_t _depth = 0;
    };

    /// A struct among the fields: a flag holds one value, so it can never be read
    struct FlagProbeNested {
        BE_CLASS(FlagProbeNested)

        BE_REFLECT_FIELD int32_t _top = 0;
        BE_REFLECT_FIELD FlagProbeInner _inner;
        BE_REFLECT_FIELD Vector<int32_t> _list;
    };

}  // namespace BECore::Tests
