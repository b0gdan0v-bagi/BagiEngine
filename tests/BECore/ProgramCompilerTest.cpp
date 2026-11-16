// The compile function of formulas (Values/Programs/ProgramCompiler): a text becomes a constant, a slot or a program of the pool. Formulas are run in a
// ValueWorld and compared with the same formula in plain C++; folding, the pool, the register limit and the one-line error are checked on their own.

#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <Common/Printers.h>
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        bool Contains(const String& text, StringView part) {
            return text.find(part.data(), 0, part.size()) != String::npos;
        }

        /// The field values a formula is run over: a few states of the two objects of a `ValueWorld`
        struct State {
            bool alive;
            int32_t attack;
            int64_t score;
            float speed;
            double precise;
            int32_t level;
            float power;
        };

        const State kStates[] = {
            {true, 12, 40000000000, 1.5f, 0.25, 3, 2.5f},  {false, 0, 0, 0.0f, 0.0, 0, 0.0f}, {true, -7, -5, -2.0f, -0.5, 1, 100.0f}, {true, 1000, 7, 3.25f, 1e9, 20, -4.0f},
            {false, 99, 123456789012, 8.0f, 2.0, 2, 0.5f}, {true, 5, 1, 0.1f, 0.1, 7, 3.0f},
        };

        void Apply(ValueWorld& world, const State& state) {
            world.caster.alive = state.alive;
            world.caster.attack = state.attack;
            world.caster.score = state.score;
            world.caster.speed = state.speed;
            world.caster.precise = state.precise;
            world.cast.level = state.level;
            world.cast.power = state.power;
        }

        template <typename T>
        struct Formula {
            StringView text;
            T (*reference)(const ValueUnitStats&, const ValueCastInfo&);
        };

        template <typename T>
        void ExpectFormula(const Formula<T>& formula) {
            ValueWorld world;
            const auto compiled = CompileValue<T>(formula.text, world.declaration);
            ASSERT_TRUE(compiled.has_value()) << String(formula.text).c_str() << ": " << compiled.error().message.c_str();
            for (const State& state : kStates) {
                Apply(world, state);
                const T expected = formula.reference(world.caster, world.cast);
                EXPECT_EQ(compiled->Evaluate(world.context), expected) << String(formula.text).c_str() << " with attack " << state.attack << ", level " << state.level;
            }
        }

        CompiledExpression Compile(const ValueWorld& world, StringView text, const TypeDesc& expected) {
            const auto compiled = CompileExpression(text, world.declaration, expected);
            EXPECT_TRUE(compiled.has_value()) << String(text).c_str() << ": " << (compiled.has_value() ? String() : compiled.error().message).c_str();
            return compiled.value_or(CompiledExpression{});
        }

        /// A right-nested sum of @p additions additions of `caster.attack`: every level keeps one more register alive
        String NestedSum(size_t additions) {
            String text = "caster.attack";
            for (size_t i = 0; i < additions; ++i) {
                text = Format("caster.attack + ({})", text);
            }
            return text;
        }

    }  // namespace

    TEST(ProgramCompilerTest, TheEntrysFormulaEqualsPlainCode) {
        ExpectFormula<int32_t>(
            {"caster.attack * (100 + cast.level * 10) / 100", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return caster.attack * (100 + cast.level * 10) / 100; }});
    }

    TEST(ProgramCompilerTest, IntegerFormulasEqualPlainCode) {
        ExpectFormula<int32_t>({"clamp(caster.attack, 0, cast.level * 5)", [](const ValueUnitStats& caster, const ValueCastInfo& cast) {
                                    const int32_t high = cast.level * 5;
                                    return std::min(std::max(caster.attack, 0), high);
                                }});
        ExpectFormula<int32_t>({"max(min(caster.attack, 10), min(cast.level, 5))",
                                [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return std::max(std::min(caster.attack, 10), std::min(cast.level, 5)); }});
        ExpectFormula<int32_t>(
            {"select(caster.alive, caster.attack, -caster.attack)", [](const ValueUnitStats& caster, const ValueCastInfo&) { return caster.alive ? caster.attack : -caster.attack; }});
        ExpectFormula<int32_t>({"abs(caster.attack) % (cast.level + 1) - 3", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return std::abs(caster.attack) % (cast.level + 1) - 3; }});
        ExpectFormula<int64_t>({"caster.score / 3 + 1", [](const ValueUnitStats& caster, const ValueCastInfo&) { return caster.score / 3 + 1; }});
        ExpectFormula<int64_t>(
            {"caster.score + int64(cast.level) * 4000000000", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return caster.score + int64_t{cast.level} * 4000000000; }});
    }

    TEST(ProgramCompilerTest, FloatFormulasEqualPlainCode) {
        ExpectFormula<float>({"lerp(caster.speed, cast.power, 0.25)", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return std::lerp(caster.speed, cast.power, 0.25f); }});
        ExpectFormula<float>(
            {"clamp(caster.speed * cast.power, -1.5, 6)", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return std::min(std::max(caster.speed * cast.power, -1.5f), 6.0f); }});
        ExpectFormula<float>({"float(caster.attack) / 4 + floor(cast.power)",
                              [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return static_cast<float>(caster.attack) / 4.0f + std::floor(cast.power); }});
        ExpectFormula<double>({"caster.precise * 2 + sqrt(2.0)", [](const ValueUnitStats& caster, const ValueCastInfo&) { return caster.precise * 2.0 + std::sqrt(2.0); }});
        ExpectFormula<double>(
            {"round(caster.precise) + ceil(double(cast.power))", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return std::round(caster.precise) + std::ceil(double{cast.power}); }});
    }

    TEST(ProgramCompilerTest, BooleanFormulasEqualPlainCode) {
        ExpectFormula<bool>({"caster.alive && cast.level > 2", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return caster.alive && cast.level > 2; }});
        ExpectFormula<bool>({"!caster.alive || caster.attack >= 12 && cast.power != 2.5",
                             [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return !caster.alive || (caster.attack >= 12 && cast.power != 2.5f); }});
        ExpectFormula<bool>({"select(caster.alive, caster.speed < cast.power, caster.score == 0)",
                             [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return caster.alive ? caster.speed < cast.power : caster.score == 0; }});
    }

    TEST(ProgramCompilerTest, AConstantFormulaIsAConstant) {
        const ValueWorld world;
        const CompiledExpression product = Compile(world, "2 * 6", DescribeType<int32_t>());
        EXPECT_EQ(product.kind, CompiledKind::Constant);
        EXPECT_EQ(product.type, ProgramType::Int32);
        int32_t twelve = 0;
        std::memcpy(&twelve, product.constant.bytes.data(), sizeof(twelve));
        EXPECT_EQ(twelve, 12);

        const auto value = CompileValue<int32_t>("2 * 6", world.declaration);
        ASSERT_TRUE(value.has_value());
        EXPECT_TRUE(value->IsConst());
        EXPECT_EQ(value->GetConst(), 12);

        const auto fraction = CompileValue<float>("1.5 * 2", world.declaration);
        ASSERT_TRUE(fraction.has_value());
        EXPECT_TRUE(fraction->IsConst());
        EXPECT_EQ(fraction->GetConst(), 3.0f);

        const auto condition = CompileValue<bool>("1 < 2 && !false", world.declaration);
        ASSERT_TRUE(condition.has_value());
        EXPECT_TRUE(condition->IsConst());
        EXPECT_TRUE(condition->GetConst());
    }

    TEST(ProgramCompilerTest, FoldingRunsTheInterpretersKernels) {
        const ValueWorld world;
        const auto wrapped = CompileValue<int32_t>("2147483647 + 1", world.declaration);
        ASSERT_TRUE(wrapped.has_value());
        ASSERT_TRUE(wrapped->IsConst());
        EXPECT_EQ(wrapped->GetConst(), std::numeric_limits<int32_t>::min());

        const auto saturated = CompileValue<int32_t>("int(1e10)", world.declaration);
        ASSERT_TRUE(saturated.has_value());
        ASSERT_TRUE(saturated->IsConst());
        EXPECT_EQ(saturated->GetConst(), std::numeric_limits<int32_t>::max());

        const auto divided = CompileValue<int32_t>("7 / (3 - 3) + 7 % (2 - 2)", world.declaration);
        ASSERT_TRUE(divided.has_value());
        ASSERT_TRUE(divided->IsConst());
        EXPECT_EQ(divided->GetConst(), 0);

        const auto clamped = CompileValue<int32_t>("clamp(5, 9, 3)", world.declaration);
        ASSERT_TRUE(clamped.has_value());
        ASSERT_TRUE(clamped->IsConst());
        EXPECT_EQ(clamped->GetConst(), 9);
    }

    TEST(ProgramCompilerTest, AFormulaThatIsOneFieldIsASlotEqualToLink) {
        ValueWorld world;
        const auto value = CompileValue<int32_t>("caster.attack", world.declaration);
        ASSERT_TRUE(value.has_value());
        Value<int32_t> linked;
        ASSERT_TRUE(linked.Link(world.declaration, "caster.attack").has_value());
        EXPECT_TRUE(value->IsSlot());
        EXPECT_EQ(*value, linked);
        EXPECT_EQ(value->Level(), linked.Level());
        EXPECT_EQ(value->Offset(), linked.Offset());
        world.caster.attack = 31;
        EXPECT_EQ(value->Get(world.context), 31);

        const auto nested = CompileValue<int32_t>("caster.span.low", world.declaration);
        ASSERT_TRUE(nested.has_value());
        EXPECT_TRUE(nested->IsSlot());
        EXPECT_EQ(nested->Path(), PoolString::Intern("caster.span.low"));
    }

    TEST(ProgramCompilerTest, ThereIsNoAlgebra) {
        ValueWorld world;
        const auto difference = CompileExpression("caster.attack - caster.attack", world.declaration, DescribeType<int32_t>());
        ASSERT_TRUE(difference.has_value());
        ASSERT_EQ(difference->kind, CompiledKind::Program);
        const Program& program = ProgramPool::Get(difference->program);
        EXPECT_EQ(program.Code().size(), 3U) << program.Disassemble().c_str();
        EXPECT_EQ(program.Code().back().op, Opcode::SubInt32);
        world.caster.attack = 8;
        EXPECT_EQ(Run<int32_t>(program, world.context), 0);

        const auto identity = CompileExpression("caster.attack * 1 + 0", world.declaration, DescribeType<int32_t>());
        ASSERT_TRUE(identity.has_value());
        EXPECT_EQ(identity->kind, CompiledKind::Program);
        EXPECT_EQ(ProgramPool::Get(identity->program).Code().size(), 5U);
    }

    TEST(ProgramCompilerTest, ADivisorThatFoldsToZeroGivesZeroAtRunTime) {
        ValueWorld world;
        const auto value = CompileValue<int32_t>("caster.attack / (3 - 3)", world.declaration);
        ASSERT_TRUE(value.has_value());
        ASSERT_EQ(value->Kind(), ValueKind::Program);
        const Program& program = ProgramPool::Get(value->Id());
        EXPECT_EQ(program.Code().size(), 3U) << program.Disassemble().c_str();
        EXPECT_EQ(program.Code().back().op, Opcode::DivInt32);
        ASSERT_EQ(program.Constants().size(), 1U);
        int32_t divisor = -1;
        std::memcpy(&divisor, program.Constants()[0].bytes.data(), sizeof(divisor));
        EXPECT_EQ(divisor, 0);
        for (const int32_t attack : {0, 12, -7}) {
            world.caster.attack = attack;
            EXPECT_EQ(value->Evaluate(world.context), 0);
        }

        const auto literal = CompileExpression("caster.attack / 0", world.declaration, DescribeType<int32_t>());
        EXPECT_FALSE(literal.has_value());
    }

    TEST(ProgramCompilerTest, EqualConstantsAreOneEntryOfTheTable) {
        const ValueWorld world;
        const auto value = CompileValue<int32_t>("caster.attack * 3 + cast.level * 3 + 3", world.declaration);
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(ProgramPool::Get(value->Id()).Constants().size(), 1U);
    }

    TEST(ProgramCompilerTest, TheSameCodeIsOneIdAndAnotherDeclarationIsAnotherProgram) {
        const ValueWorld world;
        const auto first = CompileValue<int32_t>("caster.attack * (100 + cast.level * 10) / 100", world.declaration);
        const auto second = CompileValue<int32_t>("caster.attack * (100 + cast.level * 10) / 100", world.declaration);
        ASSERT_TRUE(first.has_value() && second.has_value());
        EXPECT_EQ(first->Id(), second->Id());
        const size_t count = ProgramPool::Count();

        const auto respaced = CompileValue<int32_t>("caster.attack*(100+cast.level*10)/100", world.declaration);
        ASSERT_TRUE(respaced.has_value());
        EXPECT_EQ(respaced->Id(), first->Id());
        EXPECT_NE(respaced->Path(), first->Path());
        const auto folded = CompileValue<int32_t>("caster.attack * ((50 + 50) + cast.level * 10) / (10 * 10)", world.declaration);
        ASSERT_TRUE(folded.has_value());
        EXPECT_EQ(folded->Id(), first->Id());
        EXPECT_EQ(ProgramPool::Count(), count);

        ContextDecl swapped;
        ASSERT_TRUE(swapped.Add<ValueCastInfo>("cast").has_value());
        ASSERT_TRUE(swapped.Add<ValueUnitStats>("caster").has_value());
        const auto other = CompileValue<int32_t>("caster.attack * (100 + cast.level * 10) / 100", swapped);
        ASSERT_TRUE(other.has_value());
        EXPECT_NE(other->Id(), first->Id());
        EXPECT_EQ(ProgramPool::Count(), count + 1);
    }

    TEST(ProgramCompilerTest, ReadsIsTheLevelsTheFormulaUses) {
        const ValueWorld world;
        const auto reads = [&](StringView text) {
            const auto value = CompileValue<int32_t>(text, world.declaration);
            EXPECT_TRUE(value.has_value()) << String(text).c_str();
            return value.has_value() ? value->Reads() : LevelMask{0};
        };
        EXPECT_EQ(reads("caster.attack * 2"), LevelBit(0));
        EXPECT_EQ(reads("cast.level * 2 + 1"), LevelBit(1));
        EXPECT_EQ(reads("caster.attack + cast.level"), LevelMask{LevelBit(0) | LevelBit(1)});
        EXPECT_EQ(reads("caster.attack"), LevelBit(0));
        EXPECT_EQ(reads("12 + 1"), LevelMask{0});
    }

    TEST(ProgramCompilerTest, ADeepFormulaFitsTheRegistersUpToTheLimit) {
        ValueWorld world;
        // k additions nest k + 1 operands, each in a register of its own
        const String fits = NestedSum(kMaxProgramRegisters - 1);
        const auto fitting = CompileValue<int32_t>(fits, world.declaration);
        ASSERT_TRUE(fitting.has_value()) << fitting.error().message.c_str();
        EXPECT_EQ(ProgramPool::Get(fitting->Id()).RegisterCount(), kMaxProgramRegisters);
        world.caster.attack = 2;
        EXPECT_EQ(fitting->Evaluate(world.context), 2 * static_cast<int32_t>(kMaxProgramRegisters));

        const String tooDeep = NestedSum(kMaxProgramRegisters);
        const size_t count = ProgramPool::Count();
        const auto refused = CompileValue<int32_t>(tooDeep, world.declaration);
        ASSERT_FALSE(refused.has_value());
        EXPECT_TRUE(Contains(refused.error().message, "32 registers")) << refused.error().message.c_str();
        EXPECT_GT(refused.error().column, 0U);
        EXPECT_EQ(ProgramPool::Count(), count);
    }

    TEST(ProgramCompilerTest, ATextWithAnErrorLeavesNothingInThePool) {
        const ValueWorld world;
        const size_t count = ProgramPool::Count();
        for (const StringView text :
             {"caster.attack * ", "caster.attak * 2", "caster.attack * 2.5", "caster.attack + true", "caster.attack @ 2", "", "caster.attack / 0", "min(caster.attack)", "cast.power * 2"}) {
            const auto compiled = CompileExpression(text, world.declaration, DescribeType<int32_t>());
            EXPECT_FALSE(compiled.has_value()) << String(text).c_str();
        }
        EXPECT_EQ(ProgramPool::Count(), count);
    }

    TEST(ProgramCompilerTest, ATypeNoProgramComputesIsRefusedBeforeTheTextIsRead) {
        const ValueWorld world;
        const auto rect = CompileExpression("((( not a formula", world.declaration, DescribeType<Rect>());
        ASSERT_FALSE(rect.has_value());
        EXPECT_TRUE(Contains(rect.error().message, "bool, int, int64, float, double, vec2, vec3, vec4, ivec2, quat, color, enum or string")) << rect.error().message.c_str();
        EXPECT_TRUE(Contains(rect.error().message, "Rect")) << rect.error().message.c_str();
        EXPECT_EQ(rect.error().column, 0U);
        const auto unsigned32 = CompileValue<uint32_t>("caster.count", world.declaration);
        ASSERT_FALSE(unsigned32.has_value());
        EXPECT_TRUE(Contains(unsigned32.error().message, "bool, int, int64, float, double"));
    }

    TEST(ProgramCompilerTest, AProgramValueKeepsTheTextAsWritten) {
        const ValueWorld world;
        const auto value = CompileValue<int32_t>("caster.attack  *  2", world.declaration);
        ASSERT_TRUE(value.has_value());
        ASSERT_EQ(value->Kind(), ValueKind::Program);
        EXPECT_EQ(value->Path(), PoolString::Intern("caster.attack  *  2"));
        EXPECT_EQ(ProgramPool::Get(value->Id()).ResultType(), ProgramType::Int32);
    }

    TEST(ProgramCompilerTest, ACheckerErrorKeepsItsColumn) {
        const ValueWorld world;
        const auto compiled = CompileExpression("caster.attack + cast.power", world.declaration, DescribeType<int32_t>());
        ASSERT_FALSE(compiled.has_value());
        EXPECT_GT(compiled.error().column, 0U);
        const String line = FormatProgramError("caster.attack + cast.power", compiled.error());
        EXPECT_TRUE(Contains(line, Format("(column {})", compiled.error().column))) << line.c_str();
    }

    TEST(ProgramCompilerTest, TheErrorLineHasACaretUnderTheColumn) {
        EXPECT_EQ(FormatProgramError("abcdefgh", ProgramError{"bad", 5}), "bad (column 5)\n  abcdefgh\n      ^");
        EXPECT_EQ(FormatProgramError("abcdefgh", ProgramError{"bad", 1}), "bad (column 1)\n  abcdefgh\n  ^");
        EXPECT_EQ(FormatProgramError("abc", ProgramError{"no place", 0}), "no place");
        // One past the end: an operator with no operand
        EXPECT_EQ(FormatProgramError("1 +", ProgramError{"expected a value", 4}), "expected a value (column 4)\n  1 +\n     ^");
    }

    TEST(ProgramCompilerTest, ALongTextShowsAWindowAroundTheColumn) {
        const String text = String(100, 'a') + "b" + String(100, 'c');
        const String line = FormatProgramError(text, ProgramError{"bad", 101});
        const String expected = Format("bad (column 101)\n  ...{}b{}...\n  {}^", String(32, 'a'), String(27, 'c'), String(3 + 32, ' '));
        EXPECT_EQ(line, expected);
    }

    TEST(ProgramCompilerTest, AByteThatIsNotPrintableIsShownAsAQuestionMark) {
        const String text = "1 + \x01\xff";
        const String line = FormatProgramError(text, ProgramError{"bad", 5});
        EXPECT_EQ(line, "bad (column 5)\n  1 + ??\n      ^");
    }

}  // namespace BECore::Tests
