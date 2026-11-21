// The tooltip text of a Value<T> (Values/Explain/ValueTooltip.h): `{damage}` is the value and the parts of its formula, in the words of the current language of the string table.

#include <BECore/UI/UIStrings.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Explain/TooltipStrings.h>
#include <BECore/Values/Explain/ValueTooltip.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        struct Abilities {
            ContextDecl declaration;
            ValueUnitStats caster;
            ValueCastInfo ability;
            Context context;

            Abilities() {
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(declaration.Add<ValueCastInfo>("ability").has_value());
                context = Context(declaration);
                context.Set(0, caster);
                context.Set(1, ability);
                ability.level = 1;
            }

            Abilities(const Abilities&) = delete;
            Abilities& operator=(const Abilities&) = delete;

            [[nodiscard]] ValueExplanation Explain(StringView formula, bool fuse = true) const {
                const auto compiled = CompileValue<int32_t>(formula, declaration, CompileOptions{.fuse = fuse});
                EXPECT_TRUE(compiled.has_value()) << String(formula).c_str();
                return compiled.has_value() ? ExplainValue(*compiled, context, declaration) : ValueExplanation{};
            }
        };

        /// The string table of the two languages the tests switch between
        UIStrings MakeStrings() {
            UIStrings strings;
            const PoolString en = "en"_intern;
            const PoolString ru = "ru"_intern;
            EXPECT_TRUE(strings.Set(en, "values.per", "{0} per {1}"));
            EXPECT_TRUE(strings.Set(ru, "values.per", "{0} за {1}"));
            EXPECT_TRUE(strings.Set(en, "values.label.ability.level", "level"));
            EXPECT_TRUE(strings.Set(ru, "values.label.ability.level", "уровень"));
            EXPECT_TRUE(strings.Set(en, "tip.fireball", "Deals {damage} damage"));
            return strings;
        }

        String Tooltip(StringView text, const ValueExplanation& damage, const TooltipWords& words) {
            const TooltipBinding bindings[] = {{"damage", &damage}};
            return FormatTooltip(text, Span<const TooltipBinding>(bindings), words);
        }

        const TooltipWords kPlainWords;

    }  // namespace

    TEST(ValueTooltipTest, AFormulaShowsItsValueAndItsPartsInTheWordsOfTheLanguage) {
        const Abilities world;
        UIStrings strings = MakeStrings();
        const ValueExplanation damage = world.Explain("10 + 2 * ability.level");
        ASSERT_TRUE(damage.hasValue);

        EXPECT_EQ(Tooltip("Deals {damage} damage", damage, MakeTooltipWords(strings)), "Deals 12 (10 + 2 per level) damage");
        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        EXPECT_EQ(Tooltip("Deals {damage} damage", damage, MakeTooltipWords(strings)), "Deals 12 (10 + 2 за уровень) damage");
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        EXPECT_EQ(Tooltip("Deals {damage} damage", damage, MakeTooltipWords(strings)), "Deals 12 (10 + 2 per level) damage");
    }

    TEST(ValueTooltipTest, ALabelTheLanguageLacksFallsBackToThePath) {
        const Abilities world;
        UIStrings strings;
        ASSERT_TRUE(strings.Set("en"_intern, "values.per", "{0} per {1}"));
        EXPECT_EQ(Tooltip("{damage}", world.Explain("10 + 2 * ability.level"), MakeTooltipWords(strings)), "12 (10 + 2 per ability.level)");

        const UIStrings empty;
        EXPECT_EQ(Tooltip("{damage}", world.Explain("10 + 2 * ability.level"), MakeTooltipWords(empty)), "12 (10 + 2 per ability.level)") << "the default pattern";
    }

    TEST(ValueTooltipTest, ATemplateTheStringTableResolvedFirstWorks) {
        const Abilities world;
        UIStrings strings = MakeStrings();
        const String resolved = strings.Resolve("{#tip.fireball}");
        EXPECT_EQ(resolved, "Deals {damage} damage");
        EXPECT_EQ(Tooltip(resolved, world.Explain("10 + 2 * ability.level"), MakeTooltipWords(strings)), "Deals 12 (10 + 2 per level) damage");
    }

    TEST(ValueTooltipTest, AConstAndASlotGiveThePlainValue) {
        const Abilities world;
        const ValueExplanation constant = ExplainValue(Value<int32_t>(12), world.context, world.declaration);
        EXPECT_EQ(Tooltip("{damage}", constant, kPlainWords), "12");
        Value<int32_t> slot;
        ASSERT_TRUE(slot.Link(world.declaration, "caster.attack").has_value());
        EXPECT_EQ(Tooltip("{damage}", ExplainValue(slot, world.context, world.declaration), kPlainWords), "12");
    }

    TEST(ValueTooltipTest, ValueOnlyAndBracesAndUnknownNames) {
        const Abilities world;
        const ValueExplanation damage = world.Explain("10 + 2 * ability.level");
        EXPECT_EQ(Tooltip("{damage:value} dmg", damage, kPlainWords), "12 dmg");
        EXPECT_EQ(Tooltip("{{damage}} is {damage:value}", damage, kPlainWords), "{damage} is 12");
        EXPECT_EQ(Tooltip("a }} b {{ c", damage, kPlainWords), "a } b { c");
        EXPECT_EQ(Tooltip("{heal} {damage:value} {heal:value}", damage, kPlainWords), "{heal} 12 {heal:value}") << "an unknown name is never an empty hole";
        EXPECT_EQ(Tooltip("{damage", damage, kPlainWords), "{damage");
        EXPECT_EQ(Tooltip("{damage {damage:value}", damage, kPlainWords), "{damage 12");
        EXPECT_EQ(Tooltip("}", damage, kPlainWords), "}");
        EXPECT_EQ(Tooltip("{}", damage, kPlainWords), "{}");
        EXPECT_EQ(Tooltip("", damage, kPlainWords), "");
    }

    TEST(ValueTooltipTest, APartIsTheFormulaWithTheParenthesesItsPrecedenceNeeds) {
        const Abilities world;
        EXPECT_EQ(Tooltip("{damage}", world.Explain("(ability.level + 1) * caster.attack"), kPlainWords), "24 ((ability.level + 1) * caster.attack)");
        EXPECT_EQ(Tooltip("{damage}", world.Explain("caster.attack - (ability.level - 1)"), kPlainWords), "12 (caster.attack - (ability.level - 1))");
        EXPECT_EQ(Tooltip("{damage}", world.Explain("caster.attack - ability.level - 1"), kPlainWords), "10 (caster.attack - ability.level - 1)");
        EXPECT_EQ(Tooltip("{damage}", world.Explain("max(caster.attack, 20) + int(ability.power)"), kPlainWords), "22 (max(caster.attack, 20) + int(ability.power))");
        EXPECT_EQ(Tooltip("{damage}", world.Explain("-caster.attack + 30"), kPlainWords), "18 (-caster.attack + 30)");
    }

    TEST(ValueTooltipTest, AFieldTimesANumberIsThePerPatternInEitherOrderAndWhenFused) {
        const Abilities world;
        EXPECT_EQ(Tooltip("{damage}", world.Explain("ability.level * 2 + 10"), kPlainWords), "12 (2 per ability.level + 10)");
        EXPECT_EQ(Tooltip("{damage}", world.Explain("ability.level * 2 + 10", false), kPlainWords), "12 (2 per ability.level + 10)");
        EXPECT_EQ(Tooltip("{damage}", world.Explain("10 + ability.level * 3"), kPlainWords), "13 (10 + 3 per ability.level)");
        EXPECT_EQ(Tooltip("{damage}", world.Explain("caster.attack * ability.level"), kPlainWords), "12 (caster.attack * ability.level)") << "two fields are a product";
    }

    TEST(ValueTooltipTest, ALongFormulaShowsItsValueOnly) {
        const Abilities world;
        // 32 terms, a balanced tree: a chain this long is refused for its height
        String formula = "caster.attack";
        for (int32_t i = 0; i < 5; ++i) {
            formula = Format("({}) + ({})", formula, formula);
        }
        EXPECT_EQ(Tooltip("{damage}", world.Explain(formula), kPlainWords), "384");
    }

    TEST(ValueTooltipTest, AValueWithoutAContextIsAQuestionMark) {
        const Abilities world;
        Context partial(world.declaration);
        partial.Set(0, world.caster);
        const auto compiled = CompileValue<int32_t>("10 + 2 * ability.level", world.declaration);
        ASSERT_TRUE(compiled.has_value());
        const ValueExplanation damage = ExplainValue(*compiled, partial, world.declaration);
        EXPECT_EQ(Tooltip("{damage} / {damage:value}", damage, kPlainWords), "? / ?");
    }

}  // namespace BECore::Tests
