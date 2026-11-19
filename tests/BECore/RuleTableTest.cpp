#include <BECore/RuleTestSupport.h>
#include <BECore/Rules/RuleTable.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kKinds = 4;

        /// A value without a default constructor.
        struct Effect {
            explicit Effect(int power, bool pierce = false) : power(power), pierce(pierce) {}
            bool operator==(const Effect&) const = default;
            int power;
            bool pierce;
        };

    }  // namespace

    TEST(RuleTableTest, TheComboTableIsWhatTheThreeRulesSay) {
        const auto baked = MakeComboTable().Bake("none");
        ASSERT_TRUE(baked.has_value()) << baked.error().message.c_str();

        // bomb, color_bomb, rocket, gem on both axes.
        const char* const expected[kKinds][kKinds] = {
            {"merge", "super", "merge", "none"},
            {"super", "clear_all", "clear_all", "none"},
            {"merge", "clear_all", "merge", "none"},
            {"none", "none", "none", "none"},
        };
        for (uint32_t a = 0; a < kKinds; ++a) {
            for (uint32_t b = 0; b < kKinds; ++b) {
                EXPECT_EQ(baked->Lookup(a, b), String(expected[a][b])) << a << " x " << b;
            }
        }
    }

    TEST(RuleTableTest, ASymmetricRuleAddsItsMirrorAndAnOwnMirrorOnce) {
        RuleTable<String> table(ComboAxes());
        const auto first = table.AddSymmetric("tag:special", "color_bomb", "x", "one");
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(*first, 0u);
        EXPECT_EQ(table.RuleCount(), 2u);
        const auto own = table.AddSymmetric("tag:special", "tag:special", "y", "two");
        ASSERT_TRUE(own.has_value());
        EXPECT_EQ(*own, 2u);
        EXPECT_EQ(table.RuleCount(), 3u);
        const auto plain = table.AddRule({"gem", "gem"}, "z", "three");
        ASSERT_TRUE(plain.has_value());
        EXPECT_EQ(*plain, 3u);
    }

    TEST(RuleTableTest, TheMirroredRuleIsNamedInAMessage) {
        Vector<RuleAxis> axes;
        axes.push_back(MakeNamedAxis("first", {"p", "q"}));
        axes.push_back(MakeNamedAxis("second", {"p", "q"}));
        RuleTable<int> table(std::move(axes));
        ASSERT_TRUE(table.AddSymmetric("p", "*", 1, "sym.xml").has_value());
        ASSERT_TRUE(table.AddRule({"*", "p"}, 2, "plain.xml").has_value());
        const auto baked = table.Bake(0);
        ASSERT_FALSE(baked.has_value());
        EXPECT_TRUE(Contains(baked.error().message, "(mirrored)")) << baked.error().message.c_str();
        EXPECT_TRUE(Contains(baked.error().message, "sym.xml")) << baked.error().message.c_str();
        EXPECT_TRUE(Contains(baked.error().message, "plain.xml")) << baked.error().message.c_str();
        EXPECT_TRUE(Contains(baked.error().message, "first=p second=*")) << baked.error().message.c_str();
    }

    TEST(RuleTableTest, ASymmetricRuleNeedsTwoAxesOfOneDomain) {
        Vector<RuleAxis> axes;
        axes.push_back(MakeNamedAxis("kind", {"a", "b"}));
        axes.push_back(MakeNamedAxis("verb", {"a", "c"}));
        RuleTable<int> table(std::move(axes));
        const auto different = table.AddSymmetric("a", "a", 1, "sym.xml");
        ASSERT_FALSE(different.has_value());
        EXPECT_TRUE(Contains(different.error().message, "sym.xml")) << different.error().message.c_str();
        EXPECT_EQ(table.RuleCount(), 0u);

        Vector<RuleAxis> single;
        single.push_back(MakeNamedAxis("kind", {"a", "b"}));
        RuleTable<int> one(std::move(single));
        EXPECT_FALSE(one.AddSymmetric("a", "b", 1, "sym.xml").has_value());
        EXPECT_EQ(one.RuleCount(), 0u);
    }

    TEST(RuleTableTest, ASymmetricRuleWithABadSelectorAddsNothing) {
        RuleTable<int> table(ComboAxes());
        const auto result = table.AddSymmetric("tag:special", "no_such_kind", 1, "bad.xml");
        ASSERT_FALSE(result.has_value());
        EXPECT_TRUE(Contains(result.error().message, "no_such_kind")) << result.error().message.c_str();
        EXPECT_EQ(table.RuleCount(), 0u);
    }

    TEST(RuleTableTest, ACellNoRuleMatchesHoldsTheDefaultAndHasNoWinner) {
        const auto baked = MakeComboTable().Bake("none");
        ASSERT_TRUE(baked.has_value());
        EXPECT_EQ(baked->Lookup(3, 3), String("none"));
        EXPECT_FALSE(baked->Winner(3, 3).has_value());
        EXPECT_EQ(baked->DefaultValue(), String("none"));
        // (bomb, color_bomb) is won by rule `bomb x color_bomb`, which is rule number 2 (rules 0 and 1 are the first pair).
        ASSERT_TRUE(baked->Winner(0, 1).has_value());
        EXPECT_EQ(*baked->Winner(0, 1), 2u);
        ASSERT_TRUE(baked->Winner(1, 0).has_value());
        EXPECT_EQ(*baked->Winner(1, 0), 3u);
    }

    TEST(RuleTableTest, LookupIsTheRowMajorIndexIntoTheCells) {
        const auto baked = MakeComboTable().Bake("none");
        ASSERT_TRUE(baked.has_value());
        ASSERT_EQ(baked->CellCount(), kKinds * kKinds);
        for (uint32_t a = 0; a < kKinds; ++a) {
            for (uint32_t b = 0; b < kKinds; ++b) {
                EXPECT_EQ(baked->CellIndex(a, b), a * kKinds + b);
                EXPECT_EQ(&baked->Lookup(a, b), &baked->LookupCell(a * kKinds + b));
            }
        }
        EXPECT_EQ(baked->Axes().size(), 2u);
        EXPECT_EQ(baked->Axes()[0].Name(), StringView("first"));
    }

    TEST(RuleTableTest, ThreeAxesAreRowMajorToo) {
        Vector<RuleAxis> axes;
        axes.push_back(MakeNamedAxis("a", {"a0", "a1"}));
        axes.push_back(MakeNamedAxis("b", {"b0", "b1", "b2"}));
        axes.push_back(MakeNamedAxis("c", {"c0", "c1", "c2", "c3"}));
        RuleTable<int> table(std::move(axes));
        ASSERT_TRUE(table.AddRule({"a1", "b2", "c3"}, 7, "last").has_value());
        ASSERT_TRUE(table.AddRule({"a1", "b0", "c1"}, 5, "middle").has_value());
        const auto baked = table.Bake(-1);
        ASSERT_TRUE(baked.has_value());
        EXPECT_EQ(baked->CellCount(), 24u);
        EXPECT_EQ(baked->Lookup(1, 2, 3), 7);
        EXPECT_EQ(baked->LookupCell(23), 7);
        EXPECT_EQ(baked->LookupCell(13), 5);
        EXPECT_EQ(baked->Lookup(size_t{0}, uint8_t{1}, 2), -1);
    }

    TEST(RuleTableTest, AValueOfAStructWithEqualityNeedsNoDefaultConstructor) {
        Vector<RuleAxis> axes;
        axes.push_back(MakeNamedAxis("kind", {"a", "b", "c"}));
        RuleTable<Effect> table(std::move(axes));
        EXPECT_FALSE(table.AddRule({"tag:nothing"}, Effect(1), "x").has_value());
        ASSERT_TRUE(table.AddRule({"a"}, Effect(3, true), "one").has_value());
        ASSERT_TRUE(table.AddRule({"a"}, Effect(3, true), "two").has_value());
        ASSERT_TRUE(table.AddRule({"*"}, Effect(2), "three").has_value());
        const auto baked = table.Bake(Effect(0));
        ASSERT_TRUE(baked.has_value()) << baked.error().message.c_str();
        EXPECT_EQ(baked->Lookup(0), Effect(3, true));
        EXPECT_EQ(baked->Lookup(1), Effect(2));
        ASSERT_TRUE(baked->Winner(0).has_value());
        EXPECT_EQ(*baked->Winner(0), 0u);  // of two equal rules the first declared

        // A value that differs in one field is another value, so the same two rules now clash.
        ASSERT_TRUE(table.AddRule({"a"}, Effect(3, false), "four").has_value());
        const auto clash = table.Bake(Effect(0));
        ASSERT_FALSE(clash.has_value());
        EXPECT_TRUE(Contains(clash.error().message, "four")) << clash.error().message.c_str();
    }

    TEST(RuleTableTest, ARuleWithAnUnknownNameIsAnErrorWithItsOriginAndAddsNothing) {
        RuleTable<int> table(ComboAxes());
        ASSERT_TRUE(table.AddRule({"bomb", "gem"}, 1, "ok.xml").has_value());
        const auto bad = table.AddRule({"bomb", "banana"}, 2, "rules.xml:12");
        ASSERT_FALSE(bad.has_value());
        EXPECT_TRUE(Contains(bad.error().message, "rules.xml:12")) << bad.error().message.c_str();
        EXPECT_TRUE(Contains(bad.error().message, "banana")) << bad.error().message.c_str();
        EXPECT_TRUE(Contains(bad.error().message, "second")) << bad.error().message.c_str();
        EXPECT_EQ(table.RuleCount(), 1u);
        const auto next = table.AddRule({"gem", "gem"}, 3, "next.xml");
        ASSERT_TRUE(next.has_value());
        EXPECT_EQ(*next, 1u);
    }

    TEST(RuleTableTest, AWrongNumberOfSelectorsIsAnError) {
        RuleTable<int> table(ComboAxes());
        const auto tooFew = table.AddRule({"bomb"}, 1, "few.xml");
        ASSERT_FALSE(tooFew.has_value());
        EXPECT_TRUE(Contains(tooFew.error().message, "few.xml")) << tooFew.error().message.c_str();
        EXPECT_FALSE(table.AddRule({"bomb", "gem", "gem"}, 1, "many.xml").has_value());
        EXPECT_EQ(table.RuleCount(), 0u);
    }

    TEST(RuleTableTest, ATableWithNoRulesIsAllDefault) {
        const auto baked = RuleTable<int>(ComboAxes()).Bake(42);
        ASSERT_TRUE(baked.has_value());
        EXPECT_EQ(baked->RuleCount(), 0u);
        for (uint32_t cell = 0; cell < baked->CellCount(); ++cell) {
            EXPECT_EQ(baked->LookupCell(cell), 42);
        }
    }

    TEST(RuleTableTest, WonCellsAddUpToTheCellsWithAWinner) {
        const auto baked = MakeComboTable().Bake("none");
        ASSERT_TRUE(baked.has_value());
        uint32_t won = 0;
        for (uint32_t rule = 0; rule < baked->RuleCount(); ++rule) {
            won += baked->WonCells(rule);
        }
        uint32_t withWinner = 0;
        for (uint32_t a = 0; a < kKinds; ++a) {
            for (uint32_t b = 0; b < kKinds; ++b) {
                withWinner += baked->Winner(a, b).has_value() ? 1 : 0;
            }
        }
        EXPECT_EQ(won, withWinner);
        EXPECT_EQ(withWinner, 9u);  // the 3 x 3 block of the special kinds
    }

    TEST(RuleTableTest, ABakeThatConflictsReturnsTheRuleError) {
        Vector<RuleAxis> axes;
        axes.push_back(MakeNamedAxis("kind", {"a", "b"}));
        axes.push_back(MakeNamedAxis("verb", {"x", "y"}));
        RuleTable<int> table(std::move(axes));
        ASSERT_TRUE(table.AddRule({"a", "*"}, 1, "row.xml").has_value());
        ASSERT_TRUE(table.AddRule({"*", "y"}, 2, "col.xml").has_value());
        const auto baked = table.Bake(0);
        ASSERT_FALSE(baked.has_value());
        ASSERT_EQ(baked.error().conflicts.size(), 1u);
        EXPECT_TRUE(Contains(baked.error().message, "kind=a verb=*")) << baked.error().message.c_str();
        EXPECT_TRUE(Contains(baked.error().message, "row.xml")) << baked.error().message.c_str();
        EXPECT_TRUE(Contains(baked.error().message, "col.xml")) << baked.error().message.c_str();

        // The author's explicit rule for the overlap resolves it, and the table can be baked again.
        ASSERT_TRUE(table.AddRule({"a", "y"}, 3, "both.xml").has_value());
        const auto resolved = table.Bake(0);
        ASSERT_TRUE(resolved.has_value()) << resolved.error().message.c_str();
        EXPECT_EQ(resolved->Lookup(0, 1), 3);
    }

}  // namespace BECore::Tests
