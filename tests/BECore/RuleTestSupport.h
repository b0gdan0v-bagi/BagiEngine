#pragma once

// Helpers of the rule-table tests: small axes, rules from selector texts, the brute-force winner of a cell (written from the
// definition, apart from the baker) and a generator of a large conflict-free table.

#include <BECore/Format/Format.h>
#include <BECore/Random/Rng.h>
#include <BECore/Rules/RuleAxis.h>
#include <BECore/Rules/RuleBaker.h>
#include <BECore/Rules/RuleTable.h>
#include <BECore/Types/Types.h>
#include <gtest/gtest.h>
#include <initializer_list>

namespace BECore::Tests {

    inline TagTree BakedTags(std::initializer_list<StringView> paths) {
        TagTree tree;
        for (StringView path : paths) {
            EXPECT_TRUE(tree.Add(path).has_value()) << ToStd(path);
        }
        tree.Bake();
        return tree;
    }

    inline RuleAxis MakeNamedAxis(StringView name, std::initializer_list<StringView> valueNames, const TagTree& tags = BakedTags({})) {
        auto axis = RuleAxis::Named(name, valueNames, tags);
        EXPECT_TRUE(axis.has_value()) << (axis ? "" : axis.error().c_str());
        return std::move(*axis);
    }

    /// The axes of the combo table: a kind against a kind, `bomb`, `color_bomb` and `rocket` are `special`, `gem` is not.
    inline Vector<RuleAxis> ComboAxes() {
        const TagTree tags = BakedTags({"special"});
        Vector<RuleAxis> axes;
        for (const StringView name : {"first", "second"}) {
            RuleAxis axis = MakeNamedAxis(name, {"bomb", "color_bomb", "rocket", "gem"}, tags);
            for (const StringView special : {"bomb", "color_bomb", "rocket"}) {
                EXPECT_TRUE(axis.SetTags(special, {"special"}).has_value());
            }
            axes.push_back(std::move(axis));
        }
        return axes;
    }

    /// The combo table: three symmetric rules, so rules #0 and #1, #2 and #3 are mirror pairs and #4 is its own mirror.
    inline RuleTable<String> MakeComboTable() {
        RuleTable<String> table(ComboAxes());
        EXPECT_TRUE(table.AddSymmetric("tag:special", "color_bomb", "clear_all", "combo:1").has_value());
        EXPECT_TRUE(table.AddSymmetric("bomb", "color_bomb", "super", "combo:2").has_value());
        EXPECT_TRUE(table.AddSymmetric("tag:special", "tag:special", "merge", "combo:3").has_value());
        return table;
    }

    /// A rule with one selector text per axis; a selector that does not parse fails the test.
    inline RuleSpec MakeSpec(Span<const RuleAxis> axes, std::initializer_list<StringView> selectorTexts, uint32_t valueClass, StringView origin = {}) {
        RuleSpec spec;
        spec.valueClass = valueClass;
        spec.origin = String(origin);
        size_t axisIndex = 0;
        for (StringView text : selectorTexts) {
            const auto selector = axes[axisIndex].ParseSelector(text);
            EXPECT_TRUE(selector.has_value()) << (selector ? "" : selector.error().c_str());
            spec.selectors.push_back(selector ? *selector : RuleSelector::Any());
            if (axisIndex != 0) {
                spec.text.append(" ");
            }
            AppendFormat(spec.text, "{}={}", axes[axisIndex].Name(), text);
            ++axisIndex;
        }
        return spec;
    }

    /// What the definition says about a cell: the matching rules, the maximal ones among them, and the verdict.
    struct BruteCell {
        Vector<uint32_t> maximal;
        bool conflict = false;
        Optional<uint32_t> winner;
    };

