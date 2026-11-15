#include <BECore/Rules/RuleAxis.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        TagTree MakeTags(std::initializer_list<StringView> paths) {
            TagTree tree;
            for (StringView path : paths) {
                EXPECT_TRUE(tree.Add(path).has_value()) << ToStd(path);
            }
            tree.Bake();
            return tree;
        }

        /// The element kinds of the tests: `plain` has no tag, `bomb` and `rocket` are special, `wild` is only `special` itself,
        /// `specialist` carries a tag whose name begins with "special" without being under it.
        RuleAxis MakeKindAxis() {
            const TagTree tags = MakeTags({"special.bomb", "special.rocket", "special", "specialist", "blocker"});
            const StringView names[] = {"plain", "bomb", "rocket", "wild", "specialist", "stone", "both"};
            auto axis = RuleAxis::Named("kind", Span<const StringView>(names), tags);
            EXPECT_TRUE(axis.has_value());
            EXPECT_TRUE(axis->SetTags("bomb", {"special.bomb"}).has_value());
            EXPECT_TRUE(axis->SetTags("rocket", {"special.rocket"}).has_value());
            EXPECT_TRUE(axis->SetTags("wild", {"special"}).has_value());
            EXPECT_TRUE(axis->SetTags("specialist", {"specialist"}).has_value());
            EXPECT_TRUE(axis->SetTags("stone", {"blocker"}).has_value());
            EXPECT_TRUE(axis->SetTags("both", {"blocker", "special.bomb"}).has_value());
            return std::move(*axis);
        }

        Vector<uint32_t> ExpandText(const RuleAxis& axis, StringView text) {
            const auto selector = axis.ParseSelector(text);
            EXPECT_TRUE(selector.has_value()) << ToStd(text);
            return selector ? axis.Expand(*selector) : Vector<uint32_t>{};
        }

        uint32_t SpecificityOf(const RuleAxis& axis, StringView text) {
            const auto selector = axis.ParseSelector(text);
            EXPECT_TRUE(selector.has_value()) << ToStd(text);
            return selector ? selector->Specificity() : 0;
        }

        void ExpectErrorNames(const std::expected<RuleSelector, String>& result, StringView text, StringView axisName) {
            ASSERT_FALSE(result.has_value()) << ToStd(text);
            EXPECT_NE(result.error().find(String(text)), String::npos) << result.error().c_str();
            EXPECT_NE(result.error().find(String(axisName)), String::npos) << result.error().c_str();
        }

    }  // namespace

    TEST(RuleAxisTest, AValueNameSelectsOneValue) {
        const RuleAxis axis = MakeKindAxis();
        EXPECT_EQ(ExpandText(axis, "rocket"), (Vector<uint32_t>{2}));
        EXPECT_EQ(ExpandText(axis, "plain"), (Vector<uint32_t>{0}));
        const auto selector = axis.ParseSelector("rocket");
        ASSERT_TRUE(selector.has_value());
        EXPECT_EQ(selector->Kind(), RuleSelectorKind::Id);
        EXPECT_EQ(selector->Value(), 2u);
    }

    TEST(RuleAxisTest, StarSelectsEveryValue) {
        const RuleAxis axis = MakeKindAxis();
        EXPECT_EQ(ExpandText(axis, "*"), (Vector<uint32_t>{0, 1, 2, 3, 4, 5, 6}));
        EXPECT_EQ(axis.ParseSelector("*")->Kind(), RuleSelectorKind::Any);
    }

    TEST(RuleAxisTest, TagSelectsTheTagAndItsSubtreeNotAPrefixOfTheText) {
        const RuleAxis axis = MakeKindAxis();
        // bomb, rocket (under special), wild (special itself) and both (special.bomb); not specialist, not stone.
        EXPECT_EQ(ExpandText(axis, "tag:special"), (Vector<uint32_t>{1, 2, 3, 6}));
        EXPECT_EQ(ExpandText(axis, "tag:specialist"), (Vector<uint32_t>{4}));
        EXPECT_EQ(ExpandText(axis, "tag:blocker"), (Vector<uint32_t>{5, 6}));
    }

    TEST(RuleAxisTest, TagStarLeavesOutTheTagItself) {
        const RuleAxis axis = MakeKindAxis();
        EXPECT_EQ(ExpandText(axis, "tag:special.*"), (Vector<uint32_t>{1, 2, 6}));
    }

    TEST(RuleAxisTest, AnAxisOfTagsExpandsDescendants) {
        const TagTree tags = MakeTags({"match.3", "match.4", "match.5.line", "other"});
        const RuleAxis axis = RuleAxis::OfTags("how", tags);
        ASSERT_EQ(axis.Count(), 6u);
        EXPECT_EQ(axis.ValueName(0), "match");
        EXPECT_EQ(axis.ValueName(3), "match.5");
        EXPECT_EQ(axis.ValueName(4), "match.5.line");
        EXPECT_EQ(axis.FindValue("match.4"), 2u);
        EXPECT_EQ(axis.ValueTags(2).size(), 1u);
        EXPECT_EQ(axis.ValueTags(2)[0], TagId{2});

        EXPECT_EQ(ExpandText(axis, "tag:match.*"), (Vector<uint32_t>{1, 2, 3, 4}));
        EXPECT_EQ(ExpandText(axis, "tag:match"), (Vector<uint32_t>{0, 1, 2, 3, 4}));
        EXPECT_EQ(ExpandText(axis, "tag:match.5"), (Vector<uint32_t>{3, 4}));
        EXPECT_EQ(ExpandText(axis, "match.5"), (Vector<uint32_t>{3}));
        EXPECT_EQ(ExpandText(axis, "tag:other"), (Vector<uint32_t>{5}));
    }

    TEST(RuleAxisTest, SpecificityNumbers) {
        const TagTree tags = MakeTags({"match.3", "match.4", "other"});
        const RuleAxis axis = RuleAxis::OfTags("how", tags);
        EXPECT_EQ(SpecificityOf(axis, "*"), 0u);
        EXPECT_EQ(SpecificityOf(axis, "tag:match"), 2u);
        EXPECT_EQ(SpecificityOf(axis, "tag:match.*"), 3u);
        EXPECT_EQ(SpecificityOf(axis, "tag:match.3"), 4u);
        EXPECT_EQ(SpecificityOf(axis, "match.3"), kIdSpecificity);
        EXPECT_LT(SpecificityOf(axis, "*"), SpecificityOf(axis, "tag:match"));
        EXPECT_LT(SpecificityOf(axis, "tag:match"), SpecificityOf(axis, "tag:match.*"));
        EXPECT_LT(SpecificityOf(axis, "tag:match.*"), SpecificityOf(axis, "tag:match.3"));
        EXPECT_LT(SpecificityOf(axis, "tag:match.3"), SpecificityOf(axis, "match.3"));
        EXPECT_EQ(RuleSelector::Any().Specificity(), 0u);
        EXPECT_EQ(RuleSelector::Id(7).Specificity(), kIdSpecificity);
    }

    TEST(RuleAxisTest, TheIdIsAboveTheDeepestTag) {
        TagTree tags;
        String path = "t";
        ASSERT_TRUE(tags.Add(path).has_value());
        for (int depth = 1; depth < 200; ++depth) {
            path += ".t";
        }
        ASSERT_TRUE(tags.Add(path).has_value());
        tags.Bake();
        const RuleAxis axis = RuleAxis::OfTags("deep", tags);
        EXPECT_LT(SpecificityOf(axis, Format("tag:{}", path)), kIdSpecificity);
        EXPECT_EQ(SpecificityOf(axis, Format("tag:{}", path)), 400u);
    }

    TEST(RuleAxisTest, SurroundingSpacesAreTrimmed) {
        const RuleAxis axis = MakeKindAxis();
        EXPECT_EQ(ExpandText(axis, "  bomb "), (Vector<uint32_t>{1}));
        EXPECT_EQ(ExpandText(axis, " tag:special.* "), (Vector<uint32_t>{1, 2, 6}));
        EXPECT_EQ(ExpandText(axis, "\t*\n"), ExpandText(axis, "*"));
    }

    TEST(RuleAxisTest, MatchesFollowsTheKindOfTheSelector) {
        const RuleAxis axis = MakeKindAxis();
        EXPECT_TRUE(axis.Matches(RuleSelector::Any(), 3));
        EXPECT_TRUE(axis.Matches(RuleSelector::Id(3), 3));
        EXPECT_FALSE(axis.Matches(RuleSelector::Id(3), 2));
        const auto special = axis.ParseSelector("tag:special");
        ASSERT_TRUE(special.has_value());
        EXPECT_TRUE(axis.Matches(*special, 6));
        EXPECT_FALSE(axis.Matches(*special, 5));
        EXPECT_FALSE(axis.Matches(*special, 0));
    }

    TEST(RuleAxisTest, SetTagsKeepsASortedSetWithoutRepeats) {
        const TagTree tags = MakeTags({"a", "b", "c"});
        auto axis = RuleAxis::Named("kind", {"x"}, tags);
        ASSERT_TRUE(axis.has_value());
        ASSERT_TRUE(axis->SetTags(0u, {"c", "a", "c", "a"}).has_value());
        const Span<const TagId> valueTags = axis->ValueTags(0);
        ASSERT_EQ(valueTags.size(), 2u);
        EXPECT_EQ(valueTags[0], TagId{0});
        EXPECT_EQ(valueTags[1], TagId{2});
        ASSERT_TRUE(axis->SetTags("x", {}).has_value());
        EXPECT_TRUE(axis->ValueTags(0).empty());
    }

    TEST(RuleAxisTest, AnAxisIsAValueThatKeepsItsOwnTree) {
        RuleAxis copy = [&] {
            const TagTree tags = MakeTags({"a.b"});
            auto axis = RuleAxis::Named("kind", {"x"}, tags);
            EXPECT_TRUE(axis->SetTags("x", {"a.b"}).has_value());
            return std::move(*axis);
        }();
        const RuleAxis other = copy;
        EXPECT_EQ(ExpandText(other, "tag:a"), (Vector<uint32_t>{0}));
        EXPECT_EQ(other.Name(), "kind");
        EXPECT_EQ(other.Tags().Count(), 2u);
    }

    TEST(RuleAxisTest, SelectorErrorsNameTheTextAndTheAxis) {
        const RuleAxis axis = MakeKindAxis();
        ExpectErrorNames(axis.ParseSelector("nope"), "nope", "kind");
        ExpectErrorNames(axis.ParseSelector("tag:nope"), "tag:nope", "kind");
        ExpectErrorNames(axis.ParseSelector("tag:nope.*"), "tag:nope.*", "kind");
        ExpectErrorNames(axis.ParseSelector("tag:special.bomb.*"), "tag:special.bomb.*", "kind");
        ExpectErrorNames(axis.ParseSelector("tag:blocker.*"), "tag:blocker.*", "kind");
        ExpectErrorNames(axis.ParseSelector("tag:"), "tag:", "kind");
        ExpectErrorNames(axis.ParseSelector(""), "", "kind");
        ExpectErrorNames(axis.ParseSelector("  "), "  ", "kind");
    }

    TEST(RuleAxisTest, ATagNoValueCarriesIsAnEmptyExpansion) {
        const TagTree tags = MakeTags({"used", "unused"});
        auto axis = RuleAxis::Named("kind", {"x"}, tags);
        ASSERT_TRUE(axis->SetTags("x", {"used"}).has_value());
        ExpectErrorNames(axis->ParseSelector("tag:unused"), "tag:unused", "kind");
        EXPECT_TRUE(axis->ParseSelector("tag:used").has_value());
    }

    TEST(RuleAxisTest, DuplicateAndBadValueNamesAreErrors) {
        const auto twice = RuleAxis::Named("kind", {"a", "b", "a"});
        ASSERT_FALSE(twice.has_value());
        EXPECT_NE(twice.error().find("'a'"), String::npos);
        EXPECT_NE(twice.error().find("kind"), String::npos);

        for (StringView bad : {StringView(""), StringView("*"), StringView("tag:x"), StringView(" x")}) {
            const auto result = RuleAxis::Named("kind", {bad});
            ASSERT_FALSE(result.has_value()) << ToStd(bad);
            EXPECT_NE(result.error().find("kind"), String::npos);
        }
    }

    TEST(RuleAxisTest, SetTagsErrorsNameTheAxisAndWhatIsUnknown) {
        const TagTree tags = MakeTags({"a"});
        auto axis = RuleAxis::Named("kind", {"x"}, tags);
        ASSERT_TRUE(axis.has_value());

        const auto unknownTag = axis->SetTags("x", {"a", "missing"});
        ASSERT_FALSE(unknownTag.has_value());
        EXPECT_NE(unknownTag.error().find("missing"), String::npos);
        EXPECT_NE(unknownTag.error().find("kind"), String::npos);
        EXPECT_TRUE(axis->ValueTags(0).empty());

        const auto unknownValue = axis->SetTags("y", {"a"});
        ASSERT_FALSE(unknownValue.has_value());
        EXPECT_NE(unknownValue.error().find("'y'"), String::npos);
        EXPECT_NE(unknownValue.error().find("kind"), String::npos);

        EXPECT_FALSE(axis->SetTags(5u, {"a"}).has_value());
    }

    TEST(RuleAxisTest, CompileTimeChecks) {
        static_assert(RuleSelector::Any().Kind() == RuleSelectorKind::Any);
        static_assert(RuleSelector::Id(4).Value() == 4);
        static_assert(RuleSelector::Tags(TagRange{2, 5}, 3).Range() == TagRange{2, 5});
        static_assert(RuleSelector::Any() == RuleSelector::Any());
        static_assert(!(RuleSelector::Id(1) == RuleSelector::Id(2)));
    }

}  // namespace BECore::Tests
