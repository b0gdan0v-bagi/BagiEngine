#pragma once

#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore {

    // =========================================================================
    // Reflected test data for the load diagnostics. The .gen.hpp is generated from this header and included in
    // XmlDiagnosticsTest.cpp only.
    // =========================================================================

    namespace XmlDiagnosticsTestData {

        CORE_ENUM(ProbeKind, uint8_t, Small, Large)

        /// A reflected object read as a child element
        struct Leaf {
            BE_CLASS(Leaf)

            BE_REFLECT_FIELD int32_t leafValue = 0;
        };

        /// An enum field and an object field
        struct Shapes {
            BE_CLASS(Shapes)

            BE_REFLECT_FIELD ProbeKind kind = ProbeKind::Small;
            BE_REFLECT_FIELD Leaf leaf;
        };

    }  // namespace XmlDiagnosticsTestData

}  // namespace BECore