    inline BruteCell BruteForceCell(Span<const RuleAxis> axes, Span<const RuleSpec> rules, Span<const uint32_t> ids) {
        Vector<uint32_t> matching;
        for (uint32_t r = 0; r < rules.size(); ++r) {
            bool matches = true;
            for (size_t a = 0; a < axes.size(); ++a) {
                matches = matches && axes[a].Matches(rules[r].selectors[a], ids[a]);
            }
            if (matches) {
                matching.push_back(r);
            }
        }
        const auto dominates = [&](uint32_t x, uint32_t y) {
            bool higher = false;
            for (size_t a = 0; a < axes.size(); ++a) {
                const uint32_t sx = rules[x].selectors[a].Specificity();
                const uint32_t sy = rules[y].selectors[a].Specificity();
                if (sx < sy) {
                    return false;
                }
                higher = higher || sx > sy;
            }
            return higher;
        };
        BruteCell cell;
        for (const uint32_t candidate : matching) {
            bool dominated = false;
            for (const uint32_t other : matching) {
                dominated = dominated || dominates(other, candidate);
            }
            if (!dominated) {
                cell.maximal.push_back(candidate);
            }
        }
        for (const uint32_t r : cell.maximal) {
            cell.conflict = cell.conflict || rules[r].valueClass != rules[cell.maximal.front()].valueClass;
        }
        if (!cell.maximal.empty() && !cell.conflict) {
            cell.winner = cell.maximal.front();
        }
        return cell;
    }

    /// The ids of a row-major cell index.
    inline Vector<uint32_t> CellIds(Span<const RuleAxis> axes, uint32_t cell) {
        Vector<uint32_t> ids(axes.size());
        for (size_t a = axes.size(); a-- > 0;) {
            ids[a] = cell % axes[a].Count();
            cell /= axes[a].Count();
        }
        return ids;
    }

    inline constexpr uint32_t kScaleKinds = 100;
    inline constexpr uint32_t kScaleVerbs = 8;
    inline constexpr uint32_t kScaleTagValues = 20;
    inline constexpr uint32_t kScaleRules = 300;

    /// 100 kinds x 8 verbs x 20 tags and about 300 rules, the same for a seed. No cell is a conflict by construction: every rule
    /// but the first (`* * *`) names a verb, the third axis is narrowed only together with a kind id, and the value class is a
    /// function of the specificity numbers, so two rules of one cell are comparable or agree.
    struct ScaleFixture {
        Vector<RuleAxis> axes;
        Vector<RuleSpec> rules;
        /// The selector texts of each rule, one per axis.
        Vector<Vector<String>> texts;

        [[nodiscard]] uint32_t CellCount() const {
            return kScaleKinds * kScaleVerbs * kScaleTagValues;
        }
    };

