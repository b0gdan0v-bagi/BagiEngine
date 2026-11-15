#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::Tests {

    namespace AssetPurposeTestTypes {

        /// One reflected type of each purpose; shared by the purpose, filter and asset part tests.
        struct PurposeSimProbe {
            BE_CLASS(PurposeSimProbe, PURPOSE = sim)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        struct PurposeVisualProbe {
            BE_CLASS(PurposeVisualProbe, purpose = Visual)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        struct PurposeEditorProbe {
            BE_CLASS(PurposeEditorProbe, PURPOSE = editor)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        struct PurposeCommonProbe {
            BE_CLASS(PurposeCommonProbe)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        /// Factory base of the filter tests: `Common`, so a loader of any part may hold a pointer to it
        class PurposeAssetBase : public RefCounted {
            BE_CLASS(PurposeAssetBase, FACTORY_BASE, ELEMENT = element)
        public:
            PurposeAssetBase() = default;
            ~PurposeAssetBase() override = default;
        };

        /// Each derived class counts its constructions: a refused element must never be created
        class PurposeSimDerived : public PurposeAssetBase {
            BE_CLASS(PurposeSimDerived, PURPOSE = sim)
        public:
            PurposeSimDerived() {
                ++constructed;
            }
            ~PurposeSimDerived() override = default;

            BE_REFLECT_FIELD int32_t value = 0;

            static inline int32_t constructed = 0;
        };

        class PurposeVisualDerived : public PurposeAssetBase {
            BE_CLASS(PurposeVisualDerived, PURPOSE = visual)
        public:
            PurposeVisualDerived() {
                ++constructed;
            }
            ~PurposeVisualDerived() override = default;

            BE_REFLECT_FIELD int32_t value = 0;

            static inline int32_t constructed = 0;
        };

        /// The generator compares the names of reflected classes in a field's type, so an alias hides a visual field from
        /// it the way a factory-created element is hidden: only the load-time filter can catch both.
        using HiddenVisualProbe = PurposeVisualProbe;

        /// A sim holder of the load-time filter cases
        struct PurposeSimHolder {
            BE_CLASS(PurposeSimHolder, PURPOSE = sim)

            BE_REFLECT_FIELD PurposeSimProbe simPart;
            BE_REFLECT_FIELD HiddenVisualProbe visualPart;
            BE_REFLECT_FIELD PurposeCommonProbe commonPart;
            BE_REFLECT_FIELD Vector<IntrusivePtrNonAtomic<PurposeAssetBase>> elements;
            BE_REFLECT_FIELD IntrusivePtrNonAtomic<PurposeAssetBase> single;
        };

        struct PurposeCommonHolder {
            BE_CLASS(PurposeCommonHolder)

            BE_REFLECT_FIELD PurposeCommonProbe commonPart;
        };

    }  // namespace AssetPurposeTestTypes

}  // namespace BECore::Tests
