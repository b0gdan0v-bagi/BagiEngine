#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::CookTestData {

    // Reflected test data of ContentCookTest (the `.gen.hpp` is included in the .cpp only). Names are unique among the test headers.

    struct CookProbePart {
        BE_CLASS(CookProbePart)

        BE_REFLECT_FIELD String label;
        BE_REFLECT_FIELD float weight = 0.0f;
    };

    /// A config read as `MakeFromConfig` reads one
    class CookProbe : public RefCounted {
        BE_CLASS(CookProbe)
    public:
        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD PoolString kind;
        BE_REFLECT_FIELD Vector<int32_t> items;
        BE_REFLECT_FIELD Vector<CookProbePart> parts;
        BE_REFLECT_FIELD Optional<int32_t> extra;
    };

}  // namespace BECore::CookTestData
