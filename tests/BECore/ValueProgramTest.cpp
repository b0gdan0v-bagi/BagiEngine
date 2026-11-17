// Value<T> of the kind Program (Values/Value.h): a handle of a compiled formula in the ProgramPool, evaluated over a context. The programs are
// assembled by hand here; the text front end has its own tests.

#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <BECore/Values/Value.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr int32_t kFactor = 3;

        SlotRef Link(const ValueWorld& world, StringView path, const TypeDesc& type) {
            const auto slot = LinkSlot(world.declaration, path, type);
            EXPECT_TRUE(slot.has_value()) << String(path).c_str();
            return slot.value_or(SlotRef{});
        }

        /// `caster.attack * 3`, interned, as the value a loader would make from the text @p text
        Value<int32_t> AttackTimesThree(const ValueWorld& world, StringView text) {
            ProgramBuilder builder;
            const uint8_t attack = builder.AcquireRegister().value();
            builder.EmitLoadSlot(attack, ProgramType::Int32, Link(world, "caster.attack", DescribeType<int32_t>()));
            const uint8_t factor = builder.AcquireRegister().value();
            builder.EmitLoadConst(factor, kFactor);
            builder.Emit(Opcode::MulInt32, attack, attack, factor);
            const ProgramId id = ProgramPool::Intern(builder.Finish(ProgramType::Int32, attack, PoolString::Intern(text)).value()).value();
            return Value<int32_t>::FromProgram(id, PoolString::Intern(text));
        }

        /// A program that reads only the level `cast`: `cast.power * 2.0f`
        Value<float> PowerTimesTwo(const ValueWorld& world) {
            ProgramBuilder builder;
            const uint8_t power = builder.AcquireRegister().value();
            builder.EmitLoadSlot(power, ProgramType::Float, Link(world, "cast.power", DescribeType<float>()));
            const uint8_t two = builder.AcquireRegister().value();
            builder.EmitLoadConst(two, 2.0f);
            builder.Emit(Opcode::MulFloat, power, power, two);
            const ProgramId id = ProgramPool::Intern(builder.Finish(ProgramType::Float, power).value()).value();
            return Value<float>::FromProgram(id, "cast.power * 2"_intern);
        }

    }  // namespace

    static_assert(std::is_trivially_copyable_v<Value<int32_t>> && std::is_trivially_copyable_v<Value<double>> && std::is_trivially_copyable_v<Value<Vec3>>);
    static_assert(sizeof(Value<float>) == 24);

    TEST(ValueProgramTest, CompileTimeChecks) {
        SUCCEED();
    }

    TEST(ValueProgramTest, AProgramValueEvaluatesToTheReference) {
        ValueWorld world;
        const Value<int32_t> value = AttackTimesThree(world, "caster.attack * 3");
        EXPECT_EQ(value.Kind(), ValueKind::Program);
        EXPECT_FALSE(value.IsConst());
        EXPECT_FALSE(value.IsSlot());
        for (const int32_t attack : {0, 12, -7, 1000}) {
            world.caster.attack = attack;
            EXPECT_EQ(value.Evaluate(world.context), attack * kFactor);
        }
        EXPECT_EQ(PowerTimesTwo(world).Evaluate(world.context), world.cast.power * 2.0f);
    }

    TEST(ValueProgramTest, AProgramValueOfAVectorAnEnumAndAStringEvaluatesToTheReference) {
        ValueWorld world;
        // cast.target + (1, 2, 3)
        ProgramBuilder vector;
        const uint8_t target = vector.AcquireRegister().value();
        vector.EmitLoadSlot(target, ProgramType::Vec3, Link(world, "cast.target", DescribeType<Vec3>()));
        const uint8_t offset = vector.AcquireRegister().value();
        vector.EmitLoadConst(offset, Vec3{1.0f, 2.0f, 3.0f});
        vector.Emit(Opcode::AddVec3, target, target, offset);
        const ProgramId vectorId = ProgramPool::Intern(vector.Finish(ProgramType::Vec3, target).value()).value();
        const Value<Vec3> shifted = Value<Vec3>::FromProgram(vectorId, "cast.target + vec3(1, 2, 3)"_intern);
        EXPECT_EQ(shifted.Evaluate(world.context), world.cast.target + Vec3(1.0f, 2.0f, 3.0f));

        // The element of the caster: the register holds the index, the value is the enumerator of its own enum
        ProgramBuilder element;
        const uint8_t loaded = element.AcquireRegister().value();
        element.EmitLoadSlot(loaded, ProgramType::Enum, Link(world, "caster.element", DescribeType<ValueElement>()), static_cast<uint8_t>(DescribeType<ValueElement>().enumBytes));
        const ProgramId elementId = ProgramPool::Intern(element.Finish(ProgramType::Enum, loaded).value()).value();
        const Value<ValueElement> chosen = Value<ValueElement>::FromProgram(elementId, "caster.element"_intern);
        for (const ValueElement value : {ValueElement::Fire, ValueElement::Water, ValueElement::Earth}) {
            world.caster.element = value;
            EXPECT_EQ(chosen.Evaluate(world.context), value);
        }

        // select(caster.alive, 'burn', 'chill'): a string is the bytes of an interned text
        ProgramBuilder text;
        const uint8_t condition = text.AcquireRegister().value();
        text.EmitLoadSlot(condition, ProgramType::Bool, Link(world, "caster.alive", DescribeType<bool>()));
        const uint8_t burn = text.AcquireRegister().value();
        text.EmitLoadConst(burn, PoolString{"burn"_intern});
        const uint8_t chill = text.AcquireRegister().value();
        text.EmitLoadConst(chill, PoolString{"chill"_intern});
        text.Emit(Opcode::SelectString, condition, condition, burn, chill);
        const ProgramId textId = ProgramPool::Intern(text.Finish(ProgramType::String, condition).value()).value();
        const Value<PoolString> label = Value<PoolString>::FromProgram(textId, "select(caster.alive, 'burn', 'chill')"_intern);
        world.caster.alive = true;
        EXPECT_EQ(label.Evaluate(world.context), "burn"_intern);
        world.caster.alive = false;
        EXPECT_EQ(label.Evaluate(world.context), "chill"_intern);
        EXPECT_EQ(label.Reads(), LevelBit(0));
    }

    TEST(ValueProgramTest, EvaluateOfAConstAndASlotEqualsGet) {
        ValueWorld world;
        const Value<int32_t> constant = 41;
        EXPECT_EQ(constant.Evaluate(world.context), constant.Get(world.context));
        Value<int32_t> slot;
        ASSERT_TRUE(slot.Link(world.declaration, "caster.attack").has_value());
        world.caster.attack = 77;
        EXPECT_EQ(slot.Evaluate(world.context), 77);
        EXPECT_EQ(slot.Evaluate(world.context), slot.Get(world.context));
        Value<Vec3> vector = Vec3{1.0f, 2.0f, 3.0f};
        EXPECT_EQ(vector.Evaluate(world.context), vector.GetConst());
    }

    TEST(ValueProgramTest, ReadsOfAConstASlotAndAProgram) {
        ValueWorld world;
        EXPECT_EQ(Value<int32_t>(5).Reads(), LevelMask{0});
        Value<int32_t> slot;
        ASSERT_TRUE(slot.Link(world.declaration, "cast.level").has_value());
        EXPECT_EQ(slot.Reads(), LevelBit(1));
        EXPECT_EQ(AttackTimesThree(world, "caster.attack * 3").Reads(), LevelBit(0));
        EXPECT_EQ(PowerTimesTwo(world).Reads(), LevelBit(1));
        ValueDependencies dependencies;
        dependencies.Add(slot).Add(AttackTimesThree(world, "caster.attack * 3"));
        EXPECT_EQ(dependencies.Mask(), LevelBit(0) | LevelBit(1));
    }

    TEST(ValueProgramTest, AProgramValueCarriesItsIdAndItsText) {
        ValueWorld world;
        const Value<int32_t> value = AttackTimesThree(world, "caster.attack * 3");
        EXPECT_EQ(value.Path().ToStringView(), "caster.attack * 3");
        EXPECT_EQ(ProgramPool::Get(value.Id()).Reads(), LevelBit(0));
        EXPECT_EQ(ProgramPool::Get(value.Id()).ResultType(), ProgramType::Int32);
    }

    TEST(ValueProgramTest, EqualityIsTheIdAndTheText) {
        ValueWorld world;
        const Value<int32_t> first = AttackTimesThree(world, "caster.attack * 3");
        const Value<int32_t> again = AttackTimesThree(world, "caster.attack * 3");
        const Value<int32_t> spelledOtherwise = AttackTimesThree(world, "3 * caster.attack");
        EXPECT_EQ(first, again);
        EXPECT_EQ(first.Id(), spelledOtherwise.Id()) << "one program";
        EXPECT_NE(first, spelledOtherwise) << "written another way";
        EXPECT_NE(first, Value<int32_t>(0));
        Value<int32_t> slot;
        ASSERT_TRUE(slot.Link(world.declaration, "caster.attack").has_value());
        EXPECT_NE(first, slot);
        EXPECT_NE(Value<int32_t>::FromProgram(first.Id(), "x"_intern), first);
    }

}  // namespace BECore::Tests
