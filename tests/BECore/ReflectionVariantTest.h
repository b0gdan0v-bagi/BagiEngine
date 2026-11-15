#pragma once

#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>

namespace BECore {

    // =========================================================================
    // Reflected test data for the Variant accessor. The .gen.hpp is generated from this header and included in
    // ReflectionVariantTest.cpp only. No struct here has a Variant field: the tests call Ser::Save / Ser::Load on a variant directly.
    // =========================================================================

    namespace ReflectionVariantTestData {

        /// An alternative saved as a child object
        struct Point {
            BE_CLASS(Point)

            BE_REFLECT_FIELD int32_t x = 0;
            BE_REFLECT_FIELD int32_t y = 0;

            bool operator==(const Point& other) const = default;
        };

    }  // namespace ReflectionVariantTestData

}  // namespace BECore
