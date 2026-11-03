#pragma once

#include <BECore/Reflection/TypeTraits.h>

namespace BECore::Tests {

    /// Reflected type that the ConfigDeserializer tests write to a file and load back through OpenDeserializer / LoadSerialized
    struct DeserializerProbe {
        BE_CLASS(DeserializerProbe)

        BE_REFLECT_FIELD int32_t width = 1;
        BE_REFLECT_FIELD PoolString title;
    };

}  // namespace BECore::Tests
