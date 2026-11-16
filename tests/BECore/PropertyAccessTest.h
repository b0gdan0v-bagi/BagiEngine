#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore::PropertyAccessData {

    // =========================================================================
    // Reflected test data of GetProperty / SetProperty (PropertyAccessTest, CompiledPathTest). The .gen.hpp is generated from this
    // header and included in PropertyAccessTest.cpp only. The names are unique among all test headers: a type is found by its simple name.
    // =========================================================================

    CORE_ENUM(PaShade, uint8_t, Dark, Mid, Light)

    /// The deepest of three nested structs
    struct PaCore {
        BE_CLASS(PaCore)

        BE_REFLECT_FIELD int32_t depth = 3;
        BE_REFLECT_FIELD String label = "core";
    };

    struct PaMiddle {
        BE_CLASS(PaMiddle)

        BE_REFLECT_FIELD int32_t level = 2;
        BE_REFLECT_FIELD PaCore core;
    };

    struct PaTop {
        BE_CLASS(PaTop)

        BE_REFLECT_FIELD int32_t top = 1;
        BE_REFLECT_FIELD PaMiddle middle;
    };

    struct PaItem {
        BE_CLASS(PaItem)

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD String name;
    };

    struct PaNode : RefCounted {
        BE_CLASS(PaNode)

        BE_REFLECT_FIELD int32_t id = 9;
    };

    /// One field of every scalar kind, the three text types and an enum
    struct PaScalars {
        BE_CLASS(PaScalars)

        BE_REFLECT_FIELD bool flag = true;
        BE_REFLECT_FIELD int8_t i8 = -8;
        BE_REFLECT_FIELD int16_t i16 = -16;
        BE_REFLECT_FIELD int32_t i32 = -32;
        BE_REFLECT_FIELD int64_t i64 = -64;
        BE_REFLECT_FIELD uint8_t u8 = 8;
        BE_REFLECT_FIELD uint16_t u16 = 16;
        BE_REFLECT_FIELD uint32_t u32 = 32;
        BE_REFLECT_FIELD uint64_t u64 = 64;
        BE_REFLECT_FIELD float f32 = 0.5f;
        BE_REFLECT_FIELD double f64 = 0.25;
        BE_REFLECT_FIELD String text = "hello";
        BE_REFLECT_FIELD FixedString<4, false> fixedText;
        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD PaShade shade = PaShade::Mid;
    };

    /// Attributes: a range, a read-only and a hidden field, and containers of numbers with a range
    struct PaGuarded {
        BE_CLASS(PaGuarded)

        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1)) float _opacity = 0.5f;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(1, 10)) int32_t _ranged = 5;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly) int32_t _locked = 11;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(hidden) int32_t _secret = 12;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly, hidden) int32_t _sealed = 13;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly) PaCore _frozen;
        BE_REFLECT_FIELD PaCore open;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 100)) Vector<int32_t> _percents;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly) Vector<int32_t> _lockedList;
    };

    /// Every container and wrapper shape
    struct PaContainers {
        BE_CLASS(PaContainers)

        BE_REFLECT_FIELD Vector<PaItem> items;
        BE_REFLECT_FIELD Vector<int32_t> numbers;
        BE_REFLECT_FIELD HashMap<String, int32_t> table;
        BE_REFLECT_FIELD Map<int32_t, String> ordered;
        BE_REFLECT_FIELD Array<int32_t, 2> pairOfInts{};
        BE_REFLECT_FIELD Optional<int32_t> maybe;
        BE_REFLECT_FIELD Optional<PaItem> maybeItem;
        BE_REFLECT_FIELD Variant<int32_t, String> choice;
        BE_REFLECT_FIELD Pair<int32_t, String> entry;
        BE_REFLECT_FIELD IntrusivePtrNonAtomic<PaNode> node;
        BE_REFLECT_FIELD IntrusivePtrNonAtomic<PaNode> emptyNode;
        BE_REFLECT_FIELD Vector<IntrusivePtrNonAtomic<PaNode>> nodes;
    };

    class PaBase : public RefCounted {
        BE_CLASS(PaBase)

    public:
        PaBase() = default;
        ~PaBase() override = default;

        BE_REFLECT_FIELD int32_t baseValue = 1;
    };

    class PaDerived : public PaBase {
        BE_CLASS(PaDerived)

    public:
        PaDerived() = default;
        ~PaDerived() override = default;

        BE_REFLECT_FIELD int32_t ownValue = 2;
        BE_REFLECT_FIELD PaCore core;
    };

    /// Not reflected: it sits before the reflected base of `PaShifted`, so that the base subobject is not at offset 0
    struct PaPad {
        virtual ~PaPad() = default;

        int64_t padding[2]{};
    };

    class PaShifted : public PaPad, public PaBase {
        BE_CLASS(PaShifted)

    public:
        PaShifted() = default;
        ~PaShifted() override = default;

        BE_REFLECT_FIELD int32_t shiftedValue = 3;
    };

    /// A reflected type with no property visitor
    struct PaEvent {
        BE_EVENT(PaEvent)

        BE_REFLECT_FIELD int32_t payload = 0;
    };

}  // namespace BECore::PropertyAccessData
