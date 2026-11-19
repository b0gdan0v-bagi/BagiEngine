#include <BECore/RuleTestSupport.h>
#include <BECore/Rules/RuleBaker.h>
#include <Common/TestText.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kShuffleRounds = 20;
        constexpr uint32_t kCheckedCellStep = 41;

        std::expected<RuleCells, RuleError> Bake(const Vector<RuleAxis>& axes, const Vector<RuleSpec>& rules) {
            return BakeRules(Span<const RuleAxis>(axes), Span<const RuleSpec>(rules));
        }

        /// The class of the rule that wins the cell, `kUnset` for a cell nobody won.
        constexpr uint32_t kUnset = UINT32_MAX;

        Vector<uint32_t> ClassPerCell(const RuleCells& cells, const Vector<RuleSpec>& rules) {
            Vector<uint32_t> classes;
            for (const uint32_t rule : cells.cellRule) {
                classes.push_back(rule == kNoRule ? kUnset : rules[rule].valueClass);
            }
            return classes;
        }

        /// One axis whose values are the tags of `match`, `match.3`, `match.3.h`, `match.4`, `other` (ids 0 to 4).
        Vector<RuleAxis> MatchAxes() {
            Vector<RuleAxis> axes;
            axes.push_back(RuleAxis::OfTags("how", BakedTags({"match.3.h", "match.4", "other", "match.3"})));
            return axes;
        }

        /// `kind` a, b, c (all carry the tag `t`) against `verb` x, y, z.
        Vector<RuleAxis> KindVerbAxes() {
            Vector<RuleAxis> axes;
            const TagTree tags = BakedTags({"t"});
            axes.push_back(MakeNamedAxis("kind", {"a", "b", "c"}, tags));
            EXPECT_TRUE(axes[0].SetTags("a", {"t"}).has_value());
            EXPECT_TRUE(axes[0].SetTags("b", {"t"}).has_value());
            EXPECT_TRUE(axes[0].SetTags("c", {"t"}).has_value());
            axes.push_back(MakeNamedAxis("verb", {"x", "y", "z"}));
            return axes;
        }

    }  // namespace

    TEST(RuleBakerTest, AnIdBeatsATagBeatsAny) {
        const Vector<RuleAxis> axes = MatchAxes();
        // Order of the ids: match 0, match.3 1, match.3.h 2, match.4 3, other 4.
        const Vector<RuleSpec> rules = {MakeSpec(axes, {"*"}, 1, "any"), MakeSpec(axes, {"tag:match"}, 2, "tag"), MakeSpec(axes, {"match.3"}, 3, "id")};
        const auto cells = Bake(axes, rules);
        ASSERT_TRUE(cells.has_value()) << cells.error().message.c_str();
        EXPECT_EQ(ClassPerCell(*cells, rules), (Vector<uint32_t>{2, 3, 2, 2, 1}));
        EXPECT_EQ(cells->wonCells, (Vector<uint32_t>{1, 3, 1}));
    }

    TEST(RuleBakerTest, ADeeperTagBeatsAShallowerOne) {
        const Vector<RuleAxis> axes = MatchAxes();
        const Vector<RuleSpec> rules = {MakeSpec(axes, {"tag:match"}, 1), MakeSpec(axes, {"tag:match.3"}, 2)};
        const auto cells = Bake(axes, rules);
        ASSERT_TRUE(cells.has_value());
        EXPECT_EQ(ClassPerCell(*cells, rules), (Vector<uint32_t>{1, 2, 2, 1, kUnset}));
    }

    TEST(RuleBakerTest, DescendantsOfATagBeatTheTagItself) {
        const Vector<RuleAxis> axes = MatchAxes();
        const Vector<RuleSpec> rules = {MakeSpec(axes, {"tag:match"}, 1), MakeSpec(axes, {"tag:match.*"}, 2)};
        const auto cells = Bake(axes, rules);
        ASSERT_TRUE(cells.has_value());
        EXPECT_EQ(ClassPerCell(*cells, rules), (Vector<uint32_t>{1, 2, 2, 2, kUnset}));
    }

    TEST(RuleBakerTest, ATagBeatsADeeperTagsDescendantsOnlyWhenItIsDeeper) {
        // `tag:match.*` (3) is above `tag:match` (2) but below `tag:match.3` (4).
        const Vector<RuleAxis> axes = MatchAxes();
        const Vector<RuleSpec> rules = {MakeSpec(axes, {"tag:match.*"}, 1), MakeSpec(axes, {"tag:match.3"}, 2)};
        const auto cells = Bake(axes, rules);
        ASSERT_TRUE(cells.has_value());
        EXPECT_EQ(ClassPerCell(*cells, rules), (Vector<uint32_t>{kUnset, 2, 2, 1, kUnset}));
    }

    TEST(RuleBakerTest, IncomparableRulesMeetingInACellAreAConflictAnExplicitRuleResolves) {
        const Vector<RuleAxis> axes = KindVerbAxes();
        Vector<RuleSpec> rules = {MakeSpec(axes, {"a", "*"}, 1, "first.xml:1"), MakeSpec(axes, {"*", "y"}, 2, "second.xml:7")};
        const auto conflict = Bake(axes, rules);
        ASSERT_FALSE(conflict.has_value());
        ASSERT_EQ(conflict.error().conflicts.size(), 1u);
        const RuleConflict& pair = conflict.error().conflicts[0];
        EXPECT_EQ(pair.ruleA, 0u);
        EXPECT_EQ(pair.ruleB, 1u);
        EXPECT_EQ(pair.cellCount, 1u);
        EXPECT_EQ(pair.firstCell, 1u);  // a x y
        const String& message = conflict.error().message;
        EXPECT_TRUE(Contains(message, "#0")) << message.c_str();
        EXPECT_TRUE(Contains(message, "#1")) << message.c_str();
        EXPECT_TRUE(Contains(message, "first.xml:1")) << message.c_str();
        EXPECT_TRUE(Contains(message, "second.xml:7")) << message.c_str();
        EXPECT_TRUE(Contains(message, "a x y")) << message.c_str();

        rules.push_back(MakeSpec(axes, {"a", "y"}, 3));
        const auto resolved = Bake(axes, rules);
        ASSERT_TRUE(resolved.has_value()) << resolved.error().message.c_str();
        EXPECT_EQ(resolved->cellRule[1], 2u);
        EXPECT_EQ(resolved->cellRule[0], 0u);       // a x
        EXPECT_EQ(resolved->cellRule[4], 1u);       // b y
        EXPECT_EQ(resolved->cellRule[3], kNoRule);  // b x
    }

    TEST(RuleBakerTest, ARuleIncomparableToTheFirstWinnerButDominatedByAThirdOfTheSameValueIsNoConflict) {
        // W = (a, *) value 1, X = (*, y) value 2, Y = (tag:t, y) value 1. X and W clash on (a, y) alone, but Y dominates X.
        // A baker that compares each rule with the current winner only calls W against X a conflict when X is met before Y.
        const Vector<RuleAxis> axes = KindVerbAxes();
        const RuleSpec w = MakeSpec(axes, {"a", "*"}, 1, "W");
        const RuleSpec x = MakeSpec(axes, {"*", "y"}, 2, "X");
        const RuleSpec y = MakeSpec(axes, {"tag:t", "y"}, 1, "Y");
        Vector<RuleSpec> rules = {w, x, y};
        const uint32_t permutations[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        for (const auto& permutation : permutations) {
            const Vector<RuleSpec> ordered = {rules[permutation[0]], rules[permutation[1]], rules[permutation[2]]};
            const auto cells = Bake(axes, ordered);
            ASSERT_TRUE(cells.has_value()) << cells.error().message.c_str();
            // (a, y): W and Y are both maximal, the value is 1; (b, y) and (c, y): Y over X; (a, x), (a, z): W; the rest: nothing.
            const Vector<uint32_t> classes = ClassPerCell(*cells, ordered);
            EXPECT_EQ(classes[1], 1u);      // a y
            EXPECT_EQ(classes[4], 1u);      // b y
            EXPECT_EQ(classes[7], 1u);      // c y
            EXPECT_EQ(classes[0], 1u);      // a x
            EXPECT_EQ(classes[2], 1u);      // a z
            EXPECT_EQ(classes[3], kUnset);  // b x
        }
    }

    TEST(RuleBakerTest, ARuleDominatedOnlyByALaterMemberOfTheMaximalSetIsDropped) {
        // H = (a, *) and E = (tag:t, y) are both maximal at (a, y) and agree (value 1); R = (tag:t.deep, *) is incomparable to E and
        // dominated by H alone. Checking R against one member of the set (the first met) would keep it and call it a conflict.
        Vector<RuleAxis> axes;
        axes.push_back(MakeNamedAxis("kind", {"a", "b", "c"}, BakedTags({"t.deep"})));
        EXPECT_TRUE(axes[0].SetTags("a", {"t.deep"}).has_value());
        EXPECT_TRUE(axes[0].SetTags("b", {"t"}).has_value());
        EXPECT_TRUE(axes[0].SetTags("c", {"t"}).has_value());
        axes.push_back(MakeNamedAxis("verb", {"x", "y", "z"}));
        const RuleSpec rules[] = {MakeSpec(axes, {"a", "*"}, 1, "H"), MakeSpec(axes, {"tag:t", "y"}, 1, "E"), MakeSpec(axes, {"tag:t.deep", "*"}, 2, "R")};
        const uint32_t permutations[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        for (const auto& permutation : permutations) {
            const Vector<RuleSpec> ordered = {rules[permutation[0]], rules[permutation[1]], rules[permutation[2]]};
            const auto cells = Bake(axes, ordered);
            ASSERT_TRUE(cells.has_value()) << cells.error().message.c_str();
            const Vector<uint32_t> classes = ClassPerCell(*cells, ordered);
            EXPECT_EQ(classes, (Vector<uint32_t>{1, 1, 1, kUnset, 1, kUnset, kUnset, 1, kUnset}));
            for (uint32_t i = 0; i < 3; ++i) {
                if (ordered[i].valueClass == 2) {
                    EXPECT_EQ(cells->wonCells[i], 0u);
                }
            }
        }
    }

    TEST(RuleBakerTest, EqualSpecificityAndDifferentValuesIsAConflictThatNamesBothRules) {
        const Vector<RuleAxis> axes = KindVerbAxes();
        const Vector<RuleSpec> rules = {MakeSpec(axes, {"a", "*"}, 1, "one.xml"), MakeSpec(axes, {"b", "*"}, 1, "two.xml"), MakeSpec(axes, {"a", "*"}, 2, "three.xml")};
        const auto cells = Bake(axes, rules);
        ASSERT_FALSE(cells.has_value());
        ASSERT_EQ(cells.error().conflicts.size(), 1u);
        EXPECT_EQ(cells.error().conflicts[0].ruleA, 0u);
        EXPECT_EQ(cells.error().conflicts[0].ruleB, 2u);
        EXPECT_EQ(cells.error().conflicts[0].cellCount, 3u);  // a x, a y, a z
        EXPECT_EQ(cells.error().conflicts[0].firstCell, 0u);
        const String& message = cells.error().message;
        EXPECT_TRUE(Contains(message, "one.xml")) << message.c_str();
        EXPECT_TRUE(Contains(message, "three.xml")) << message.c_str();
        EXPECT_TRUE(Contains(message, "a x")) << message.c_str();
        EXPECT_FALSE(Contains(message, "two.xml")) << message.c_str();
    }

    TEST(RuleBakerTest, EqualSpecificityWithTheSameValueIsNoConflictAndTheFirstDeclaredWins) {
        const Vector<RuleAxis> axes = KindVerbAxes();
        const Vector<RuleSpec> rules = {MakeSpec(axes, {"a", "*"}, 5), MakeSpec(axes, {"a", "*"}, 5), MakeSpec(axes, {"a", "x"}, 6)};
        const auto cells = Bake(axes, rules);
        ASSERT_TRUE(cells.has_value());
        EXPECT_EQ(cells->cellRule[0], 2u);
        EXPECT_EQ(cells->cellRule[1], 0u);
        EXPECT_EQ(cells->cellRule[2], 0u);
        EXPECT_EQ(cells->wonCells, (Vector<uint32_t>{2, 0, 1}));
    }

    TEST(RuleBakerTest, TwoTagsOfOneDepthClashOnlyOnAValueThatCarriesBoth) {
        const TagTree tags = BakedTags({"a", "b"});
        {
            Vector<RuleAxis> axes;
            axes.push_back(MakeNamedAxis("kind", {"x", "y", "both"}, tags));
            EXPECT_TRUE(axes[0].SetTags("x", {"a"}).has_value());
            EXPECT_TRUE(axes[0].SetTags("y", {"b"}).has_value());
            EXPECT_TRUE(axes[0].SetTags("both", {"a", "b"}).has_value());
            const Vector<RuleSpec> rules = {MakeSpec(axes, {"tag:a"}, 1, "A"), MakeSpec(axes, {"tag:b"}, 2, "B")};
            const auto cells = Bake(axes, rules);
            ASSERT_FALSE(cells.has_value());
            ASSERT_EQ(cells.error().conflicts.size(), 1u);
            EXPECT_EQ(cells.error().conflicts[0].cellCount, 1u);
            EXPECT_EQ(cells.error().conflicts[0].firstCell, 2u);
        }
        {
            Vector<RuleAxis> axes;
            axes.push_back(MakeNamedAxis("kind", {"x", "y"}, tags));
            EXPECT_TRUE(axes[0].SetTags("x", {"a"}).has_value());
            EXPECT_TRUE(axes[0].SetTags("y", {"b"}).has_value());
            const Vector<RuleSpec> rules = {MakeSpec(axes, {"tag:a"}, 1), MakeSpec(axes, {"tag:b"}, 2)};
            const auto cells = Bake(axes, rules);
            ASSERT_TRUE(cells.has_value());
            EXPECT_EQ(ClassPerCell(*cells, rules), (Vector<uint32_t>{1, 2}));
        }
    }

    TEST(RuleBakerTest, ManyConflictsAreCutAtSixteenInTheMessageAndCompleteInTheData) {
        // Five rows (id, *) against five columns (*, id) of another value: 25 pairs, each clashing on one cell.
        Vector<RuleAxis> axes;
        axes.push_back(MakeNamedAxis("row", {"r0", "r1", "r2", "r3", "r4"}));
        axes.push_back(MakeNamedAxis("col", {"c0", "c1", "c2", "c3", "c4"}));
        Vector<RuleSpec> rules;
        for (uint32_t i = 0; i < 5; ++i) {
            rules.push_back(MakeSpec(axes, {Format("r{}", i), "*"}, 1));
        }
        for (uint32_t i = 0; i < 5; ++i) {
            rules.push_back(MakeSpec(axes, {"*", Format("c{}", i)}, 2));
        }
        const auto cells = Bake(axes, rules);
        ASSERT_FALSE(cells.has_value());
        EXPECT_EQ(cells.error().conflicts.size(), 25u);
        const String& message = cells.error().message;
        EXPECT_TRUE(Contains(message, "and 9 more")) << message.c_str();
        EXPECT_TRUE(Contains(message, "25 rule conflicts")) << message.c_str();
        size_t lines = 0;
        for (const char c : message) {
            lines += c == '\n' ? 1 : 0;
        }
        EXPECT_EQ(lines, kMaxListedRuleConflicts + 1);
        // Sorted by the two rule numbers.
        EXPECT_EQ(cells.error().conflicts[0].ruleA, 0u);
        EXPECT_EQ(cells.error().conflicts[0].ruleB, 5u);
        EXPECT_EQ(cells.error().conflicts[24].ruleA, 4u);
        EXPECT_EQ(cells.error().conflicts[24].ruleB, 9u);
    }

    TEST(RuleBakerTest, AShadowedRuleWinsNoCells) {
        Vector<RuleAxis> axes;
        axes.push_back(MakeNamedAxis("kind", {"a", "b"}));
        const Vector<RuleSpec> rules = {MakeSpec(axes, {"a"}, 1), MakeSpec(axes, {"b"}, 2), MakeSpec(axes, {"*"}, 3)};
        const auto cells = Bake(axes, rules);
        ASSERT_TRUE(cells.has_value());
        EXPECT_EQ(cells->wonCells, (Vector<uint32_t>{1, 1, 0}));
    }

    TEST(RuleBakerTest, ACellNoRuleMatchesHasNoRule) {
        Vector<RuleAxis> axes;
        axes.push_back(MakeNamedAxis("kind", {"a", "b", "c"}));
        const Vector<RuleSpec> rules = {MakeSpec(axes, {"a"}, 1)};
        const auto cells = Bake(axes, rules);
        ASSERT_TRUE(cells.has_value());
        EXPECT_EQ(cells->cellRule, (Vector<uint32_t>{0, kNoRule, kNoRule}));
        EXPECT_EQ(cells->axisSizes, (Vector<uint32_t>{3}));
    }

    TEST(RuleBakerTest, CellsAreRowMajorWithTheLastAxisFastest) {
        const Vector<RuleAxis> axes = KindVerbAxes();
        const Vector<RuleSpec> rules = {MakeSpec(axes, {"c", "z"}, 1)};
        const auto cells = Bake(axes, rules);
        ASSERT_TRUE(cells.has_value());
        EXPECT_EQ(cells->axisSizes, (Vector<uint32_t>{3, 3}));
        ASSERT_EQ(cells->cellRule.size(), 9u);
        EXPECT_EQ(cells->cellRule[8], 0u);
        EXPECT_EQ(cells->wonCells[0], 1u);
    }

    TEST(RuleBakerTest, ARuleThatMatchesNoCellWinsNothing) {
        Vector<RuleAxis> axes;
        axes.push_back(MakeNamedAxis("kind", {"a", "b"}));
        Vector<RuleSpec> rules = {MakeSpec(axes, {"a"}, 1)};
        RuleSpec empty;
        empty.selectors.push_back(RuleSelector::Tags(TagRange{}, 2));
        rules.push_back(empty);
        const auto cells = Bake(axes, rules);
        ASSERT_TRUE(cells.has_value());
        EXPECT_EQ(cells->wonCells, (Vector<uint32_t>{1, 0}));
    }

    TEST(RuleBakerTest, MalformedInputIsAnErrorNotAConflict) {
        Vector<RuleAxis> axes = KindVerbAxes();
        EXPECT_FALSE(Bake({}, {}).has_value());

        RuleSpec shortRule;
        shortRule.selectors.push_back(RuleSelector::Any());
        shortRule.origin = "short.xml";
        const auto tooFew = Bake(axes, {shortRule});
        ASSERT_FALSE(tooFew.has_value());
        EXPECT_TRUE(tooFew.error().conflicts.empty());
        EXPECT_TRUE(Contains(tooFew.error().message, "short.xml")) << tooFew.error().message.c_str();

        RuleSpec outOfRange;
        outOfRange.selectors.push_back(RuleSelector::Id(7));
        outOfRange.selectors.push_back(RuleSelector::Any());
        const auto bad = Bake(axes, {outOfRange});
        ASSERT_FALSE(bad.has_value());
        EXPECT_TRUE(Contains(bad.error().message, "kind")) << bad.error().message.c_str();
    }

    TEST(RuleBakerTest, AProductAboveTheLimitIsAnError) {
        constexpr uint32_t kSide = 4097;  // kSide * kSide > kMaxRuleCells
        Vector<String> names;
        for (uint32_t i = 0; i < kSide; ++i) {
            names.push_back(Format("v{}", i));
        }
        Vector<StringView> views(names.begin(), names.end());
        Vector<RuleAxis> axes;
        auto wide = RuleAxis::Named("wide", Span<const StringView>(views));
        ASSERT_TRUE(wide.has_value());
        axes.push_back(*wide);
        axes.push_back(std::move(*wide));
        EXPECT_GT(uint64_t{kSide} * kSide, kMaxRuleCells);
        const auto cells = Bake(axes, {});
        ASSERT_FALSE(cells.has_value());
        EXPECT_TRUE(Contains(cells.error().message, "wide")) << cells.error().message.c_str();
        EXPECT_TRUE(cells.error().conflicts.empty());

        // Three such axes: the running product is 64-bit, so it stops at the second.
        Vector<RuleAxis> three(3, axes[0]);
        EXPECT_FALSE(Bake(three, {}).has_value());
    }

    TEST(RuleBakerTest, TheResultDoesNotDependOnTheOrderOfTheRules) {
        const ScaleFixture fixture = MakeScaleFixture();
        const auto reference = Bake(fixture.axes, fixture.rules);
        ASSERT_TRUE(reference.has_value()) << reference.error().message.c_str();
        const Vector<uint32_t> expected = ClassPerCell(*reference, fixture.rules);

        for (uint32_t round = 0; round < kShuffleRounds; ++round) {
            Vector<RuleSpec> shuffled = fixture.rules;
            Rng rng(1000 + round);
            rng.Shuffle(Span<RuleSpec>(shuffled));
            const auto cells = Bake(fixture.axes, shuffled);
            ASSERT_TRUE(cells.has_value()) << "round " << round << ": " << cells.error().message.c_str();
            EXPECT_EQ(ClassPerCell(*cells, shuffled), expected) << "round " << round;
        }
    }

    TEST(RuleBakerTest, TheSetOfConflictsDoesNotDependOnTheOrderOfTheRules) {
        Vector<RuleAxis> axes;
        axes.push_back(MakeNamedAxis("row", {"r0", "r1", "r2"}));
        axes.push_back(MakeNamedAxis("col", {"c0", "c1", "c2"}));
        Vector<RuleSpec> rules;
        for (uint32_t i = 0; i < 3; ++i) {
            rules.push_back(MakeSpec(axes, {Format("r{}", i), "*"}, 1, Format("row{}", i)));
            rules.push_back(MakeSpec(axes, {"*", Format("c{}", i)}, 2, Format("col{}", i)));
        }
        rules.push_back(MakeSpec(axes, {"r0", "c0"}, 3, "fix"));
        for (uint32_t round = 0; round < kShuffleRounds; ++round) {
            Vector<RuleSpec> shuffled = rules;
            Rng rng(500 + round);
            rng.Shuffle(Span<RuleSpec>(shuffled));
            const auto cells = Bake(axes, shuffled);
            ASSERT_FALSE(cells.has_value());
            // 9 row/column pairs, one of which ((r0, c0)) is resolved by the explicit rule.
            EXPECT_EQ(cells.error().conflicts.size(), 8u) << "round " << round;
        }
    }

    TEST(RuleBakerTest, TheScaleFixtureBakesAndEqualsTheBruteForceWinner) {
        const ScaleFixture fixture = MakeScaleFixture();
        ASSERT_EQ(fixture.rules.size(), kScaleRules);
        const auto cells = Bake(fixture.axes, fixture.rules);
        ASSERT_TRUE(cells.has_value()) << cells.error().message.c_str();
        ASSERT_EQ(cells->cellRule.size(), fixture.CellCount());

        uint64_t won = 0;
        for (const uint32_t count : cells->wonCells) {
            won += count;
        }
        EXPECT_EQ(won, fixture.CellCount());  // the first rule matches every cell

        uint32_t checked = 0;
        for (uint32_t cell = 0; cell < fixture.CellCount(); cell += kCheckedCellStep) {
            const Vector<uint32_t> ids = CellIds(fixture.axes, cell);
            const BruteCell brute = BruteForceCell(Span<const RuleAxis>(fixture.axes), Span<const RuleSpec>(fixture.rules), Span<const uint32_t>(ids));
            ASSERT_FALSE(brute.conflict) << cell;
            ASSERT_TRUE(brute.winner.has_value()) << cell;
            EXPECT_EQ(cells->cellRule[cell], *brute.winner) << "cell " << cell;
            ++checked;
        }
        EXPECT_GE(checked, 300u);
    }

}  // namespace BECore::Tests
