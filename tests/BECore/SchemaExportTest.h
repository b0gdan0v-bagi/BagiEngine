#pragma once

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

namespace BECore {
    class IDeserializer;
}

namespace BECore::SchemaTestData {

    // =========================================================================
    // Reflected test data of SchemaExportTest. The .gen.hpp is generated from this header and included in SchemaExportTest.cpp only.
    // The names are unique among all test headers: a type is found by its simple name.
    // =========================================================================

    CORE_ENUM(SchemaMode, uint8_t, Off, On, Auto)

    struct SchemaLeaf {
        BE_CLASS(SchemaLeaf)

        BE_REFLECT_FIELD int32_t id = 0;
    };

    struct SchemaNode : RefCounted {
        BE_CLASS(SchemaNode)

        BE_REFLECT_FIELD int32_t id = 0;
    };

    /// A factory base, so the element name of its list is `shape`
    class SchemaShape : public RefCounted {
        BE_CLASS(SchemaShape, FACTORY_BASE, ELEMENT = shape)
    public:
        SchemaShape() = default;
        ~SchemaShape() override = default;

        virtual int32_t Sides() const = 0;
    };

    class SchemaCircle : public SchemaShape {
        BE_CLASS(SchemaCircle, ICON = circle)
    public:
        SchemaCircle() = default;
        ~SchemaCircle() override = default;

        int32_t Sides() const override {
            return 0;
        }

        BE_REFLECT_FIELD float radius = 1.0f;
    };

    class SchemaDisc : public SchemaCircle {
        BE_CLASS(SchemaDisc)
    public:
        SchemaDisc() = default;
        ~SchemaDisc() override = default;

        BE_REFLECT_FIELD bool filled = true;
    };

    class SchemaSquare : public SchemaShape {
        BE_CLASS(SchemaSquare, PURPOSE = visual)
    public:
        SchemaSquare() = default;
        ~SchemaSquare() override = default;

        int32_t Sides() const override {
            return 4;
        }

        BE_REFLECT_FIELD float side = 1.0f;
    };

    /// One field of every shape a field can have
    struct SchemaAllShapes {
        BE_CLASS(SchemaAllShapes)

        BE_REFLECT_FIELD bool flag = false;
        BE_REFLECT_FIELD int32_t count = 0;
        BE_REFLECT_FIELD uint64_t big = 0;
        BE_REFLECT_FIELD double precise = 0.0;
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD FixedString<8, false> fixedText;
        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD SchemaMode mode = SchemaMode::Off;
        BE_REFLECT_FIELD SchemaLeaf leaf;
        BE_REFLECT_FIELD IntrusivePtrNonAtomic<SchemaNode> node;
        BE_REFLECT_FIELD Vector<int32_t> items;
        BE_REFLECT_FIELD FixedVector<int32_t, 3> fixedItems;
        BE_REFLECT_FIELD Map<int32_t, String> ordered;
        BE_REFLECT_FIELD Optional<int32_t> maybe;
        BE_REFLECT_FIELD Pair<int32_t, String> entry;
        BE_REFLECT_FIELD Variant<int32_t, String> choice;
        BE_REFLECT_FIELD Vector<SchemaLeaf> leaves;
        BE_REFLECT_FIELD Vector<IntrusivePtrNonAtomic<SchemaShape>> shapes;
    };

    /// A field of each kind of attribute, and one with none
    struct SchemaAttributed {
        BE_CLASS(SchemaAttributed)

        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1), step(0.05), tooltip("Share of the screen")) float _opacity = 0.5f;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly, hidden, required) bool _flags = true;
        BE_REFLECT_FIELD int32_t plain = 7;
    };

    /// The fields whose type is a `Value<T>`: the schema names them as such, with their `T`
    struct SchemaValueFields {
        BE_CLASS(SchemaValueFields)

        BE_REFLECT_FIELD Value<int32_t> damage;
        BE_REFLECT_FIELD Value<bool> alive;
        BE_REFLECT_FIELD Value<Vec3> position;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 100)) Value<int32_t> percent;
    };

    /// A method that can be called by name and some that cannot
    struct SchemaMethods {
        BE_CLASS(SchemaMethods)

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

        BE_FUNCTION SchemaNode* Find(int32_t id) const;
    };

}  // namespace BECore::SchemaTestData
