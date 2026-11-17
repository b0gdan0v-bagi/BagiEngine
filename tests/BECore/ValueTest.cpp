// Value<T> (Values/Value.h): a literal or a slot of a context object, for every value type. The reflected context types are those of
// ValueTestTypes.h, found by name (their generated code is included in ValueContextTest.cpp).

#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Value.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        /// A slot value of `path` reads the very member, whatever its type
        template <typename T, typename Member>
        void ExpectSlotReadsMember(const ValueWorld& world, StringView path, const Member& member) {
            static_assert(std::is_same_v<T, Member>);
            Value<T> value;
            const auto linked = value.Link(world.declaration, path);
            ASSERT_TRUE(linked.has_value()) << String(path).c_str() << ": " << (linked.has_value() ? String{} : linked.error().message).c_str();
            EXPECT_EQ(value.Kind(), ValueKind::Slot);
            EXPECT_FALSE(value.IsConst());
            EXPECT_EQ(value.Path().ToStringView(), path);
            EXPECT_EQ(static_cast<const void*>(&value.Get(world.context)), static_cast<const void*>(&member)) << String(path).c_str();
        }

        /// The literal of a value is read back as it was given, in every kind of construction
        template <typename T>
        void ExpectConstReadsLiteral(const T& literal, const T& other, const ValueWorld& world) {
            const Value<T> implicitValue = literal;
            const Value<T> named = Value<T>::Const(literal);
            EXPECT_EQ(implicitValue.Kind(), ValueKind::Const);
            EXPECT_TRUE(implicitValue.IsConst());
            EXPECT_TRUE(named.IsConst());
            EXPECT_TRUE(implicitValue.Path().Empty());
            EXPECT_EQ(std::memcmp(&implicitValue.Get(world.context), &literal, sizeof(T)), 0);
            EXPECT_EQ(std::memcmp(&named.GetConst(), &literal, sizeof(T)), 0);
            EXPECT_NE(std::memcmp(&Value<T>(other).GetConst(), &literal, sizeof(T)), 0);
        }

    }  // namespace

    static_assert(sizeof(Value<float>) == 24, "Value<float> grew: the layout of Value is a size decision, change this number only on purpose");
    static_assert(std::is_trivially_copyable_v<Value<Vec3>>);
    static_assert(ValueType<ValueElement> && ValueType<ValuePoint>);
    static_assert(!ValueType<String> && !ValueType<Vector<int32_t>> && !ValueType<HashMap<String, int32_t>> && !ValueType<Value<float>>);

    TEST(ValueTest, ALiteralOfEveryValueTypeIsReadBackFromAConst) {
        const ValueWorld world;
        ExpectConstReadsLiteral<bool>(true, false, world);
        ExpectConstReadsLiteral<int32_t>(12, 13, world);
        ExpectConstReadsLiteral<uint32_t>(12U, 13U, world);
        ExpectConstReadsLiteral<int64_t>(40000000000, 1, world);
        ExpectConstReadsLiteral<float>(1.5f, 2.5f, world);
        ExpectConstReadsLiteral<double>(0.25, 0.5, world);
        ExpectConstReadsLiteral<ValueElement>(ValueElement::Earth, ValueElement::Fire, world);
        ExpectConstReadsLiteral<Vec2>({1.0f, 2.0f}, {2.0f, 1.0f}, world);
        ExpectConstReadsLiteral<IVec2>({1, 2}, {2, 1}, world);
        ExpectConstReadsLiteral<Vec3>({1.0f, 2.0f, 3.0f}, {3.0f, 2.0f, 1.0f}, world);
        ExpectConstReadsLiteral<Vec4>({1.0f, 2.0f, 3.0f, 4.0f}, {4.0f, 3.0f, 2.0f, 1.0f}, world);
        ExpectConstReadsLiteral<Quat>({0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, world);
        ExpectConstReadsLiteral<Rect>({1.0f, 2.0f, 3.0f, 4.0f}, {4.0f, 3.0f, 2.0f, 1.0f}, world);
        ExpectConstReadsLiteral<Color>({1, 2, 3, 4}, {4, 3, 2, 1}, world);
        ExpectConstReadsLiteral<ValuePoint>({3, 4}, {4, 3}, world);
    }

    TEST(ValueTest, APoolStringLiteralIsReadBackFromAConst) {
        const ValueWorld world;
        const Value<PoolString> value = PoolString{"Hero"_intern};
        EXPECT_TRUE(value.IsConst());
        EXPECT_EQ(value.Get(world.context), "Hero"_intern);
        EXPECT_EQ(value.GetConst(), "Hero"_intern);
    }

    TEST(ValueTest, ADefaultValueIsAConstOfTheDefaultOfItsType) {
        const ValueWorld world;
        const Value<int32_t> number;
        const Value<Vec3> vector;
        EXPECT_TRUE(number.IsConst());
        EXPECT_EQ(number.Get(world.context), 0);
        EXPECT_EQ(vector.Get(world.context), Vec3{});
    }

    TEST(ValueTest, ASlotOfEveryValueTypeReadsTheMemberOfTheContextObject) {
        const ValueWorld world;
        ExpectSlotReadsMember<bool>(world, "caster.alive", world.caster.alive);
        ExpectSlotReadsMember<int32_t>(world, "caster.attack", world.caster.attack);
        ExpectSlotReadsMember<uint32_t>(world, "caster.count", world.caster.count);
        ExpectSlotReadsMember<int64_t>(world, "caster.score", world.caster.score);
        ExpectSlotReadsMember<float>(world, "caster.speed", world.caster.speed);
        ExpectSlotReadsMember<double>(world, "caster.precise", world.caster.precise);
        ExpectSlotReadsMember<PoolString>(world, "caster.title", world.caster.title);
        ExpectSlotReadsMember<ValueElement>(world, "caster.element", world.caster.element);
        ExpectSlotReadsMember<Vec2>(world, "caster.position", world.caster.position);
        ExpectSlotReadsMember<IVec2>(world, "caster.cell", world.caster.cell);
        ExpectSlotReadsMember<Vec3>(world, "caster.velocity", world.caster.velocity);
        ExpectSlotReadsMember<Vec4>(world, "caster.plane", world.caster.plane);
        ExpectSlotReadsMember<Quat>(world, "caster.facing", world.caster.facing);
        ExpectSlotReadsMember<Rect>(world, "caster.bounds", world.caster.bounds);
        ExpectSlotReadsMember<Color>(world, "caster.tint", world.caster.tint);
        ExpectSlotReadsMember<ValuePoint>(world, "caster.spawn", world.caster.spawn);
    }

    TEST(ValueTest, ASlotIntoAStructAndIntoAnotherLevelReadsThatMember) {
        const ValueWorld world;
        ExpectSlotReadsMember<int32_t>(world, "caster.span.high", world.caster.span.high);
        ExpectSlotReadsMember<Vec2>(world, "caster.span.offset", world.caster.span.offset);
        ExpectSlotReadsMember<int32_t>(world, "cast.level", world.cast.level);
        ExpectSlotReadsMember<Vec3>(world, "cast.target", world.cast.target);
    }

    TEST(ValueTest, ASlotKnowsItsLevelAndItsOffsetAndWhereItsPathIs) {
        const ValueWorld world;
        Value<int32_t> value;
        ASSERT_TRUE(value.Link(world.declaration, "cast.level").has_value());
        EXPECT_EQ(value.Level(), LevelIndex{1});
        const char* address = static_cast<const char*>(world.context.Object(value.Level())) + value.Offset();
        EXPECT_EQ(static_cast<const void*>(address), static_cast<const void*>(&world.cast.level));
    }

    TEST(ValueTest, ASlotSeesALaterWriteToTheObject) {
        ValueWorld world;
        Value<int32_t> attack;
        Value<Vec3> velocity;
        ASSERT_TRUE(attack.Link(world.declaration, "caster.attack").has_value());
        ASSERT_TRUE(velocity.Link(world.declaration, "caster.velocity").has_value());
        EXPECT_EQ(attack.Get(world.context), 12);
        world.caster.attack = 99;
        world.caster.velocity = Vec3{9.0f, 8.0f, 7.0f};
        EXPECT_EQ(attack.Get(world.context), 99);
        EXPECT_EQ(velocity.Get(world.context), (Vec3{9.0f, 8.0f, 7.0f}));
    }

    TEST(ValueTest, TheSameSlotReadsTheObjectOfEachContext) {
        ValueWorld first;
        ValueWorld second;
        second.caster.attack = 5;
        Value<int32_t> attack;
        ASSERT_TRUE(attack.Link(first.declaration, "caster.attack").has_value());
        EXPECT_EQ(attack.Get(first.context), 12);
        EXPECT_EQ(attack.Get(second.context), 5);
    }

    TEST(ValueTest, ASlotOfABaseLevelReadsAnObjectOfADerivedType) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
        ValueHeroStats hero;
        hero.attack = 31;
        hero.speed = 4.5f;
        Context context(declaration);
        context.Set(0, hero);
        Value<int32_t> attack;
        Value<float> speed;
        ASSERT_TRUE(attack.Link(declaration, "caster.attack").has_value());
        ASSERT_TRUE(speed.Link(declaration, "caster.speed").has_value());
        EXPECT_EQ(attack.Get(context), 31);
        EXPECT_EQ(&speed.Get(context), &hero.speed);
    }

    TEST(ValueTest, ALinkOfAWrongTypeIsRefusedAndTheValueIsUnchanged) {
        const ValueWorld world;
        Value<float> value = 3.5f;
        const auto linked = value.Link(world.declaration, "caster.attack");
        ASSERT_FALSE(linked.has_value());
        EXPECT_EQ(linked.error().code, LinkErrorCode::type_mismatch);
        EXPECT_TRUE(value.IsConst());
        EXPECT_EQ(value.GetConst(), 3.5f);

        Value<Vec3> vector = Vec3{1.0f, 1.0f, 1.0f};
        const auto opaque = vector.Link(world.declaration, "caster.position");
        ASSERT_FALSE(opaque.has_value());
        EXPECT_EQ(opaque.error().code, LinkErrorCode::type_mismatch);
        EXPECT_TRUE(vector.IsConst());
    }

    TEST(ValueTest, ALinkOfABadPathIsRefusedAndTheValueIsUnchanged) {
        const ValueWorld world;
        Value<int32_t> value;
        ASSERT_TRUE(value.Link(world.declaration, "caster.attack").has_value());
        const uint32_t offset = value.Offset();

        const auto unknown = value.Link(world.declaration, "caster.nothing");
        ASSERT_FALSE(unknown.has_value());
        EXPECT_EQ(unknown.error().code, LinkErrorCode::unknown_field);
        EXPECT_TRUE(value.IsSlot());
        EXPECT_EQ(value.Offset(), offset);
        EXPECT_EQ(value.Path().ToStringView(), "caster.attack");

        const auto malformed = value.Link(world.declaration, "caster..attack");
        ASSERT_FALSE(malformed.has_value());
        EXPECT_EQ(malformed.error().code, LinkErrorCode::invalid_path);
        EXPECT_EQ(value.Path().ToStringView(), "caster.attack");

        const auto level = value.Link(world.declaration, "enemy.attack");
        ASSERT_FALSE(level.has_value());
        EXPECT_EQ(level.error().code, LinkErrorCode::unknown_level);
    }

    TEST(ValueTest, AValueIsEqualToAnotherOfTheSameKindAndContent) {
        const ValueWorld world;
        Value<int32_t> a;
        Value<int32_t> b;
        ASSERT_TRUE(a.Link(world.declaration, "caster.attack").has_value());
        ASSERT_TRUE(b.Link(world.declaration, "caster.attack").has_value());
        EXPECT_EQ(a, b);

        Value<int32_t> other;
        ASSERT_TRUE(other.Link(world.declaration, "caster.span.high").has_value());
        EXPECT_NE(a, other);
        EXPECT_NE(a, Value<int32_t>(12));
        EXPECT_EQ(Value<int32_t>(12), Value<int32_t>::Const(12));
        EXPECT_NE(Value<int32_t>(12), Value<int32_t>(13));
        EXPECT_EQ(Value<PoolString>("a"_intern), Value<PoolString>("a"_intern));
    }

    TEST(ValueTest, AValueFromSlotKeepsWhatItWasGiven) {
        const Value<float> value = Value<float>::FromSlot(SlotRef{2, 40}, "view.height"_intern);
        EXPECT_EQ(value.Kind(), ValueKind::Slot);
        EXPECT_EQ(value.Level(), LevelIndex{2});
        EXPECT_EQ(value.Offset(), 40U);
        EXPECT_EQ(value.Path(), "view.height"_intern);
    }

    TEST(ValueTest, CompileTimeChecks) {}

}  // namespace BECore::Tests
