#pragma once

#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore {

    // =========================================================================
    // Reflected test data for the containers of the property visitor. The .gen.hpp is generated from this header and included in
    // PropertyVisitorContainersTest.cpp only.
    // =========================================================================

    namespace VisitorContainersTestData {

        /// A key of a map whose label is the enum's name
        CORE_ENUM(Shade, uint8_t, Light, Dark)

        /// An element that is a reflected object
        struct ContainerItem {
            BE_CLASS(ContainerItem)

            BE_REFLECT_FIELD int32_t id = 0;
        };

        /// One field of each container the visitor edits, and the attributes a container field passes to its elements
        struct ContainerFields {
            BE_CLASS(ContainerFields)

            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 10), step(0.5)) Vector<float> numbers;
            BE_REFLECT_FIELD Vector<ContainerItem> items;
            BE_REFLECT_FIELD Map<String, float> weights;
            BE_REFLECT_FIELD Map<int32_t, String> names;
            BE_REFLECT_FIELD HashMap<String, int32_t> hashed;
            BE_REFLECT_FIELD Array<int32_t, 2> twin{};
            BE_REFLECT_FIELD SmallVector<int32_t, 2> small;
            BE_REFLECT_FIELD FixedVector<int32_t, 2> fixed;
            BE_REFLECT_FIELD Pair<int32_t, String> entry;
            BE_REFLECT_FIELD Optional<int32_t> maybe;
            BE_REFLECT_FIELD Variant<std::monostate, int32_t, ContainerItem> choice;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly, tooltip("Fixed by the level")) Vector<int32_t> locked;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(hidden) Vector<int32_t> secret;
        };

    }  // namespace VisitorContainersTestData

}  // namespace BECore
