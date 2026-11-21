// The explanation of a Value<T> (Values/Explain/ValueExplain.h): kind, source, the value now as text and the trace, for each kind of value, and its erased form for the control API.

#include <BECore/ProgramDomainTypes.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Explain/ValueExplain.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ProgramDomainData;
    using namespace ValueTestData;

    namespace {

        constexpr StringView kMage = "Mage";

        struct World : ValueWorld {
            World() {
                caster.title = PoolString::Intern(kMage);
            }
        };

        template <typename T>
        Value<T> Compile(const ContextDecl& declaration, StringView text) {
            const auto compiled = CompileValue<T>(text, declaration);
            EXPECT_TRUE(compiled.has_value()) << String(text).c_str() << ": " << (compiled.has_value() ? "" : compiled.error().message.c_str());
            EXPECT_TRUE(compiled.has_value() && compiled->Kind() == ValueKind::Program) << String(text).c_str();
            return compiled.has_value() ? *compiled : Value<T>{};
        }

        template <typename T>
        Value<T> Link(const ContextDecl& declaration, StringView path) {
            Value<T> value;
            const auto linked = value.Link(declaration, path);
            EXPECT_TRUE(linked.has_value()) << String(path).c_str();
            return value;
        }

        /// The explanation says what `Evaluate` gives, as text, and its trace ends in the same value
        template <typename T>
        void ExpectExplains(const Value<T>& value, const World& world, ValueKind kind) {
            const ValueExplanation explanation = ExplainValue(value, world.context, world.declaration);
            EXPECT_EQ(explanation.kind, kind);
            ASSERT_TRUE(explanation.hasValue) << explanation.why.c_str();
            EXPECT_EQ(explanation.valueText, FormatValue(value.Evaluate(world.context)));
            EXPECT_TRUE(explanation.why.empty());
            if (kind != ValueKind::Const) {
                ASSERT_NE(explanation.trace.root, kNoTraceNode);
                EXPECT_EQ(FormatNodeValue(explanation.trace.nodes[explanation.trace.root]), explanation.valueText);
            } else {
                EXPECT_TRUE(explanation.trace.nodes.empty());
            }
        }

    }  // namespace

    TEST(ValueExplainTest, AConstIsItsLiteralWithNoTrace) {
        const World world;
        const ValueExplanation number = ExplainValue(Value<int32_t>(12), world.context, world.declaration);
        EXPECT_EQ(number.kind, ValueKind::Const);
        EXPECT_EQ(number.source, "12");
        EXPECT_TRUE(number.hasValue);
        EXPECT_EQ(number.valueText, "12");
        EXPECT_TRUE(number.trace.nodes.empty());

        EXPECT_EQ(ExplainValue(Value<bool>(true), world.context, world.declaration).valueText, "true");
        EXPECT_EQ(ExplainValue(Value<Vec3>(Vec3{1.0f, 2.0f, 3.0f}), world.context, world.declaration).valueText, "(1, 2, 3)");
        EXPECT_EQ(ExplainValue(Value<ValueElement>(ValueElement::Earth), world.context, world.declaration).valueText, "Earth");
        EXPECT_EQ(ExplainValue(Value<PoolString>(PoolString::Intern(kMage)), world.context, world.declaration).valueText, "\"Mage\"");
    }

    TEST(ValueExplainTest, ASlotIsOneContextKeyNodeAndItsValueIsTheFieldsNow) {
        World world;
        const Value<int32_t> attack = Link<int32_t>(world.declaration, "caster.attack");
        ExpectExplains(attack, world, ValueKind::Slot);

        const ValueExplanation explanation = ExplainValue(attack, world.context, world.declaration);
        EXPECT_EQ(explanation.source, "caster.attack");
        ASSERT_EQ(explanation.trace.nodes.size(), 1U);
        EXPECT_EQ(explanation.trace.nodes[0].kind, TraceStepKind::ContextKey);
        EXPECT_EQ(explanation.trace.nodes[0].label, "caster.attack");
        EXPECT_EQ(explanation.valueText, "12");

        world.caster.attack = 30;
        EXPECT_EQ(ExplainValue(attack, world.context, world.declaration).valueText, "30");
        EXPECT_EQ(ExplainValue(Link<ValueElement>(world.declaration, "caster.element"), world.context, world.declaration).valueText, "Water");
        EXPECT_EQ(ExplainValue(Link<PoolString>(world.declaration, "caster.title"), world.context, world.declaration).valueText, "\"Mage\"");
    }

    TEST(ValueExplainTest, AProgramGivesTheTreeAndTheValueOfEvaluate) {
        World world;
        ExpectExplains(Compile<int32_t>(world.declaration, "caster.attack * 2 + cast.level"), world, ValueKind::Program);
        ExpectExplains(Compile<float>(world.declaration, "cast.power * 2.0"), world, ValueKind::Program);
        ExpectExplains(Compile<double>(world.declaration, "caster.precise + 1.5"), world, ValueKind::Program);
        ExpectExplains(Compile<bool>(world.declaration, "caster.attack > 5 && caster.alive"), world, ValueKind::Program);
        ExpectExplains(Compile<Vec3>(world.declaration, "caster.velocity + cast.target"), world, ValueKind::Program);
        ExpectExplains(Compile<ValueElement>(world.declaration, "select(caster.alive, Fire, caster.element)"), world, ValueKind::Program);
        ExpectExplains(Compile<PoolString>(world.declaration, "select(caster.alive, caster.title, caster.title)"), world, ValueKind::Program);

        const ValueExplanation explanation = ExplainValue(Compile<int32_t>(world.declaration, "caster.attack * 2 + cast.level"), world.context, world.declaration);
        EXPECT_EQ(explanation.source, "caster.attack * 2 + cast.level");
        EXPECT_EQ(explanation.valueText, "27");
        EXPECT_NE(FormatTrace(explanation.trace).find("caster.attack = 12"), String::npos);
    }

    TEST(ValueExplainTest, AnEnumResultIsNamedAfterTheTypeOfTheValue) {
        const World world;
        const ValueExplanation explanation = ExplainValue(Compile<ValueElement>(world.declaration, "select(caster.alive, Fire, Earth)"), world.context, world.declaration);
        EXPECT_EQ(explanation.valueText, "Fire");
        EXPECT_EQ(FormatNodeValue(explanation.trace.nodes[explanation.trace.root]), "Fire");
    }

    TEST(ValueExplainTest, AValueOfARegisteredTypeIsPrintedByItsDescriptor) {
        static_cast<void>(RegisterDomainToys());
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<DomainBoard>("board").has_value());
        DomainBoard board;
        board.walls.words[0] = 0xF0;
        Context context(declaration);
        context.Set(0, board);

        const ValueExplanation slot = ExplainValue(Link<DomainBits256>(declaration, "board.walls"), context, declaration);
        EXPECT_EQ(slot.valueText.rfind("<mask 32 bytes: f0", 0), 0U) << slot.valueText.c_str();
        const Value<DomainBits256> program = Compile<DomainBits256>(declaration, "or_mask(board.walls, bit(8))");
        const ValueExplanation explanation = ExplainValue(program, context, declaration);
        ASSERT_TRUE(explanation.hasValue);
        EXPECT_EQ(explanation.valueText, FormatValue(program.Evaluate(context)));
        EXPECT_EQ(explanation.trace.nodes[explanation.trace.root].kind, TraceStepKind::Call);
    }

    TEST(ValueExplainTest, ATypeNoProgramComputesIsShownByItsBytes) {
        const World world;
        const Value<Rect> bounds = Link<Rect>(world.declaration, "caster.bounds");
        const ValueExplanation slot = ExplainValue(bounds, world.context, world.declaration);
        EXPECT_TRUE(slot.hasValue);
        EXPECT_EQ(slot.valueText, FormatValue(world.caster.bounds));
        EXPECT_EQ(slot.valueText.rfind("<", 0), 0U);
        EXPECT_NE(slot.valueText.find(Format(" {} bytes: ", sizeof(Rect))), String::npos) << slot.valueText.c_str();
        EXPECT_TRUE(slot.trace.nodes.empty());
        EXPECT_EQ(ExplainValue(Value<ValuePoint>(ValuePoint{6, 7}), world.context, world.declaration).valueText, FormatValue(ValuePoint{6, 7}));
    }

    TEST(ValueExplainTest, AProgramKindThatCannotExistHasNoValueAndSaysWhy) {
        const World world;
        const Value<int32_t> program = Compile<int32_t>(world.declaration, "caster.attack + 1");
        const ValueExplanation rect = ExplainValue(Value<Rect>::FromProgram(program.Id(), "forged"_intern), world.context, world.declaration);
        EXPECT_FALSE(rect.hasValue);
        EXPECT_FALSE(rect.why.empty());
        EXPECT_TRUE(rect.trace.nodes.empty());

        const ValueExplanation wrongType = ExplainValue(Value<float>::FromProgram(program.Id(), "forged"_intern), world.context, world.declaration);
        EXPECT_FALSE(wrongType.hasValue);
        EXPECT_NE(wrongType.why.find("Int32"), String::npos) << wrongType.why.c_str();
    }

    TEST(ValueExplainTest, AContextThatLacksALevelGivesNoValueAndNoAssertion) {
        const World world;
        Context partial(world.declaration);
        partial.Set(1, world.cast);

        const ValueExplanation slot = ExplainValue(Link<int32_t>(world.declaration, "caster.attack"), partial, world.declaration);
        EXPECT_FALSE(slot.hasValue);
        EXPECT_EQ(slot.why, "the context has no object at level 'caster'");
        EXPECT_EQ(slot.source, "caster.attack");

        const ValueExplanation program = ExplainValue(Compile<int32_t>(world.declaration, "caster.attack + cast.level"), partial, world.declaration);
        EXPECT_FALSE(program.hasValue);
        EXPECT_EQ(program.why, "the context has no object at level 'caster'");
        EXPECT_TRUE(program.valueText.empty());

        const ValueExplanation constant = ExplainValue(Value<int32_t>(4), Context{}, world.declaration);
        EXPECT_TRUE(constant.hasValue);
    }

    TEST(ValueExplainTest, ExplainingChangesNothingAndTheEvaluationAllocatesNothingAround) {
        const World world;
        const Value<int32_t> program = Compile<int32_t>(world.declaration, "caster.attack * 2 + cast.level");
        const int32_t before = program.Evaluate(world.context);
        ExpectNoAllocations([&] { EXPECT_EQ(program.Evaluate(world.context), before); });
        const ValueExplanation explanation = ExplainValue(program, world.context, world.declaration);
        EXPECT_EQ(Format("{}", before), explanation.valueText);
        ExpectNoAllocations([&] { EXPECT_EQ(program.Evaluate(world.context), before); });
    }

    TEST(ValueExplainTest, TheErasedFormFindsAValueByItsTypeAndMatchesTheTypedCall) {
        RegisterBuiltinValueExplainers();
        const World world;
        const Value<float> power = Compile<float>(world.declaration, "cast.power * 2.0");
        const ValueExplainFn explain = ValueExplainers::GetInstance().Find(DescribeType<Value<float>>().opaqueHash);
        ASSERT_NE(explain, nullptr);
        const ValueExplanation erased = explain(&power, world.context, world.declaration);
        const ValueExplanation typed = ExplainValue(power, world.context, world.declaration);
        EXPECT_EQ(erased.valueText, typed.valueText);
        EXPECT_EQ(erased.source, typed.source);
        EXPECT_EQ(FormatTrace(erased.trace), FormatTrace(typed.trace));

        EXPECT_NE(ValueExplainers::GetInstance().Find(DescribeType<Value<Rect>>().opaqueHash), nullptr);
        EXPECT_NE(ValueExplainers::GetInstance().Find(DescribeType<Value<PoolString>>().opaqueHash), nullptr);
        EXPECT_NE(ValueExplainers::GetInstance().Find(DescribeType<Value<Vec3>>().opaqueHash), nullptr);
        EXPECT_NE(DescribeType<Value<float>>().opaqueHash, DescribeType<Value<int32_t>>().opaqueHash);
    }

    TEST(ValueExplainTest, AGamesOwnTypeIsRegisteredWithRegister) {
        ValueExplainers& explainers = ValueExplainers::GetInstance();
        explainers.Register<ValueElement>();
        explainers.Register<ValuePoint>();
        const World world;
        const Value<ValueElement> element = Link<ValueElement>(world.declaration, "caster.element");
        const ValueExplainFn explain = explainers.Find(DescribeType<Value<ValueElement>>().opaqueHash);
        ASSERT_NE(explain, nullptr);
        EXPECT_EQ(explain(&element, world.context, world.declaration).valueText, "Water");
        EXPECT_NE(explainers.Find(DescribeType<Value<ValuePoint>>().opaqueHash), nullptr);
        EXPECT_EQ(explainers.Find(0), nullptr);
    }

}  // namespace BECore::Tests
