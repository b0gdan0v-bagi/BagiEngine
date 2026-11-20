#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::Tests {

    /// An element of the arrays below: an object, so each element opens a scope of its own
    struct ReserveElement {
        BE_CLASS(ReserveElement)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD float weight = 0.0f;
    };

    struct ReserveHolder {
        BE_CLASS(ReserveHolder)

        BE_REFLECT_FIELD Vector<int32_t> numbers;
        BE_REFLECT_FIELD Vector<ReserveElement> elements;
    };

    /// Room for three; a longer array keeps three and reports the rest
    struct ReserveFixedHolder {
        BE_CLASS(ReserveFixedHolder)

        BE_REFLECT_FIELD FixedVector<int32_t, 3> items;
    };

    /// Overflow allowed: reserves past the inline capacity
    struct ReserveSmallHolder {
        BE_CLASS(ReserveSmallHolder)

        BE_REFLECT_FIELD SmallVector<int32_t, 3> items;
    };

}  // namespace BECore::Tests
