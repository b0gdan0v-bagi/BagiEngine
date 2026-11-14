#pragma once

#include <BECore/Reflection/IPropertyVisitor.h>
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

        /// One field of each kind the `required` and `range` checks look at
        struct Constrained {
            BE_CLASS(Constrained)

            BE_REFLECT_FIELD BE_FIELD_ATTRS(required) int32_t needed = 0;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1)) float ratio = 0.5f;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(1, 10)) int32_t count = 5;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(required, range(0, 100)) int32_t both = 50;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(required) Leaf requiredLeaf;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(required) Vector<Leaf> requiredLeaves;
            BE_REFLECT_FIELD int32_t free = 0;
        };

        struct ConstrainedChild : Constrained {
            BE_CLASS(ConstrainedChild)

            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 3)) int32_t own = 0;
        };

    }  // namespace XmlDiagnosticsTestData

    namespace PropVisit {

        /// The visitor edits no list of objects; the generated `AcceptPropertyVisitor` of `Constrained` needs something to call
        inline bool Visit(IPropertyVisitor& /*v*/, Vector<XmlDiagnosticsTestData::Leaf>& /*value*/, StringView /*key*/) {
            return false;
        }

    }  // namespace PropVisit

}  // namespace BECore
