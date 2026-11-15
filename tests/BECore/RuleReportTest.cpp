#include <BECore/RuleTestSupport.h>
#include <BECore/Rules/RuleTable.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String Text(const String& value) {
            return value;
        }

        String IntText(const int& value) {
            return Format("{}", value);
        }

        /// 5 kinds x 3 verbs x 4 tags: `stone` and `glass` are `blocker`s, `bomb` is `special`; the third axis' values are the tags
        /// `blast`, `blast.big`, `blast.small` and `touch`.
        Vector<RuleAxis> InteractionAxes() {
            const TagTree kindTags = BakedTags({"blocker", "special"});
            RuleAxis kind = MakeNamedAxis("kind", {"stone", "glass", "gem", "bomb", "dust"}, kindTags);
            EXPECT_TRUE(kind.SetTags("stone", {"blocker"}).has_value());
            EXPECT_TRUE(kind.SetTags("glass", {"blocker"}).has_value());
            EXPECT_TRUE(kind.SetTags("bomb", {"special"}).has_value());
            Vector<RuleAxis> axes;
            axes.push_back(std::move(kind));
            axes.push_back(MakeNamedAxis("verb", {"Hit", "Blast", "Swap"}));
            axes.push_back(RuleAxis::OfTags("tag", BakedTags({"blast", "blast.big", "blast.small", "touch"})));
            return axes;
        }

        struct RuleText {
            StringView kind;
            StringView verb;
            StringView tag;
            StringView value;
            StringView origin;
        };

        // Rule numbers in the order of declaration: #0 .. #4.
        constexpr RuleText kInteractionRules[] = {
            {"tag:blocker", "Hit", "*", "damage 1", "rules.xml:10"}, {"stone", "Hit", "*", "damage 2\nbreak", "rules.xml:11"}, {"*", "Blast", "tag:blast.*", "splash", "rules.xml:12"},
            {"bomb", "Blast", "blast.big", "boom", "rules.xml:13"},  {"bomb", "Blast", "blast.big", "boom", "rules.xml:14"},
        };

        /// The rules above added in @p order (rule numbers follow it).
        RuleTable<String> MakeInteractionTable(Span<const size_t> order, StringView splash = "splash") {
            RuleTable<String> table(InteractionAxes());
            for (const size_t i : order) {
                const RuleText& rule = kInteractionRules[i];
                const String value = i == 2 ? String(splash) : String(rule.value);
                EXPECT_TRUE(table.AddRule({rule.kind, rule.verb, rule.tag}, value, String(rule.origin)).has_value());
            }
            return table;
        }

        RuleTable<String> MakeInteractionTable(StringView splash = "splash") {
            const size_t order[] = {0, 1, 2, 3, 4};
            return MakeInteractionTable(Span<const size_t>(order), splash);
        }

        /// The combo table of `RuleTableTest`: rules #0 and #1, #2 and #3 are mirror pairs, #4 is its own mirror.
        RuleTable<String> MakeComboTable() {
            const TagTree tags = BakedTags({"special"});
            Vector<RuleAxis> axes;
            for (const StringView name : {"first", "second"}) {
                RuleAxis axis = MakeNamedAxis(name, {"bomb", "color_bomb", "rocket", "gem"}, tags);
                for (const StringView special : {"bomb", "color_bomb", "rocket"}) {
                    EXPECT_TRUE(axis.SetTags(special, {"special"}).has_value());
                }
                axes.push_back(std::move(axis));
            }
            RuleTable<String> table(std::move(axes));
            EXPECT_TRUE(table.AddSymmetric("tag:special", "color_bomb", "clear_all", "combo:1").has_value());
            EXPECT_TRUE(table.AddSymmetric("bomb", "color_bomb", "super", "combo:2").has_value());
            EXPECT_TRUE(table.AddSymmetric("tag:special", "tag:special", "merge", "combo:3").has_value());
            return table;
        }

        Vector<String> SplitLines(const String& text) {
            Vector<String> lines;
            size_t begin = 0;
            while (begin < text.size()) {
                const size_t end = text.find('\n', begin);
                lines.push_back(text.substr(begin, end - begin));
                begin = end + 1;
            }
            return lines;
        }

        // Cells of the 5 x 3 x 4 table: stone 0, glass 1, gem 2, bomb 3, dust 4; Hit 0, Blast 1, Swap 2; blast 0, blast.big 1,
        // blast.small 2, touch 3.
        constexpr uint32_t kStone = 0;
        constexpr uint32_t kDust = 4;
        constexpr uint32_t kBomb = 3;
        constexpr uint32_t kHit = 0;
        constexpr uint32_t kBlast = 1;
        constexpr uint32_t kSwap = 2;
        constexpr uint32_t kBlastBig = 1;
        constexpr uint32_t kTouch = 3;

        constexpr StringView kGolden = "# rules interactions: kind[5] x verb[3] x tag[4]\n"
                                       "# default: none\n"
                                       "stone | Hit | blast = damage 2\\nbreak\n"
                                       "stone | Hit | blast.big = damage 2\\nbreak\n"
                                       "stone | Hit | blast.small = damage 2\\nbreak\n"
                                       "stone | Hit | touch = damage 2\\nbreak\n"
                                       "stone | Blast | blast.big = splash\n"
                                       "stone | Blast | blast.small = splash\n"
                                       "glass | Hit | blast = damage 1\n"
                                       "glass | Hit | blast.big = damage 1\n"
                                       "glass | Hit | blast.small = damage 1\n"
                                       "glass | Hit | touch = damage 1\n"
                                       "glass | Blast | blast.big = splash\n"
                                       "glass | Blast | blast.small = splash\n"
                                       "gem | Blast | blast.big = splash\n"
                                       "gem | Blast | blast.small = splash\n"
                                       "bomb | Blast | blast.big = boom\n"
                                       "bomb | Blast | blast.small = splash\n"
                                       "dust | Blast | blast.big = splash\n"
                                       "dust | Blast | blast.small = splash\n";

        constexpr StringView kGoldenWithRules = "# rules interactions: kind[5] x verb[3] x tag[4]\n"
                                                "# default: none\n"
                                                "stone | Hit | blast = damage 2\\nbreak <- #1 rules.xml:11\n"
                                                "stone | Hit | blast.big = damage 2\\nbreak <- #1 rules.xml:11\n"
                                                "stone | Hit | blast.small = damage 2\\nbreak <- #1 rules.xml:11\n"
                                                "stone | Hit | touch = damage 2\\nbreak <- #1 rules.xml:11\n"
                                                "stone | Blast | blast.big = splash <- #2 rules.xml:12\n"
                                                "stone | Blast | blast.small = splash <- #2 rules.xml:12\n"
                                                "glass | Hit | blast = damage 1 <- #0 rules.xml:10\n"
                                                "glass | Hit | blast.big = damage 1 <- #0 rules.xml:10\n"
                                                "glass | Hit | blast.small = damage 1 <- #0 rules.xml:10\n"
                                                "glass | Hit | touch = damage 1 <- #0 rules.xml:10\n"
                                                "glass | Blast | blast.big = splash <- #2 rules.xml:12\n"
                                                "glass | Blast | blast.small = splash <- #2 rules.xml:12\n"
                                                "gem | Blast | blast.big = splash <- #2 rules.xml:12\n"
                                                "gem | Blast | blast.small = splash <- #2 rules.xml:12\n"
                                                "bomb | Blast | blast.big = boom <- #3 rules.xml:13\n"
                                                "bomb | Blast | blast.small = splash <- #2 rules.xml:12\n"
                                                "dust | Blast | blast.big = splash <- #2 rules.xml:12\n"
                                                "dust | Blast | blast.small = splash <- #2 rules.xml:12\n";

    }  // namespace

    TEST(RuleReportTest, ExplainNamesTheWinnerAndTheRulesItOverrode) {
        const auto baked = MakeInteractionTable().Bake("none");
        ASSERT_TRUE(baked.has_value()) << baked.error().message.c_str();

        const CellExplanation stone = baked->Explain(kStone, kHit, kTouch);
        ASSERT_TRUE(stone.winner.has_value());
        EXPECT_EQ(stone.winner->rule, 1u);
        EXPECT_EQ(stone.winner->origin, String("rules.xml:11"));
        EXPECT_EQ(stone.winner->text, String("kind=stone verb=Hit tag=*"));
        ASSERT_EQ(stone.overridden.size(), 1u);
        EXPECT_EQ(stone.overridden[0].rule, 0u);
        EXPECT_EQ(stone.overridden[0].origin, String("rules.xml:10"));
        EXPECT_EQ(stone.overridden[0].text, String("kind=tag:blocker verb=Hit tag=*"));

        // Three rules match; the winner is the first declared of the two equal ones, and the list is ascending by number.
        const CellExplanation bomb = baked->Explain(kBomb, kBlast, kBlastBig);
        ASSERT_TRUE(bomb.winner.has_value());
        EXPECT_EQ(bomb.winner->rule, 3u);
        ASSERT_EQ(bomb.overridden.size(), 2u);
        EXPECT_EQ(bomb.overridden[0].rule, 2u);
        EXPECT_EQ(bomb.overridden[1].rule, 4u);
        EXPECT_EQ(*baked->Winner(kBomb, kBlast, kBlastBig), bomb.winner->rule);
    }

    TEST(RuleReportTest, ACellNoRuleWonSaysDefault) {
        const auto baked = MakeInteractionTable().Bake("none");
        ASSERT_TRUE(baked.has_value());
        const CellExplanation dust = baked->Explain(kDust, kSwap, kTouch);
        EXPECT_FALSE(dust.winner.has_value());
        EXPECT_TRUE(dust.overridden.empty());
        EXPECT_EQ(baked->ExplainText(Text, kDust, kSwap, kTouch), String("dust x Swap x touch = none  <- default"));
    }

    TEST(RuleReportTest, ExplainTextOfARuleOverOneOther) {
        const auto baked = MakeInteractionTable().Bake("none");
        ASSERT_TRUE(baked.has_value());
        EXPECT_EQ(baked->ExplainText(Text, kStone, kHit, kTouch),
                  String("stone x Hit x touch = damage 2\\nbreak  <- #1 [rules.xml:11] kind=stone verb=Hit tag=*; overridden: #0 [rules.xml:10] kind=tag:blocker verb=Hit tag=*"));
    }

    TEST(RuleReportTest, ExplainTextOfAnEqualValueTieNamesTheFirstDeclaredAsTheWinner) {
        const auto baked = MakeInteractionTable().Bake("none");
        ASSERT_TRUE(baked.has_value());
        EXPECT_EQ(baked->ExplainText(Text, kBomb, kBlast, kBlastBig), String("bomb x Blast x blast.big = boom  <- #3 [rules.xml:13] kind=bomb verb=Blast tag=blast.big; overridden: "
                                                                             "#2 [rules.xml:12] kind=* verb=Blast tag=tag:blast.*, #4 [rules.xml:14] kind=bomb verb=Blast tag=blast.big"));
    }

    TEST(RuleReportTest, ExplainTextOfAMirroredRuleOverTwoOthers) {
        const auto baked = MakeComboTable().Bake("none");
        ASSERT_TRUE(baked.has_value()) << baked.error().message.c_str();
        // (color_bomb, bomb): the mirror of `bomb x color_bomb` beats the mirror of `tag:special x color_bomb` and `tag:special x tag:special`.
        EXPECT_EQ(baked->ExplainText(Text, 1, 0), String("color_bomb x bomb = super  <- #3 [combo:2] first=color_bomb second=bomb (mirrored); overridden: "
                                                         "#1 [combo:1] first=color_bomb second=tag:special (mirrored), #4 [combo:3] first=tag:special second=tag:special"));
    }

    TEST(RuleReportTest, ExplainTextTakesAnyCallableAndAnyValueType) {
        RuleTable<int> table(InteractionAxes());
        ASSERT_TRUE(table.AddRule({"gem", "*", "*"}, 7, "gem.xml").has_value());
        const auto baked = table.Bake(0);
        ASSERT_TRUE(baked.has_value());
        EXPECT_EQ(baked->ExplainText([](int value) { return Format("<{}>", value); }, 2, 2, 0), String("gem x Swap x blast = <7>  <- #0 [gem.xml] kind=gem verb=* tag=*"));
        EXPECT_EQ(baked->ExplainText(IntText, 1, 1, 1), String("glass x Blast x blast.big = 0  <- default"));
    }

    TEST(RuleReportTest, TheExportEqualsTheGoldenText) {
        const auto baked = MakeInteractionTable().Bake("none");
        ASSERT_TRUE(baked.has_value());
        EXPECT_EQ(baked->ExportText("interactions", Text), String(kGolden));
    }

    TEST(RuleReportTest, TheExportWithRulesNamesTheWinnerOfEveryLine) {
        const auto baked = MakeInteractionTable().Bake("none");
        ASSERT_TRUE(baked.has_value());
        RuleExportOptions options;
        options.withRules = true;
        EXPECT_EQ(baked->ExportText("interactions", Text, options), String(kGoldenWithRules));
    }

    TEST(RuleReportTest, TheDefaultTextIsEscapedInTheHeaderAndAnEmptyOriginIsLeftOut) {
        Vector<RuleAxis> axes;
        axes.push_back(MakeNamedAxis("kind", {"a", "b"}));
        RuleTable<String> table(std::move(axes));
        ASSERT_TRUE(table.AddRule({"b"}, "x", "").has_value());
        const auto baked = table.Bake("none\nat all");
        ASSERT_TRUE(baked.has_value());
        EXPECT_EQ(baked->ExportText("t", Text), String("# rules t: kind[2]\n# default: none\\nat all\nb = x\n"));
        RuleExportOptions options;
        options.withRules = true;
        EXPECT_EQ(baked->ExportText("t", Text, options), String("# rules t: kind[2]\n# default: none\\nat all\nb = x <- #0\n"));
        EXPECT_EQ(baked->ExplainText(Text, 1), String("b = x  <- #0 kind=b"));
    }

    TEST(RuleReportTest, TheOrderOfTheRulesDoesNotChangeTheExport) {
        const auto reference = MakeInteractionTable().Bake("none");
        ASSERT_TRUE(reference.has_value());
        const String golden = reference->ExportText("interactions", Text);
        for (uint64_t seed = 1; seed <= 10; ++seed) {
            Vector<size_t> order = {0, 1, 2, 3, 4};
            Rng rng(seed);
            rng.Shuffle(Span<size_t>(order));
            const auto baked = MakeInteractionTable(Span<const size_t>(order.data(), order.size())).Bake("none");
            ASSERT_TRUE(baked.has_value()) << baked.error().message.c_str();
            EXPECT_EQ(baked->ExportText("interactions", Text), golden) << "seed " << seed;
        }
    }

    TEST(RuleReportTest, ChangingOneRulesValueChangesExactlyTheLinesOfItsCells) {
        const auto before = MakeInteractionTable("splash").Bake("none");
        const auto after = MakeInteractionTable("splash!").Bake("none");
        ASSERT_TRUE(before.has_value());
        ASSERT_TRUE(after.has_value());
        const Vector<String> oldLines = SplitLines(before->ExportText("interactions", Text));
        const Vector<String> newLines = SplitLines(after->ExportText("interactions", Text));
        ASSERT_EQ(oldLines.size(), newLines.size());

        uint32_t changed = 0;
        for (size_t i = 0; i < oldLines.size(); ++i) {
            if (oldLines[i] != newLines[i]) {
                ++changed;
                EXPECT_NE(newLines[i].find("splash!"), String::npos) << newLines[i].c_str();
            }
        }
        EXPECT_EQ(changed, before->WonCells(2));
        EXPECT_EQ(changed, 9u);
    }

    TEST(RuleReportTest, TheScaleFixtureExportsEveryWonCellInRowMajorOrder) {
        const ScaleFixture fixture = MakeScaleFixture();
        RuleTable<int> table(fixture.axes);
        for (size_t r = 0; r < fixture.rules.size(); ++r) {
            const Vector<String>& texts = fixture.texts[r];
            const StringView views[] = {texts[0], texts[1], texts[2]};
            ASSERT_TRUE(table.AddRule(Span<const StringView>(views), static_cast<int>(fixture.rules[r].valueClass), fixture.rules[r].origin).has_value());
        }
        const auto baked = table.Bake(-1);
        ASSERT_TRUE(baked.has_value()) << baked.error().message.c_str();

        const String text = baked->ExportText("scale", IntText);
        const Vector<String> lines = SplitLines(text);
        uint32_t won = 0;
        for (uint32_t rule = 0; rule < baked->RuleCount(); ++rule) {
            won += baked->WonCells(rule);
        }
        ASSERT_EQ(lines.size(), won + 2u);
        // The first rule is `* * *`, so every cell has a winner and line `i + 2` is cell `i`.
        ASSERT_EQ(won, fixture.CellCount());
        EXPECT_EQ(lines[0], String("# rules scale: kind[100] x verb[8] x how[20]"));
        EXPECT_EQ(lines[1], String("# default: -1"));
        for (uint32_t cell = 0; cell < fixture.CellCount(); ++cell) {
            const Vector<uint32_t> ids = CellIds(Span<const RuleAxis>(fixture.axes.data(), fixture.axes.size()), cell);
            const String expected = Format("{} | {} | {} = {}", fixture.axes[0].ValueName(ids[0]), fixture.axes[1].ValueName(ids[1]), fixture.axes[2].ValueName(ids[2]), baked->LookupCell(cell));
            ASSERT_EQ(lines[cell + 2], expected) << "cell " << cell;
        }
        EXPECT_EQ(baked->ExportText("scale", IntText), text);
    }

}  // namespace BECore::Tests
