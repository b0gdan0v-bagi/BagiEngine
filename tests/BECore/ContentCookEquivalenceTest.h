#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Value.h>
#include <cstdint>

namespace BECore::CookEquivalenceData {

    // Reflected test data of ContentCookEquivalenceTest (the `.gen.hpp` is included in the .cpp only). Names are unique among the test headers.

    /// The object a `from` of a document reads
    struct EquivStats {
        BE_CLASS(EquivStats)

        BE_REFLECT_FIELD int32_t attack = 12;
        BE_REFLECT_FIELD float speed = 1.5f;
    };

    class EquivShape : public RefCounted {
        BE_CLASS(EquivShape, FACTORY_BASE, ELEMENT = shape)
    public:
        EquivShape() = default;
        ~EquivShape() override = default;
    };

    class EquivCircle : public EquivShape {
        BE_CLASS(EquivCircle)
    public:
        BE_REFLECT_FIELD int32_t radius = 1;
    };

    /// A derived type with fields the base and its sibling have not
    class EquivBox : public EquivShape {
        BE_CLASS(EquivBox)
    public:
        BE_REFLECT_FIELD float width = 0.0f;
        BE_REFLECT_FIELD float height = 0.0f;
        BE_REFLECT_FIELD String label;
    };

    /// One field of each shape the equivalence of a cooked file has to hold for
    class EquivDocument : public RefCounted {
        BE_CLASS(EquivDocument)
    public:
        BE_REFLECT_FIELD ContextDecl context;
        BE_REFLECT_FIELD Value<int32_t> damage = 7;
        BE_REFLECT_FIELD Value<float> power = 1.0f;
        BE_REFLECT_FIELD PoolString kind;
        BE_REFLECT_FIELD Optional<int32_t> maybe;
        BE_REFLECT_FIELD int32_t count = 0;
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD Vector<IntrusivePtrNonAtomic<EquivShape>> shapes;
    };

}  // namespace BECore::CookEquivalenceData
