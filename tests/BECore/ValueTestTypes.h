#pragma once

#include <BECore/Math/Color.h>
#include <BECore/Math/IVec2.h>
#include <BECore/Math/Quat.h>
#include <BECore/Math/Rect.h>
#include <BECore/Math/Vec2.h>
#include <BECore/Math/Vec3.h>
#include <BECore/Math/Vec4.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Value.h>
#include <cstdint>

namespace BECore::ValueTestData {

    // =========================================================================
    // Reflected test data of the values and their context (ValueContextTest, ValueTest, ValueAccessorTest, ValueMatrixTest). The
    // .gen.hpp is generated from this header and included in ValueContextTest.cpp only; the other tests find the types by name. The
    // names are unique among all test headers: a type is found by its simple name.
    // =========================================================================

    CORE_ENUM(ValueElement, uint8_t, Fire, Water, Earth)

    /// A game's own value type: it saves itself and has no `BE_CLASS`, so the reflection sees it as opaque
    struct ValuePoint {
        int32_t column = 0;
        int32_t row = 0;

        [[nodiscard]] bool operator==(const ValuePoint&) const = default;

        template <typename Serializer>
        void Serialize(Serializer& s) const {
            s.WriteAttribute("column", column);
            s.WriteAttribute("row", row);
        }

        template <typename Deserializer>
        void Deserialize(Deserializer& d) {
            d.ReadAttribute("column", column);
            d.ReadAttribute("row", row);
        }

        template <typename Visitor>
        bool VisitProperties(Visitor& v) {
            bool changed = false;
            changed |= v.Visit("column", column);
            changed |= v.Visit("row", row);
            return changed;
        }
    };

    /// A struct held by value in a context object: a path goes through it
    struct ValueSpan {
        BE_CLASS(ValueSpan)

        BE_REFLECT_FIELD int32_t low = 1;
        BE_REFLECT_FIELD int32_t high = 9;
        BE_REFLECT_FIELD Vec2 offset;
    };

    /// A context object with a field of every value type
    class ValueUnitStats {
        BE_CLASS(ValueUnitStats)

    public:
        ValueUnitStats() = default;
        virtual ~ValueUnitStats() = default;

        BE_REFLECT_FIELD bool alive = true;
        BE_REFLECT_FIELD int32_t attack = 12;
        BE_REFLECT_FIELD uint32_t count = 3;
        BE_REFLECT_FIELD int64_t score = 40000000000;
        BE_REFLECT_FIELD float speed = 1.5f;
        BE_REFLECT_FIELD double precise = 0.25;
        BE_REFLECT_FIELD PoolString title;
        BE_REFLECT_FIELD ValueElement element = ValueElement::Water;
        BE_REFLECT_FIELD Vec2 position{2.0f, 3.0f};
        BE_REFLECT_FIELD IVec2 cell{4, 5};
        BE_REFLECT_FIELD Vec3 velocity{1.0f, 2.0f, 3.0f};
        BE_REFLECT_FIELD Vec4 plane{0.0f, 1.0f, 0.0f, -2.0f};
        BE_REFLECT_FIELD Quat facing{0.0f, 0.0f, 0.0f, 1.0f};
        BE_REFLECT_FIELD Rect bounds{1.0f, 2.0f, 3.0f, 4.0f};
        BE_REFLECT_FIELD Color tint{10, 20, 30, 255};
        BE_REFLECT_FIELD ValuePoint spawn{6, 7};
        BE_REFLECT_FIELD ValueSpan span;
        BE_REFLECT_FIELD Vector<int32_t> buffs;
    };

    /// A derived context object: its parent's fields are its own for a path
    class ValueHeroStats : public ValueUnitStats {
        BE_CLASS(ValueHeroStats)

    public:
        ValueHeroStats() = default;
        ~ValueHeroStats() override = default;

        BE_REFLECT_FIELD int32_t renown = 77;
    };

    /// A second level of a context: what is being cast
    struct ValueCastInfo {
        BE_CLASS(ValueCastInfo)

        BE_REFLECT_FIELD int32_t level = 3;
        BE_REFLECT_FIELD float power = 2.5f;
        BE_REFLECT_FIELD Vec3 target{7.0f, 8.0f, 9.0f};
    };

