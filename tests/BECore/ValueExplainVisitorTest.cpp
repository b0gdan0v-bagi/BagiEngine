// The inspector's hook for a Value<T> field (IPropertyVisitor::VisitValueField): what a visitor is told for each kind and that the explanation it can ask for is the one of ExplainValue.

#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Explain/ValueExplain.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <Common/NullPropertyVisitor.h>
#include <algorithm>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        struct SeenField {
            String compound;
            String kind;
            String source;
            ValueExplanation explanation;
        };

        /// Records, for each `VisitValueField`, the compound it came in and the explanation of the value against the world's context, and the numbers visited as the literal of a `Const`
        class RecordingVisitor final : public NullPropertyVisitor {
        public:
            explicit RecordingVisitor(const ValueWorld& world) : _world(world) {}

            Vector<SeenField> fields;
            Vector<String> literals;
            bool openCompounds = true;

            bool BeginCompound(StringView name) override {
                _compound = String(name);
                return openCompounds;
            }

            void VisitValueField(StringView, const ValueFieldInfo& info) override {
                SeenField seen{_compound, String(info.kind), String(info.source), {}};
                info.explain(_world.context, _world.declaration, seen.explanation);
                fields.push_back(std::move(seen));
            }

            bool Visit(StringView name, float&) override {
                literals.push_back(_compound + "." + String(name));
                return false;
            }

        private:
            const ValueWorld& _world;
            String _compound;
        };

        class ValueExplainVisitorTest : public ::testing::Test {
        protected:
            void SetUp() override {
                const auto damage = CompileValue<int32_t>("caster.attack * 2 + cast.level", world.declaration);
                ASSERT_TRUE(damage.has_value());
                document.damage = *damage;
                ASSERT_TRUE(document.aim.Link(world.declaration, "caster.velocity").has_value());
                document.power = 2.5f;
            }

            const SeenField* Find(const RecordingVisitor& visitor, StringView compound) const {
                for (const SeenField& field : visitor.fields) {
                    if (field.compound == compound) {
                        return &field;
                    }
                }
                return nullptr;
            }

            ValueWorld world;
            ValueDocument document;
        };

    }  // namespace

    TEST_F(ValueExplainVisitorTest, EveryValueFieldIsToldItsKindAndSource) {
        RecordingVisitor visitor(world);
        document.AcceptPropertyVisitor(visitor);

        const SeenField* constant = Find(visitor, "power");
        ASSERT_NE(constant, nullptr);
        EXPECT_EQ(constant->kind, "Const");
        EXPECT_TRUE(constant->source.empty());

        const SeenField* slot = Find(visitor, "aim");
        ASSERT_NE(slot, nullptr);
        EXPECT_EQ(slot->kind, "Slot");
        EXPECT_EQ(slot->source, "caster.velocity");

        const SeenField* program = Find(visitor, "damage");
        ASSERT_NE(program, nullptr);
        EXPECT_EQ(program->kind, "Program");
        EXPECT_NE(program->source.find("caster.attack"), String::npos);
    }

    TEST_F(ValueExplainVisitorTest, TheExplainOfTheInfoGivesTheExplanationOfTheValue) {
        RecordingVisitor visitor(world);
        document.AcceptPropertyVisitor(visitor);

        const SeenField* program = Find(visitor, "damage");
        ASSERT_NE(program, nullptr);
        const ValueExplanation expected = ExplainValue(document.damage, world.context, world.declaration);
        EXPECT_EQ(program->explanation.kind, ValueKind::Program);
        EXPECT_TRUE(program->explanation.hasValue);
        EXPECT_EQ(program->explanation.valueText, expected.valueText);
        EXPECT_EQ(program->explanation.valueText, "27");
        EXPECT_NE(program->explanation.trace.root, kNoTraceNode);

        const SeenField* slot = Find(visitor, "aim");
        ASSERT_NE(slot, nullptr);
        EXPECT_EQ(slot->explanation.valueText, "(1, 2, 3)");
    }

    TEST_F(ValueExplainVisitorTest, AContextWithoutTheLevelGivesNoValueAndNoAssert) {
        const ContextDecl& declaration = world.declaration;
        const Context bare(declaration);
        ValueExplanation explanation;
        const auto explain = [&](const Context& context, const ContextDecl& decl, ValueExplanation& out) { out = ExplainValue(document.damage, context, decl); };
        const ValueFieldInfo info{"Program", "", explain};
        info.explain(bare, declaration, explanation);
        EXPECT_FALSE(explanation.hasValue);
        EXPECT_FALSE(explanation.why.empty());
    }

    TEST_F(ValueExplainVisitorTest, AConstStillVisitsItsLiteralAndASlotOrProgramVisitsNone) {
        RecordingVisitor visitor(world);
        document.AcceptPropertyVisitor(visitor);
        // `power` is a Const float; `aim` is a Slot of Vec3, whose compound the editor shows empty, as before
        EXPECT_NE(std::find(visitor.literals.begin(), visitor.literals.end(), String("power.value")), visitor.literals.end());
        EXPECT_EQ(std::find(visitor.literals.begin(), visitor.literals.end(), String("aim.value")), visitor.literals.end());
    }

    TEST_F(ValueExplainVisitorTest, AClosedCompoundIsNotExplained) {
        RecordingVisitor visitor(world);
        visitor.openCompounds = false;
        document.AcceptPropertyVisitor(visitor);
        EXPECT_TRUE(visitor.fields.empty());
    }

}  // namespace BECore::Tests
