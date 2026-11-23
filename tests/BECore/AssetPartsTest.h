#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::Tests {

    namespace AssetPartsTestTypes {

        /// One reflected type of each purpose. They live apart from AssetPurposeTest.h: a test that loads a type needs its
        /// generated traits, and a `.gen.hpp` is included from one `.cpp` only.
        struct PartSimProbe {
            BE_CLASS(PartSimProbe, PURPOSE = sim)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        struct PartVisualProbe {
            BE_CLASS(PartVisualProbe, PURPOSE = visual)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        struct PartEditorProbe {
            BE_CLASS(PartEditorProbe, PURPOSE = editor)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        struct PartCommonProbe {
            BE_CLASS(PartCommonProbe)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        /// `Common` factory base: a loader of any part may hold a pointer to it
        class PartAssetBase : public RefCounted {
            BE_CLASS(PartAssetBase, FACTORY_BASE, ELEMENT = element)
        public:
            PartAssetBase() = default;
            ~PartAssetBase() override = default;
        };

        /// Counts its constructions: an element of another part must never be created
        class PartVisualDerived : public PartAssetBase {
            BE_CLASS(PartVisualDerived, PURPOSE = visual)
        public:
            PartVisualDerived() {
                ++constructed;
            }
            ~PartVisualDerived() override = default;

            BE_REFLECT_FIELD int32_t value = 0;

            static inline int32_t constructed = 0;
        };

        /// The generator compares the names of reflected classes in a field's type, so an alias hides a visual field from it
        /// the way a factory-created element is hidden: only the load-time filter catches both.
        using HiddenVisualProbe = PartVisualProbe;

        struct PartSimHolder {
            BE_CLASS(PartSimHolder, PURPOSE = sim)

            BE_REFLECT_FIELD PartSimProbe simPart;
            BE_REFLECT_FIELD HiddenVisualProbe visualPart;
            BE_REFLECT_FIELD Vector<IntrusivePtr<PartAssetBase>> elements;
        };

    }  // namespace AssetPartsTestTypes

}  // namespace BECore::Tests
