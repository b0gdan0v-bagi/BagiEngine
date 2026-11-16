// The compile function of formulas (Values/Programs/ProgramCompiler): a text becomes a constant, a slot or a program of the pool. Formulas are run in a
// ValueWorld and compared with the same formula in plain C++; folding, the pool, the register limit and the one-line error are checked on their own.

#include <BECore/Animation/Lerp.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/Components.h>
#include <BECore/Values/Programs/ExprChecker.h>
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

    // The vector, quaternion, color, enum and string types

    namespace {

        /// The fields of the new types a formula is run over
        struct VectorState {
            bool alive;
            Vec2 position;
            IVec2 cell;
            Vec3 velocity;
            Vec4 plane;
            Quat facing;
            Color tint;
            ValueElement element;
            Vec3 target;
            float power;
        };

        constexpr float kQuarterTurn = 0.70710678f;

        const VectorState kVectorStates[] = {
            {true, {2.0f, 3.0f}, {4, 5}, {1.0f, 2.0f, 3.0f}, {0.0f, 1.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {10, 20, 30, 255}, ValueElement::Water, {7.0f, 8.0f, 9.0f}, 2.5f},
            {false,
             {-1.5f, 0.0f},
             {-3, 2147483647},
             {0.0f, 0.0f, 0.0f},
             {1.0f, 2.0f, 3.0f, 4.0f},
             {0.0f, 0.0f, kQuarterTurn, kQuarterTurn},
             {255, 0, 128, 0},
             ValueElement::Fire,
             {0.5f, -0.5f, 2.0f},
             0.0f},
            {true, {100.0f, -100.0f}, {0, 0}, {-4.0f, 5.0f, -6.0f}, {-1.0f, -1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 0.0f, 2.0f}, {0, 0, 0, 255}, ValueElement::Earth, {-7.0f, 1.0f, 0.25f}, -4.0f},
            {false, {0.0f, 0.0f}, {1, -1}, {1e10f, -1e10f, 3.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1, 2, 3, 4}, ValueElement::Water, {1.0f, 1.0f, 1.0f}, 1.0f},
        };

        void Apply(ValueWorld& world, const VectorState& state) {
            world.caster.alive = state.alive;
            world.caster.position = state.position;
            world.caster.cell = state.cell;
            world.caster.velocity = state.velocity;
            world.caster.plane = state.plane;
            world.caster.facing = state.facing;
            world.caster.tint = state.tint;
            world.caster.element = state.element;
            world.cast.target = state.target;
            world.cast.power = state.power;
        }

        /// The same bytes: a NaN or a `Color` (which has no `==`) is compared as it is stored
        template <typename T>
        bool SameBytes(const T& first, const T& second) {
            return std::memcmp(&first, &second, sizeof(T)) == 0;
        }

        /// The formula as a `Value<T>` over every state, against the same formula in plain C++
        template <typename T>
        const Value<T> ExpectOverVectors(StringView text, T (*reference)(const ValueUnitStats&, const ValueCastInfo&), ValueKind kind = ValueKind::Program) {
            ValueWorld world;
            const auto compiled = CompileValue<T>(text, world.declaration);
            EXPECT_TRUE(compiled.has_value()) << String(text).c_str() << ": " << (compiled.has_value() ? String() : compiled.error().message).c_str();
            if (!compiled.has_value()) {
                return {};
            }
            EXPECT_EQ(compiled->Kind(), kind) << String(text).c_str();
            for (const VectorState& state : kVectorStates) {
                Apply(world, state);
                const T expected = reference(world.caster, world.cast);
                const T actual = compiled->Evaluate(world.context);
                if constexpr (std::is_same_v<T, PoolString>) {
                    EXPECT_EQ(actual, expected) << String(text).c_str();
                } else {
                    EXPECT_TRUE(SameBytes(actual, expected)) << String(text).c_str();
                }
            }
            return *compiled;
        }

        template <typename T>
        T ConstantImage(const CompiledExpression& compiled) {
            T value{};
            std::memcpy(&value, compiled.constant.bytes.data(), sizeof(T));
            return value;
        }

    }  // namespace

    static_assert(ComponentsOf(ProgramType::Vec4).count == kMaxTypedOperands && ComponentsOf(ProgramType::Quat).count == kMaxTypedOperands &&
                      ComponentsOf(ProgramType::Color).count == kMaxTypedOperands,
                  "a Pack of four is the widest node: the checker and the lowering carry four operands");

    TEST(ProgramCompilerTest, VectorFormulasEqualPlainCode) {
        ExpectOverVectors<Vec3>("caster.velocity * 2 + cast.target", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return caster.velocity * 2.0f + cast.target; });
        ExpectOverVectors<Vec3>("cast.power * caster.velocity - cast.target / 2",
                                [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return cast.power * caster.velocity - cast.target / 2.0f; });
        ExpectOverVectors<Vec3>("-caster.velocity", [](const ValueUnitStats& caster, const ValueCastInfo&) { return -caster.velocity; });
        ExpectOverVectors<Vec3>("lerp(caster.velocity, cast.target, cast.power)",
                                [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return Lerp(caster.velocity, cast.target, cast.power); });
        ExpectOverVectors<Vec3>("normalize(cast.target - caster.velocity)", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return (cast.target - caster.velocity).Normalized(); });
        ExpectOverVectors<float>("length(cast.target - caster.velocity)", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return (cast.target - caster.velocity).Length(); });
        ExpectOverVectors<float>("dot(caster.velocity, cast.target) + 1", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return Dot(caster.velocity, cast.target) + 1.0f; });
        ExpectOverVectors<float>("caster.velocity.x * length(cast.target)", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return caster.velocity.x * cast.target.Length(); });
        ExpectOverVectors<float>("lerp(caster.velocity, cast.target, 0.5).y + (caster.position * 2).x",
                                 [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return Lerp(caster.velocity, cast.target, 0.5f).y + (caster.position * 2.0f).x; });
        ExpectOverVectors<Vec2>("caster.position * 3", [](const ValueUnitStats& caster, const ValueCastInfo&) { return caster.position * 3.0f; });
        ExpectOverVectors<Vec2>("lerp(caster.position, vec2(1, 1), 0.25)", [](const ValueUnitStats& caster, const ValueCastInfo&) { return Lerp(caster.position, Vec2{1.0f, 1.0f}, 0.25f); });
        ExpectOverVectors<Vec4>("caster.plane / 2 + vec4(1, 2, 3, 4)", [](const ValueUnitStats& caster, const ValueCastInfo&) { return caster.plane / 2.0f + Vec4{1.0f, 2.0f, 3.0f, 4.0f}; });
        ExpectOverVectors<Vec4>("vec4(caster.plane.x, caster.position.y, cast.power, 1)",
                                [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return Vec4{caster.plane.x, caster.position.y, cast.power, 1.0f}; });
        ExpectOverVectors<bool>("caster.velocity == cast.target || caster.position != caster.position",
                                [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return caster.velocity == cast.target || caster.position != caster.position; });
    }

    TEST(ProgramCompilerTest, IntegerVectorQuaternionColorEnumAndStringFormulasEqualPlainCode) {
        ExpectOverVectors<IVec2>("ivec2(caster.cell.x + 1, 2)",
                                 [](const ValueUnitStats& caster, const ValueCastInfo&) { return IVec2{static_cast<int32_t>(static_cast<uint32_t>(caster.cell.x) + 1U), 2}; });
        ExpectOverVectors<IVec2>("caster.cell * 2 - caster.cell", [](const ValueUnitStats& caster, const ValueCastInfo&) {
            const auto wrap = [](int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); };
            const auto sub = [](int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); };
            return IVec2{sub(wrap(caster.cell.x, 2), caster.cell.x), sub(wrap(caster.cell.y, 2), caster.cell.y)};
        });
        ExpectOverVectors<Vec3>("caster.facing * cast.target", [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return caster.facing.Rotate(cast.target); });
        ExpectOverVectors<Quat>("caster.facing * quat(0, 0, 0, 1)", [](const ValueUnitStats& caster, const ValueCastInfo&) { return caster.facing * Quat{0.0f, 0.0f, 0.0f, 1.0f}; });
        ExpectOverVectors<Quat>("lerp(caster.facing, quat(0, 0, 1, 0), cast.power)",
                                [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return Slerp(caster.facing, Quat{0.0f, 0.0f, 1.0f, 0.0f}, cast.power); });
        ExpectOverVectors<Quat>("normalize(caster.facing)", [](const ValueUnitStats& caster, const ValueCastInfo&) { return caster.facing.Normalized(); });
        ExpectOverVectors<Color>("lerp(caster.tint, color(0, 0, 0, 255), 0.5)", [](const ValueUnitStats& caster, const ValueCastInfo&) { return Lerp(caster.tint, Color{0, 0, 0, 255}, 0.5f); });
        ExpectOverVectors<Color>("color(caster.tint.r, 300, -4, caster.tint.a)", [](const ValueUnitStats& caster, const ValueCastInfo&) { return Color{caster.tint.r, 255, 0, caster.tint.a}; });
        ExpectOverVectors<int32_t>("caster.tint.g + caster.tint.a", [](const ValueUnitStats& caster, const ValueCastInfo&) { return int32_t{caster.tint.g} + int32_t{caster.tint.a}; });
        ExpectOverVectors<PoolString>("select(caster.element == Fire, 'burn', 'chill')", [](const ValueUnitStats& caster, const ValueCastInfo&) {
            return caster.element == ValueElement::Fire ? PoolString::Intern("burn") : PoolString::Intern("chill");
        });
        ExpectOverVectors<PoolString>("select(caster.alive, 'a', 'b')", [](const ValueUnitStats& caster, const ValueCastInfo&) { return PoolString::Intern(caster.alive ? "a" : "b"); });
        ExpectOverVectors<ValueElement>("select(caster.alive, Fire, caster.element)",
                                        [](const ValueUnitStats& caster, const ValueCastInfo&) { return caster.alive ? ValueElement::Fire : caster.element; });
        ExpectOverVectors<bool>("Earth != caster.element && caster.element == caster.element",
                                [](const ValueUnitStats& caster, const ValueCastInfo&) { return caster.element != ValueElement::Earth; });
        ExpectOverVectors<bool>("caster.title == 'x' || caster.title == ''",
                                [](const ValueUnitStats& caster, const ValueCastInfo&) { return caster.title == PoolString::Intern("x") || caster.title.Empty(); });
    }

    TEST(ProgramCompilerTest, AFormulaOfConstantsOfAnyNewTypeIsAConstant) {
        const ValueWorld world;
        const auto constant = [&](auto tag, StringView text) {
            using T = decltype(tag);
            const auto value = CompileValue<T>(text, world.declaration);
            EXPECT_TRUE(value.has_value()) << String(text).c_str() << ": " << (value.has_value() ? String() : value.error().message).c_str();
            if (value.has_value()) {
                EXPECT_TRUE(value->IsConst()) << String(text).c_str();
            }
            return value.has_value() ? value->GetConst() : T{};
        };
        EXPECT_TRUE(SameBytes(constant(Vec2{}, "vec2(1, 2) * 2"), Vec2{2.0f, 4.0f}));
        EXPECT_TRUE(SameBytes(constant(Vec3{}, "vec3(1, 2, 3) * 2"), Vec3{2.0f, 4.0f, 6.0f}));
        EXPECT_TRUE(SameBytes(constant(Vec3{}, "2 * vec3(1, 2, 3) + vec3(0, 0, 1)"), Vec3{2.0f, 4.0f, 7.0f}));
        EXPECT_TRUE(SameBytes(constant(Vec4{}, "vec4(1, 2, 3, 4) / 2"), Vec4{0.5f, 1.0f, 1.5f, 2.0f}));
        EXPECT_TRUE(SameBytes(constant(IVec2{}, "ivec2(1, 2) * 3"), IVec2{3, 6}));
        EXPECT_TRUE(SameBytes(constant(Quat{}, "quat(0, 0, 0, 1) * quat(0, 0, 0, 1)"), Quat{0.0f, 0.0f, 0.0f, 1.0f}));
        EXPECT_TRUE(SameBytes(constant(Vec3{}, "quat(0, 0, 0, 1) * vec3(1, 0, 0)"), Vec3{1.0f, 0.0f, 0.0f}));
        EXPECT_TRUE(SameBytes(constant(Color{}, "lerp(color(0, 0, 0, 255), color(255, 255, 255, 255), 0.5)"), Lerp(Color{0, 0, 0, 255}, Color{255, 255, 255, 255}, 0.5f)));
        EXPECT_EQ(constant(ValueElement{}, "select(1 < 2, Fire, Earth)"), ValueElement::Fire);
        EXPECT_EQ(constant(ValueElement{}, "Earth"), ValueElement::Earth);
        EXPECT_EQ(constant(PoolString{}, "'chill'"), PoolString::Intern("chill"));
        EXPECT_EQ(constant(PoolString{}, "select(2 > 1, 'burn', 'chill')"), PoolString::Intern("burn"));
        EXPECT_TRUE(constant(bool{}, "'a' == 'a'"));
        EXPECT_FALSE(constant(bool{}, "'a' == 'b'"));
        EXPECT_EQ(constant(float{}, "vec3(1, 2, 3).y"), 2.0f);
        EXPECT_EQ(constant(int32_t{}, "color(1, 2, 3, 4).g"), 2);
        EXPECT_EQ(constant(float{}, "length(vec3(3, 4, 0))"), 5.0f);
        // What the interpreter computes, not what the text says: a channel is clamped, a wrap wraps
        EXPECT_TRUE(SameBytes(constant(Color{}, "color(-1, 0, 255, 256)"), Color{0, 0, 255, 255}));
        EXPECT_TRUE(SameBytes(constant(IVec2{}, "ivec2(2147483647, 0) * 2"), IVec2{-2, 0}));
        const auto compiled = CompileExpression("vec3(1, 2, 3) * 2", world.declaration, DescribeType<Vec3>());
        ASSERT_TRUE(compiled.has_value());
        EXPECT_EQ(compiled->kind, CompiledKind::Constant);
        EXPECT_EQ(compiled->type, ProgramType::Vec3);
        EXPECT_TRUE(SameBytes(ConstantImage<Vec3>(*compiled), Vec3{2.0f, 4.0f, 6.0f}));
        EXPECT_EQ(compiled->constant.bytes[sizeof(Vec3)], 0) << "the image is zero-padded";
    }

    TEST(ProgramCompilerTest, AFormulaThatIsOneFieldOfAnyNewTypeIsASlot) {
        ValueWorld world;
        const auto slot = [&](auto tag, StringView path) {
            using T = decltype(tag);
            const auto value = CompileValue<T>(path, world.declaration);
            EXPECT_TRUE(value.has_value()) << String(path).c_str();
            if (!value.has_value()) {
                return;
            }
            EXPECT_TRUE(value->IsSlot()) << String(path).c_str();
            Value<T> linked;
            EXPECT_TRUE(linked.Link(world.declaration, path).has_value());
            EXPECT_EQ(value->Level(), linked.Level());
            EXPECT_EQ(value->Offset(), linked.Offset());
            EXPECT_EQ(value->Path(), PoolString::Intern(path));
            EXPECT_TRUE(SameBytes(value->Get(world.context), linked.Get(world.context)));
        };
        slot(Vec2{}, "caster.position");
        slot(Vec3{}, "caster.velocity");
        slot(Vec3{}, "cast.target");
        slot(Vec4{}, "caster.plane");
        slot(IVec2{}, "caster.cell");
        slot(Quat{}, "caster.facing");
        slot(Color{}, "caster.tint");
        slot(ValueElement{}, "caster.element");
        slot(PoolString{}, "caster.title");
        slot(Vec2{}, "caster.span.offset");
    }

    TEST(ProgramCompilerTest, AComponentOfOneFieldIsASlotAtTheComponentsOffset) {
        ValueWorld world;
        world.caster.velocity = Vec3{1.0f, 2.0f, 3.0f};
        const auto velocity = LinkSlot(world.declaration, "caster.velocity", DescribeType<Vec3>());
        ASSERT_TRUE(velocity.has_value());
        const auto x = CompileValue<float>("caster.velocity.x", world.declaration);
        const auto y = CompileValue<float>("caster.velocity.y", world.declaration);
        const auto z = CompileValue<float>("caster.velocity.z", world.declaration);
        ASSERT_TRUE(x.has_value() && y.has_value() && z.has_value());
        EXPECT_TRUE(x->IsSlot() && y->IsSlot() && z->IsSlot());
        EXPECT_EQ(x->Offset(), velocity->offset + offsetof(Vec3, x));
        EXPECT_EQ(y->Offset(), velocity->offset + offsetof(Vec3, y));
        EXPECT_EQ(z->Offset(), velocity->offset + offsetof(Vec3, z));
        EXPECT_EQ(y->Offset(), x->Offset() + sizeof(float)) << "the next float";
        EXPECT_EQ(y->Path(), PoolString::Intern("caster.velocity.y"));
        EXPECT_EQ(x->Get(world.context), 1.0f);
        EXPECT_EQ(y->Get(world.context), 2.0f);
        EXPECT_EQ(z->Evaluate(world.context), 3.0f);
        world.caster.velocity.y = -8.0f;
        EXPECT_EQ(y->Get(world.context), -8.0f);

        const auto cell = LinkSlot(world.declaration, "caster.cell", DescribeType<IVec2>());
        ASSERT_TRUE(cell.has_value());
        const auto cellY = CompileValue<int32_t>("caster.cell.y", world.declaration);
        ASSERT_TRUE(cellY.has_value());
        EXPECT_TRUE(cellY->IsSlot());
        EXPECT_EQ(cellY->Offset(), cell->offset + offsetof(IVec2, y));
        EXPECT_EQ(cellY->Get(world.context), world.caster.cell.y);

        const auto offset = LinkSlot(world.declaration, "caster.span.offset", DescribeType<Vec2>());
        ASSERT_TRUE(offset.has_value());
        const auto through = CompileValue<float>("caster.span.offset.y", world.declaration);
        ASSERT_TRUE(through.has_value());
        EXPECT_TRUE(through->IsSlot());
        EXPECT_EQ(through->Offset(), offset->offset + offsetof(Vec2, y));

        const auto quat = CompileValue<float>("caster.facing.w", world.declaration);
        ASSERT_TRUE(quat.has_value());
        EXPECT_TRUE(quat->IsSlot());
        EXPECT_EQ(quat->Get(world.context), world.caster.facing.w);

        // A channel of a color is a byte, so it is computed: a program of one load and one Extract, reading the color's level
        const auto channel = CompileValue<int32_t>("caster.tint.g", world.declaration);
        ASSERT_TRUE(channel.has_value());
        EXPECT_EQ(channel->Kind(), ValueKind::Program);
        EXPECT_EQ(channel->Reads(), LevelBit(0));
        const Program& program = ProgramPool::Get(channel->Id());
        ASSERT_EQ(program.Code().size(), 2U) << program.Disassemble().c_str();
        EXPECT_EQ(program.Code()[0].op, Opcode::LoadSlotColor);
        EXPECT_EQ(program.Code()[1].op, Opcode::ExtractColor);
        EXPECT_EQ(program.Code()[1].imm, 1U);
        EXPECT_EQ(channel->Evaluate(world.context), world.caster.tint.g);
    }

    TEST(ProgramCompilerTest, TheOperandsOfAPackAreInConsecutiveRegisters) {
        ValueWorld world;
        const auto value = CompileValue<Vec4>("vec4(caster.plane.x, cast.power, caster.speed, 1)", world.declaration);
        ASSERT_TRUE(value.has_value()) << value.error().message.c_str();
        ASSERT_EQ(value->Kind(), ValueKind::Program);
        const Program& program = ProgramPool::Get(value->Id());
        const Span<const Instruction> code = program.Code();
        ASSERT_EQ(code.size(), 5U) << program.Disassemble().c_str();
        const Instruction& pack = code.back();
        EXPECT_EQ(pack.op, Opcode::PackVec4);
        EXPECT_EQ(pack.dst, pack.a) << "the result goes where the first component was";
        for (size_t i = 0; i < ComponentsOf(ProgramType::Vec4).count; ++i) {
            EXPECT_EQ(code[i].dst, pack.a + i) << "operand " << i << " is the register above operand " << (i == 0 ? 0 : i - 1) << ": " << program.Disassemble().c_str();
        }
        EXPECT_EQ(program.RegisterCount(), 4U);
        world.cast.power = 5.0f;
        world.caster.speed = 0.5f;
        EXPECT_TRUE(SameBytes(value->Evaluate(world.context), Vec4{world.caster.plane.x, 5.0f, 0.5f, 1.0f}));
    }

    TEST(ProgramCompilerTest, AnOperandThatIsAFourOperandCallKeepsTheRegistersFew) {
        ValueWorld world;
        const auto value = CompileValue<Quat>("quat(vec4(caster.plane.x, caster.plane.y, 0, 1).y, caster.facing.y, caster.facing.z, quat(caster.facing.x, 0, 0, 1).w)", world.declaration);
        ASSERT_TRUE(value.has_value()) << value.error().message.c_str();
        ASSERT_EQ(value->Kind(), ValueKind::Program);
        EXPECT_LE(ProgramPool::Get(value->Id()).RegisterCount(), 8U) << ProgramPool::Get(value->Id()).Disassemble().c_str();
        for (const VectorState& state : kVectorStates) {
            Apply(world, state);
            EXPECT_TRUE(SameBytes(value->Evaluate(world.context), Quat{world.caster.plane.y, world.caster.facing.y, world.caster.facing.z, 1.0f})) << state.plane.y;
        }
        ExpectOverVectors<Vec4>("vec4(vec4(caster.plane.x, 1, 2, 3).w, vec3(caster.velocity.x, 0, cast.power).z, 0, quat(0, 0, caster.facing.z, 1).z)",
                                [](const ValueUnitStats& caster, const ValueCastInfo& cast) { return Vec4{3.0f, cast.power, 0.0f, caster.facing.z}; });
    }

    TEST(ProgramCompilerTest, TheRegisterLimitStillNamesTheNodeOfAPackThatDoesNotFit) {
        const ValueWorld world;
        String text = "caster.plane.x";
        constexpr size_t kLevels = 12;
        for (size_t i = 0; i < kLevels; ++i) {
            text = Format("vec4(1, 2, 3, {}).w", text);
        }
        const size_t count = ProgramPool::Count();
        const auto refused = CompileValue<float>(text, world.declaration);
        ASSERT_FALSE(refused.has_value());
        EXPECT_TRUE(Contains(refused.error().message, "32 registers")) << refused.error().message.c_str();
        EXPECT_GT(refused.error().column, 0U);
        EXPECT_EQ(ProgramPool::Count(), count);
        const auto fits = CompileValue<float>(Format("vec4(1, 2, 3, {}).w", "caster.plane.x"), world.declaration);
        EXPECT_TRUE(fits.has_value());
    }

    TEST(ProgramCompilerTest, ProgramsOfTheNewTypesAreInternedByTheirCode) {
        const ValueWorld world;
        const auto first = CompileValue<Vec3>("caster.velocity * 2 + cast.target", world.declaration);
        const auto respaced = CompileValue<Vec3>("caster.velocity*2+cast.target", world.declaration);
        const auto folded = CompileValue<Vec3>("caster.velocity * (1 + 1) + cast.target", world.declaration);
        ASSERT_TRUE(first.has_value() && respaced.has_value() && folded.has_value());
        EXPECT_EQ(first->Id(), respaced->Id());
        EXPECT_EQ(first->Id(), folded->Id());
        EXPECT_NE(first->Path(), respaced->Path());
        const auto other = CompileValue<Vec3>("caster.velocity * 3 + cast.target", world.declaration);
        ASSERT_TRUE(other.has_value());
        EXPECT_NE(other->Id(), first->Id());

        // A string constant is the bytes of an interned text: the same text is one program, another text another
        const auto burn = CompileValue<PoolString>("select(caster.alive, 'burn', 'chill')", world.declaration);
        const auto again = CompileValue<PoolString>("select(caster.alive,'burn','chill')", world.declaration);
        const auto swapped = CompileValue<PoolString>("select(caster.alive, 'chill', 'burn')", world.declaration);
        ASSERT_TRUE(burn.has_value() && again.has_value() && swapped.has_value());
        EXPECT_EQ(burn->Id(), again->Id());
        EXPECT_NE(burn->Id(), swapped->Id());
        const size_t count = ProgramPool::Count();
        EXPECT_TRUE(CompileValue<PoolString>("select(caster.alive, 'burn', 'chill')", world.declaration).has_value());
        EXPECT_EQ(ProgramPool::Count(), count);

        // An enum is its index: the same code under two enums is one program, and the value casts the index back to its own enum
        const auto element = CompileValue<ValueElement>("select(caster.alive, Fire, Earth)", world.declaration);
        ASSERT_TRUE(element.has_value());
        EXPECT_EQ(ProgramPool::Get(element->Id()).ResultType(), ProgramType::Enum);
    }

    TEST(ProgramCompilerTest, ATypeWithoutAnOperatorIsRefusedWhateverTheValueTypeAsksFor) {
        const ValueWorld world;
        const size_t count = ProgramPool::Count();
        for (const StringView text : {"caster.tint + caster.tint", "caster.facing + caster.facing", "caster.velocity < cast.target", "caster.velocity * cast.target", "caster.cell / 2",
                                      "caster.title + caster.title", "vec3(1, 2)", "caster.velocity.q", "caster.element < Fire", "select(caster.alive, caster.velocity, caster.position)"}) {
            const auto compiled = CompileExpression(text, world.declaration, DescribeType<Vec3>());
            EXPECT_FALSE(compiled.has_value()) << String(text).c_str();
        }
        EXPECT_EQ(ProgramPool::Count(), count);
    }

}  // namespace BECore::Tests
