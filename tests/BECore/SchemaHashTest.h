#pragma once

#include <BECore/Math/Vec2.h>
#include <BECore/Math/Vec3.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Value.h>
#include <cstdint>

namespace BECore::HashTestData {

    // =========================================================================
    // Reflected test data of SchemaHashTest. The .gen.hpp is generated from this header and included in SchemaHashTest.cpp only.
    // The names are unique among all test headers: a type is found by its simple name. Each type differs from `HshBase` (or from
    // its named twin) in one respect; the root's own name is not part of the hash, so a twin of the same layout is the same hash.
    // =========================================================================

    CORE_ENUM(HshModeA, uint8_t, Off, On)
    CORE_ENUM(HshModeRenamed, uint8_t, Off, Auto)
    CORE_ENUM(HshModeAdded, uint8_t, Off, On, Auto)
    CORE_ENUM(HshModeWide, uint16_t, Off, On)

    struct HshBase {
        BE_CLASS(HshBase)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD String label;
        BE_REFLECT_FIELD float ratio = 0.0f;
    };

    struct HshTwin {
        BE_CLASS(HshTwin)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD String label;
        BE_REFLECT_FIELD float ratio = 0.0f;
    };

    /// `HshBase` with the attributes a field can have: none of them is in a file
    struct HshAttributed {
        BE_CLASS(HshAttributed)

        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 100), tooltip("Not in the hash"), hidden) int32_t id = 0;
        BE_REFLECT_FIELD String label;
        BE_REFLECT_FIELD float ratio = 0.0f;
    };

    struct HshFieldRenamed {
        BE_CLASS(HshFieldRenamed)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD float ratio = 0.0f;
    };

    struct HshSwapped {
        BE_CLASS(HshSwapped)

        BE_REFLECT_FIELD String label;
        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD float ratio = 0.0f;
    };

    struct HshWiderId {
        BE_CLASS(HshWiderId)

        BE_REFLECT_FIELD int64_t id = 0;
        BE_REFLECT_FIELD String label;
        BE_REFLECT_FIELD float ratio = 0.0f;
    };

    struct HshFieldAdded {
        BE_CLASS(HshFieldAdded)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD String label;
        BE_REFLECT_FIELD float ratio = 0.0f;
        BE_REFLECT_FIELD bool flag = false;
    };

    struct HshPooled {
        BE_CLASS(HshPooled)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD PoolString label;
        BE_REFLECT_FIELD float ratio = 0.0f;
    };

    // The containers: one field each, so a difference of the type is the only one

    struct HshVectorField {
        BE_CLASS(HshVectorField)

        BE_REFLECT_FIELD Vector<int32_t> items;
    };

    struct HshArrayField {
        BE_CLASS(HshArrayField)

        BE_REFLECT_FIELD Array<int32_t, 2> items{};
    };

    struct HshFixedThree {
        BE_CLASS(HshFixedThree)

        BE_REFLECT_FIELD FixedVector<int32_t, 3> items;
    };

    struct HshFixedFour {
        BE_CLASS(HshFixedFour)

        BE_REFLECT_FIELD FixedVector<int32_t, 4> items;
    };

    struct HshInt64Vector {
        BE_CLASS(HshInt64Vector)

        BE_REFLECT_FIELD Vector<int64_t> items;
    };

    struct HshMapField {
        BE_CLASS(HshMapField)

        BE_REFLECT_FIELD Map<int32_t, String> items;
    };

    struct HshMapOfInt64 {
        BE_CLASS(HshMapOfInt64)

        BE_REFLECT_FIELD Map<int64_t, String> items;
    };

    struct HshOptionalField {
        BE_CLASS(HshOptionalField)

        BE_REFLECT_FIELD Optional<int32_t> items;
    };

    struct HshVariantField {
        BE_CLASS(HshVariantField)

        BE_REFLECT_FIELD Variant<int32_t, String> items;
    };

    struct HshVariantOther {
        BE_CLASS(HshVariantOther)

        BE_REFLECT_FIELD Variant<int32_t, float> items;
    };

    struct HshModeField {
        BE_CLASS(HshModeField)

        BE_REFLECT_FIELD HshModeA mode = HshModeA::Off;
    };

    struct HshModeRenamedField {
        BE_CLASS(HshModeRenamedField)

        BE_REFLECT_FIELD HshModeRenamed mode = HshModeRenamed::Off;
    };

    struct HshModeAddedField {
        BE_CLASS(HshModeAddedField)

        BE_REFLECT_FIELD HshModeAdded mode = HshModeAdded::Off;
    };

    struct HshModeWideField {
        BE_CLASS(HshModeWideField)

        BE_REFLECT_FIELD HshModeWide mode = HshModeWide::Off;
    };

    struct HshVec2Field {
        BE_CLASS(HshVec2Field)

        BE_REFLECT_FIELD Vec2 position;
    };

    struct HshVec3Field {
        BE_CLASS(HshVec3Field)

        BE_REFLECT_FIELD Vec3 position;
    };

    struct HshValueInt {
        BE_CLASS(HshValueInt)

        BE_REFLECT_FIELD Value<int32_t> amount;
    };

    struct HshValueFloat {
        BE_CLASS(HshValueFloat)

        BE_REFLECT_FIELD Value<float> amount;
    };

    // Nested objects and inheritance

    struct HshInner {
        BE_CLASS(HshInner)

        BE_REFLECT_FIELD int32_t value = 0;
    };

    struct HshInnerWider {
        BE_CLASS(HshInnerWider)

        BE_REFLECT_FIELD int32_t value = 0;
        BE_REFLECT_FIELD int32_t extra = 0;
    };

    struct HshHolderInner {
        BE_CLASS(HshHolderInner)

        BE_REFLECT_FIELD HshInner inner;
    };

    struct HshHolderInnerWider {
        BE_CLASS(HshHolderInnerWider)

        BE_REFLECT_FIELD HshInnerWider inner;
    };

    struct HshHolderInners {
        BE_CLASS(HshHolderInners)

        BE_REFLECT_FIELD Vector<HshInner> inners;
    };

    class HshParent : public RefCounted {
        BE_CLASS(HshParent)

    public:
        HshParent() = default;
        ~HshParent() override = default;

        BE_REFLECT_FIELD int32_t parentValue = 0;
    };

    class HshParentWider : public RefCounted {
        BE_CLASS(HshParentWider)

    public:
        HshParentWider() = default;
        ~HshParentWider() override = default;

        BE_REFLECT_FIELD int32_t parentValue = 0;
        BE_REFLECT_FIELD int32_t parentExtra = 0;
    };

    class HshChild : public HshParent {
        BE_CLASS(HshChild)

    public:
        HshChild() = default;
        ~HshChild() override = default;

        BE_REFLECT_FIELD int32_t childValue = 0;
    };

    class HshChildOfWider : public HshParentWider {
        BE_CLASS(HshChildOfWider)

    public:
        HshChildOfWider() = default;
        ~HshChildOfWider() override = default;

        BE_REFLECT_FIELD int32_t childValue = 0;
    };

    /// The fields of `HshChild` in one type
    struct HshFlatChild {
        BE_CLASS(HshFlatChild)

        BE_REFLECT_FIELD int32_t parentValue = 0;
        BE_REFLECT_FIELD int32_t childValue = 0;
    };

    /// Recursive through a pointer
    class HshTree : public RefCounted {
        BE_CLASS(HshTree)

    public:
        HshTree() = default;
        ~HshTree() override = default;

        BE_REFLECT_FIELD int32_t value = 0;
        BE_REFLECT_FIELD Vector<IntrusivePtr<HshTree>> children;
    };

    // A factory base, the types of it and a list that holds them

    class HshShape : public RefCounted {
        BE_CLASS(HshShape, FACTORY_BASE, ELEMENT = hshape)

    public:
        HshShape() = default;
        ~HshShape() override = default;

        virtual int32_t Sides() const = 0;

        BE_REFLECT_FIELD int32_t color = 0;
    };

    class HshCircle : public HshShape {
        BE_CLASS(HshCircle)

    public:
        HshCircle() = default;
        ~HshCircle() override = default;

        int32_t Sides() const override {
            return 0;
        }

        BE_REFLECT_FIELD float radius = 1.0f;
    };

    /// The fields `HshCircle` would have with one more; the test gives them to a type named `HshCircle`
    class HshCircleWider : public HshShape {
        BE_CLASS(HshCircleWider)

    public:
        HshCircleWider() = default;
        ~HshCircleWider() override = default;

        int32_t Sides() const override {
            return 0;
        }

        BE_REFLECT_FIELD float radius = 1.0f;
        BE_REFLECT_FIELD bool filled = false;
    };

    class HshSquare : public HshShape {
        BE_CLASS(HshSquare)

    public:
        HshSquare() = default;
        ~HshSquare() override = default;

        int32_t Sides() const override {
            return 4;
        }

        BE_REFLECT_FIELD float side = 1.0f;
    };

    struct HshShapeList {
        BE_CLASS(HshShapeList)

        BE_REFLECT_FIELD Vector<IntrusivePtr<HshShape>> shapes;
    };

}  // namespace BECore::HashTestData
