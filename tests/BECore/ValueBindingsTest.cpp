// CompileValue<T> as the entry point of code that holds a formula and no document (Values/Programs/ProgramCompiler.h): conditions and arguments over a Context,
// the kinds it gives back, its errors and its tables.

#include <BECore/Threading/Thread.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <BECore/Values/ValueTables.h>
#include <Common/Printers.h>
#include <Common/Wait.h>
#include <atomic>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr StringView kCondition = "caster.attack < 10 && caster.alive";
        constexpr StringView kArgument = "caster.attack * 2 + cast.level";
        constexpr int kCompilingThreads = 2;

        template <typename T>
        Value<T> Compiled(const ContextDecl& declaration, StringView text, const CompileOptions& options = {}) {
            const auto value = CompileValue<T>(text, declaration, options);
            EXPECT_TRUE(value.has_value()) << String(text).c_str() << ": " << (value.has_value() ? String() : value.error().message).c_str();
            return value.value_or(Value<T>{});
        }

    }  // namespace

    TEST(ValueBindingsTest, AConditionAndAnArgumentEvaluateOverTwoContexts) {
        ValueWorld weak;
        weak.caster.attack = 5;
        weak.cast.level = 3;
        ValueWorld strong;
        strong.caster.attack = 50;
        strong.cast.level = 4;
        strong.caster.alive = false;

        const Value<bool> condition = Compiled<bool>(weak.declaration, kCondition);
        const Value<int32_t> argument = Compiled<int32_t>(weak.declaration, kArgument);
        ASSERT_EQ(condition.Kind(), ValueKind::Program);
        ASSERT_EQ(argument.Kind(), ValueKind::Program);
        EXPECT_EQ(condition.Path(), PoolString::Intern(kCondition));

        EXPECT_TRUE(condition.Evaluate(weak.context));
        EXPECT_FALSE(condition.Evaluate(strong.context));
        EXPECT_EQ(argument.Evaluate(weak.context), 13);
        EXPECT_EQ(argument.Evaluate(strong.context), 104);
        // A weak caster who is dead is no condition either
        weak.caster.alive = false;
        EXPECT_FALSE(condition.Evaluate(weak.context));
        EXPECT_EQ(condition.Reads(), LevelBit(0));
    }

    TEST(ValueBindingsTest, TheSameTextUnderTwoDeclarationsIsTwoPrograms) {
        const ValueWorld world;
        ContextDecl swapped;
        ASSERT_TRUE(swapped.Add<ValueCastInfo>("cast").has_value());
        ASSERT_TRUE(swapped.Add<ValueUnitStats>("caster").has_value());

        const Value<int32_t> first = Compiled<int32_t>(world.declaration, kArgument);
        const Value<int32_t> second = Compiled<int32_t>(swapped, kArgument);
        EXPECT_NE(first.Id(), second.Id());
        EXPECT_EQ(Compiled<int32_t>(world.declaration, kArgument), first) << "one declaration is one program";

        ValueWorld a;
        a.caster.attack = 7;
        a.cast.level = 2;
        Context reordered(swapped);
        reordered.Set(0, a.cast);
        reordered.Set(1, a.caster);
        EXPECT_EQ(first.Evaluate(a.context), 16);
        EXPECT_EQ(second.Evaluate(reordered), 16);
    }

    TEST(ValueBindingsTest, ATextThatIsAConstantOrOneFieldIsNoProgram) {
        ValueWorld world;
        const size_t before = ProgramPool::Count();
        const Value<int32_t> constant = Compiled<int32_t>(world.declaration, "2 * 6");
        ASSERT_EQ(constant.Kind(), ValueKind::Const);
        EXPECT_EQ(constant.GetConst(), 12);
        const Value<int32_t> field = Compiled<int32_t>(world.declaration, "caster.attack");
        ASSERT_EQ(field.Kind(), ValueKind::Slot);
        EXPECT_EQ(field.Path(), "caster.attack"_intern);
        EXPECT_EQ(ProgramPool::Count(), before);
        world.caster.attack = 33;
        EXPECT_EQ(field.Evaluate(world.context), 33);

        const Value<ValueElement> element = Compiled<ValueElement>(world.declaration, "Fire");
        ASSERT_EQ(element.Kind(), ValueKind::Const);
        EXPECT_EQ(element.GetConst(), ValueElement::Fire);
    }

    TEST(ValueBindingsTest, AnErrorHasTheColumnOfTheTextAndAddsNothingToThePool) {
        const ValueWorld world;
        const size_t before = ProgramPool::Count();
        const StringView text = "caster.attack * cast.power";
        const auto refused = CompileValue<int32_t>(text, world.declaration);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().column, 17U);
        EXPECT_EQ(ProgramPool::Count(), before);
        const String shown = FormatProgramError(text, refused.error());
        EXPECT_NE(shown.find("(column 17)"), String::npos) << shown.c_str();
        EXPECT_NE(shown.find("^"), String::npos) << shown.c_str();

        const auto notAType = CompileValue<Rect>("caster.bounds", world.declaration);
        ASSERT_FALSE(notAType.has_value());
        EXPECT_NE(notAType.error().message.find("a formula can have the type"), String::npos) << notAType.error().message.c_str();
    }

    TEST(ValueBindingsTest, TwoThreadsCompilingTheSameTextGetTheSameIds) {
        const ValueWorld world;
        struct Outcome {
            bool metOther = false;
            ProgramId argument = 0;
            ProgramId condition = 0;
            bool ok = true;
        };
        std::atomic<int> arrived{0};
        const auto work = [&](Outcome& outcome) {
            arrived.fetch_add(1);
            outcome.metOther = WaitUntil([&arrived] { return arrived.load() == kCompilingThreads; });
            for (int round = 0; round < 50; ++round) {
                const auto argument = CompileValue<int32_t>(kArgument, world.declaration);
                const auto condition = CompileValue<bool>(kCondition, world.declaration);
                outcome.ok = outcome.ok && argument.has_value() && condition.has_value();
                if (!outcome.ok) {
                    return;
                }
                outcome.argument = argument->Id();
                outcome.condition = condition->Id();
            }
        };
        Outcome a;
        Outcome b;
        {
            Thread threadA("ValueBindings A", [&] { work(a); });
            Thread threadB("ValueBindings B", [&] { work(b); });
            threadA.join();
            threadB.join();
        }
        EXPECT_TRUE(a.metOther);
        EXPECT_TRUE(b.metOther);
        ASSERT_TRUE(a.ok);
        ASSERT_TRUE(b.ok);
        EXPECT_EQ(a.argument, b.argument);
        EXPECT_EQ(a.condition, b.condition);
        EXPECT_NE(a.argument, a.condition);
    }

    TEST(ValueBindingsTest, TheOptionsCarryTheTablesOfByLevel) {
        ValueTableSet tables;
        tables.Add("Damage", {5.0, 8.0, 13.0});
        CompileOptions options;
        options.tables = &tables;
        ValueWorld world;
        const Value<int32_t> damage = Compiled<int32_t>(world.declaration, "ByLevel('Damage', cast.level) + caster.attack", options);
        world.caster.attack = 100;
        for (const int32_t level : {-1, 0, 1, 2, 3, 9}) {
            world.cast.level = level;
            const int32_t entry = level < 0 ? 5 : (level == 0 ? 5 : (level == 1 ? 8 : 13));
            EXPECT_EQ(damage.Evaluate(world.context), 100 + entry) << level;
        }
        const auto none = CompileValue<int32_t>("ByLevel('Damage', cast.level)", world.declaration);
        ASSERT_FALSE(none.has_value());
        EXPECT_NE(none.error().message.find("no tables are declared"), String::npos) << none.error().message.c_str();
    }

}  // namespace BECore::Tests
