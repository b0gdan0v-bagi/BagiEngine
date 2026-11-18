#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::Tests {

    /// Reflected type that the ConfigDeserializer tests write to a file and load back through OpenDeserializer / LoadSerialized
    struct DeserializerProbe {
        BE_CLASS(DeserializerProbe)

        BE_REFLECT_FIELD int32_t width = 1;
        BE_REFLECT_FIELD PoolString title;
    };

    /// A list of probes: the array a config file holds as `<items>` with a child per element
    struct DeserializerProbeList {
        BE_CLASS(DeserializerProbeList)

        BE_REFLECT_FIELD Vector<DeserializerProbe> items;
    };

}  // namespace BECore::Tests
