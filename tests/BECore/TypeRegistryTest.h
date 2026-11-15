#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore::RegistryTestData {

    // =========================================================================
    // Reflected test data of the type registry (TypeDescTest, TypeRegistryTest). The .gen.hpp is generated from this header
    // and included in TypeRegistryTest.cpp only. The names are unique among all test headers: a type is found by its simple name.
    // =========================================================================

    CORE_ENUM(RegMode, uint8_t, Off, On, Auto)

    /// A reflected object held by value
    struct RegInner {
        BE_CLASS(RegInner)

        BE_REFLECT_FIELD int32_t value = 0;
        BE_REFLECT_FIELD String label;
    };

    /// A reflected object held through a pointer
    struct RegNode : RefCounted {
        BE_CLASS(RegNode)

        BE_REFLECT_FIELD int32_t id = 0;
    };

    /// One field of every kind a field can have
    struct RegAllKinds {
        BE_CLASS(RegAllKinds)

        BE_REFLECT_FIELD bool flag = false;
        BE_REFLECT_FIELD int8_t tiny = 0;
        BE_REFLECT_FIELD int16_t word = 0;
        BE_REFLECT_FIELD int32_t count = 0;
        BE_REFLECT_FIELD int64_t big = 0;
        BE_REFLECT_FIELD uint8_t utiny = 0;
        BE_REFLECT_FIELD uint16_t uword = 0;
        BE_REFLECT_FIELD uint32_t ucount = 0;
        BE_REFLECT_FIELD uint64_t ubig = 0;
        BE_REFLECT_FIELD float ratio = 0.0f;
        BE_REFLECT_FIELD double precise = 0.0;
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD FixedString<8, false> fixedText;
        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD RegMode mode = RegMode::Off;
        BE_REFLECT_FIELD RegInner inner;
        BE_REFLECT_FIELD Vector<int32_t> items;
        BE_REFLECT_FIELD SmallVector<int32_t, 4> smallItems;
        BE_REFLECT_FIELD FixedVector<int32_t, 3> fixedItems;
        BE_REFLECT_FIELD Array<int32_t, 2> pairOfInts{};
        BE_REFLECT_FIELD HashMap<String, int32_t> table;
        BE_REFLECT_FIELD FixedHashMap<String, int32_t, 4, 5, false> fixedTable;
        BE_REFLECT_FIELD Map<int32_t, String> ordered;
        BE_REFLECT_FIELD Optional<int32_t> maybe;
        BE_REFLECT_FIELD Pair<int32_t, String> entry;
        BE_REFLECT_FIELD Variant<int32_t, String> choice;
        BE_REFLECT_FIELD IntrusivePtrNonAtomic<RegNode> node;
        BE_REFLECT_FIELD Vector<RegInner> inners;
    };

    /// A base with a vtable and a field
    class RegBase : public RefCounted {
        BE_CLASS(RegBase)

    public:
        RegBase() = default;
        ~RegBase() override = default;

        BE_REFLECT_FIELD int32_t baseValue = 1;
    };

    class RegDerived : public RegBase {
        BE_CLASS(RegDerived)

    public:
        RegDerived() = default;
        ~RegDerived() override = default;

        BE_REFLECT_FIELD int32_t ownValue = 2;
        BE_REFLECT_FIELD RegInner nested;
    };

    /// Not reflected: it sits before the reflected base in `RegShifted`, so that the base subobject is not at offset 0
    struct RegPad {
        virtual ~RegPad() = default;

        int64_t padding[2]{};
    };

    class RegShifted : public RegPad, public RegBase {
        BE_CLASS(RegShifted)

    public:
        RegShifted() = default;
        ~RegShifted() override = default;

        BE_REFLECT_FIELD int32_t shiftedValue = 3;
    };

    /// A field of each kind of attributes
    struct RegAttributed {
        BE_CLASS(RegAttributed)

        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1), step(0.05), tooltip("Share of the screen")) float _opacity = 0.5f;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly, hidden, required) bool _flags = true;
        BE_REFLECT_FIELD int32_t plain = 7;
    };

    /// Methods that can be called by name and methods that cannot
    struct RegMethods {
        BE_CLASS(RegMethods)

        BE_REFLECT_FIELD int32_t total = 0;

        BE_FUNCTION int32_t Add(int32_t amount, const String& label) {
            total += amount + static_cast<int32_t>(label.size());
            return total;
        }

        BE_FUNCTION void Reset() {
            total = 0;
        }

        BE_FUNCTION bool IsEmpty() const {
            return total == 0;
        }

        BE_FUNCTION bool Initialize(IDeserializer& deserializer);

        BE_FUNCTION void Fill(Vector<int32_t>& out) {
            out.push_back(total);
        }

        BE_FUNCTION RegNode* Find(int32_t id) const;
    };

    /// A BE_EVENT is a reflected type with no vtable and no visitor
    struct RegEvent {
        BE_EVENT(RegEvent)

        BE_REFLECT_FIELD int32_t payload = 0;
    };

}  // namespace BECore::RegistryTestData