    inline ScaleFixture MakeScaleFixture(uint64_t seed = 20260) {
        constexpr uint32_t kGroups = 4;
        constexpr uint32_t kLeavesPerGroup = 3;
        constexpr uint32_t kMostKindTags = 2;
        constexpr uint32_t kClassCount = 9;

        Rng rng(seed);

        // Kinds carry up to two tags of a tree of 4 groups with 3 leaves each.
        TagTree kindTree;
        Vector<String> kindTagPaths;
        Vector<String> groupPaths;
        for (uint32_t g = 0; g < kGroups; ++g) {
            groupPaths.push_back(Format("g{}", g));
            kindTagPaths.push_back(groupPaths.back());
            for (uint32_t l = 0; l < kLeavesPerGroup; ++l) {
                kindTagPaths.push_back(Format("g{}.l{}", g, l));
            }
        }
        for (const String& path : kindTagPaths) {
            EXPECT_TRUE(kindTree.Add(path).has_value());
        }
        kindTree.Bake();

        Vector<String> kindNames;
        for (uint32_t k = 0; k < kScaleKinds; ++k) {
            kindNames.push_back(Format("k{}", k));
        }
        Vector<StringView> kindViews(kindNames.begin(), kindNames.end());
        auto kindAxis = RuleAxis::Named("kind", Span<const StringView>(kindViews), kindTree);
        EXPECT_TRUE(kindAxis.has_value());
        for (uint32_t k = 0; k < kScaleKinds; ++k) {
            Vector<StringView> tags;
            const uint32_t tagCount = rng.UniformInt<uint32_t>(0, kMostKindTags);
            for (uint32_t i = 0; i < tagCount; ++i) {
                tags.push_back(kindTagPaths[rng.UniformInt<size_t>(0, kindTagPaths.size() - 1)]);
            }
            EXPECT_TRUE(kindAxis->SetTags(k, Span<const StringView>(tags)).has_value());
        }

        Vector<String> verbNames;
        for (uint32_t v = 0; v < kScaleVerbs; ++v) {
            verbNames.push_back(Format("v{}", v));
        }
        Vector<StringView> verbViews(verbNames.begin(), verbNames.end());
        auto verbAxis = RuleAxis::Named("verb", Span<const StringView>(verbViews));
        EXPECT_TRUE(verbAxis.has_value());

        // The third axis: 4 roots with 4 children each, its values are the tags.
        TagTree howTree;
        Vector<String> howRoots;
        for (uint32_t t = 0; t < kScaleTagValues / 5; ++t) {
            howRoots.push_back(Format("t{}", t));
            EXPECT_TRUE(howTree.Add(howRoots.back()).has_value());
            for (uint32_t c = 0; c < 4; ++c) {
                EXPECT_TRUE(howTree.Add(Format("t{}.c{}", t, c)).has_value());
            }
        }
        howTree.Bake();

        ScaleFixture fixture;
        fixture.axes.push_back(std::move(*kindAxis));
        fixture.axes.push_back(std::move(*verbAxis));
        fixture.axes.push_back(RuleAxis::OfTags("how", howTree));
        EXPECT_EQ(fixture.axes[2].Count(), kScaleTagValues);

        const auto add = [&](const String& kind, const String& verb, const String& how) -> bool {
            const StringView texts[] = {kind, verb, how};
            RuleSpec spec;
            for (size_t a = 0; a < 3; ++a) {
                const auto selector = fixture.axes[a].ParseSelector(texts[a]);
                if (!selector) {
                    return false;
                }
                spec.selectors.push_back(*selector);
            }
            uint32_t sum = 0;
            for (size_t a = 0; a < 3; ++a) {
                sum += spec.selectors[a].Specificity() * static_cast<uint32_t>(a + 1);
            }
            spec.valueClass = sum % kClassCount;
            spec.origin = Format("fixture:{}", fixture.rules.size());
            spec.text = Format("kind={} verb={} how={}", kind, verb, how);
            fixture.rules.push_back(std::move(spec));
            fixture.texts.push_back({kind, verb, how});
            return true;
        };

        EXPECT_TRUE(add("*", "*", "*"));
        while (fixture.rules.size() < kScaleRules) {
            const String verb = Format("v{}", rng.UniformInt<uint32_t>(0, kScaleVerbs - 1));
            const uint32_t kindChoice = rng.UniformInt<uint32_t>(0, 9);
            String kind = "*";
            bool kindIsId = false;
            if (kindChoice <= 2) {
                kind = Format("k{}", rng.UniformInt<uint32_t>(0, kScaleKinds - 1));
                kindIsId = true;
            } else if (kindChoice <= 5) {
                kind = Format("tag:{}", kindTagPaths[rng.UniformInt<size_t>(0, kindTagPaths.size() - 1)]);
            } else if (kindChoice <= 7) {
                kind = Format("tag:{}.*", groupPaths[rng.UniformInt<size_t>(0, groupPaths.size() - 1)]);
            }
            String how = "*";
            if (kindIsId && rng.Chance(0.5)) {
                const String root = howRoots[rng.UniformInt<size_t>(0, howRoots.size() - 1)];
                switch (rng.UniformInt<uint32_t>(0, 2)) {
                    case 0:
                        how = Format("{}.c{}", root, rng.UniformInt<uint32_t>(0, 3));
                        break;
                    case 1:
                        how = Format("tag:{}", root);
                        break;
                    default:
                        how = Format("tag:{}.*", root);
                        break;
                }
            }
            add(kind, verb, how);
        }
        return fixture;
    }

}  // namespace BECore::Tests