    /// Where a path stops: a value behind a list, a map, an optional or a pointer is not fixed
    struct ValueBehind {
        BE_CLASS(ValueBehind)

        BE_REFLECT_FIELD Vector<ValueSpan> spans;
        BE_REFLECT_FIELD HashMap<String, int32_t> table;
        BE_REFLECT_FIELD Optional<int32_t> maybe;
        BE_REFLECT_FIELD ValueSpan span;
    };

    /// A struct with a `Value` field of the kinds a content file spells: the defaults are not what the tests load, so a field a load left shows
    struct ValueSkill {
        BE_CLASS(ValueSkill)

        BE_REFLECT_FIELD Value<int32_t> damage = 7;
        BE_REFLECT_FIELD Value<float> scale = 1.0f;
        BE_REFLECT_FIELD Value<bool> critical = false;
        BE_REFLECT_FIELD Value<PoolString> label;
        BE_REFLECT_FIELD Value<ValueElement> element = ValueElement::Earth;
        BE_REFLECT_FIELD Value<Vec3> aim = Vec3{9.0f, 9.0f, 9.0f};
        BE_REFLECT_FIELD Value<Color> tint = Color{1, 2, 3, 4};
        BE_REFLECT_FIELD Value<ValuePoint> spot = ValuePoint{9, 9};
    };

    /// The same fields as plain values: what `ValueSkill` loads from a file of plain attributes must be what this loads
    struct ValuePlainSkill {
        BE_CLASS(ValuePlainSkill)

        BE_REFLECT_FIELD int32_t damage = 7;
        BE_REFLECT_FIELD float scale = 1.0f;
        BE_REFLECT_FIELD bool critical = false;
        BE_REFLECT_FIELD PoolString label;
        BE_REFLECT_FIELD ValueElement element = ValueElement::Earth;
        BE_REFLECT_FIELD Vec3 aim{9.0f, 9.0f, 9.0f};
        BE_REFLECT_FIELD Color tint{1, 2, 3, 4};
        BE_REFLECT_FIELD ValuePoint spot{9, 9};
    };

    /// A document that declares its context (`<context>`) before the values that read it; the defaults are not what the tests load
    struct ValueDocument {
        BE_CLASS(ValueDocument)

        BE_REFLECT_FIELD ContextDecl context;
        BE_REFLECT_FIELD Value<int32_t> damage = 7;
        BE_REFLECT_FIELD Value<float> power = 1.0f;
        BE_REFLECT_FIELD Value<Vec3> aim = Vec3{9.0f, 9.0f, 9.0f};
    };

    /// The document of a named config (`MakeFromConfig` needs a `RefCounted`): the same fields as `ValueDocument`
    struct ValueConfigDocument : RefCounted {
        BE_CLASS(ValueConfigDocument)

        BE_REFLECT_FIELD ContextDecl context;
        BE_REFLECT_FIELD Value<int32_t> damage = 7;
        BE_REFLECT_FIELD Value<float> power = 1.0f;
        BE_REFLECT_FIELD Value<Vec3> aim = Vec3{9.0f, 9.0f, 9.0f};
    };

    /// The mistake a document can make: a value is read before the element that declares what it reads
    struct ValueLateDocument {
        BE_CLASS(ValueLateDocument)

        BE_REFLECT_FIELD Value<int32_t> damage = 7;
        BE_REFLECT_FIELD ContextDecl context;
    };

    /// A context of two levels, `caster` (stats) and `cast` (what is cast), with an object behind each; the declaration is checked by the tests that build it
    struct ValueWorld {
        ContextDecl declaration;
        ValueUnitStats caster;
        ValueCastInfo cast;
        Context context;

        ValueWorld() {
            static_cast<void>(declaration.Add<ValueUnitStats>("caster"));
            static_cast<void>(declaration.Add<ValueCastInfo>("cast"));
            context = Context(declaration);
            context.Set(0, caster);
            context.Set(1, cast);
        }

        ValueWorld(const ValueWorld&) = delete;
        ValueWorld& operator=(const ValueWorld&) = delete;
    };

}  // namespace BECore::ValueTestData
