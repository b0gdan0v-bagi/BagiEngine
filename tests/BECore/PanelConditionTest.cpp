#include <BECore/PanelConditionProbe.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLimits.h>
#include <BECore/Widgets/PanelAssets/PanelBindingReader.h>
#include <BECore/Widgets/PanelAssets/PanelCondition.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Generated/PanelConditionProbe.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int32_t kProbeCount = 7;

        PanelCondition MustParse(StringView text) {
            auto parsed = ParsePanelCondition(text);
            EXPECT_TRUE(parsed.has_value()) << String(text).c_str() << ": " << (parsed ? "" : parsed.error().c_str());
            return parsed ? std::move(*parsed) : PanelCondition{};
        }

        class PanelConditionTest : public ::testing::Test {
        protected:
            void SetUp() override {
                probe.count = kProbeCount;
                probe.ratio = 0.5f;
                probe.title = "Files";
                probe.mode = ConditionProbeMode::Busy;
                handle = sources.Register("p", [this] { return TypedRef::Of(probe); });
            }

            /// @p whenUnreadable is what an unreadable term counts as
            bool Eval(StringView text, bool whenUnreadable = true) {
                const PanelCondition condition = MustParse(text);
                return EvaluatePanelCondition(reader, sources, condition, whenUnreadable, scratch);
            }

            PanelDataSources sources;
            PanelBindingReader reader;
            ConditionProbe probe;
            PanelSourceHandle handle;
            String scratch;
        };

    }  // namespace

    TEST_F(PanelConditionTest, APlainTermIsTheOldCondition) {
        const PanelCondition plain = MustParse("files.running");
        ASSERT_EQ(plain.alternatives.size(), 1u);
        ASSERT_EQ(plain.alternatives[0].size(), 1u);
        EXPECT_EQ(plain.alternatives[0][0].compare, PanelCompare::Truth);
        EXPECT_EQ(plain.alternatives[0][0].binding.source, "files");
        EXPECT_FALSE(plain.alternatives[0][0].binding.negated);

        const PanelCondition negated = MustParse("!files.running");
        EXPECT_TRUE(negated.alternatives[0][0].binding.negated);
    }

    TEST_F(PanelConditionTest, ParseGroupsAndBeforeOr) {
        const PanelCondition condition = MustParse("a.x and a.y or a.z and a.w and a.v or a.u");
        ASSERT_EQ(condition.alternatives.size(), 3u);
        EXPECT_EQ(condition.alternatives[0].size(), 2u);
        EXPECT_EQ(condition.alternatives[1].size(), 3u);
        EXPECT_EQ(condition.alternatives[2].size(), 1u);
    }

    TEST_F(PanelConditionTest, ParseTakesEveryOperatorAndLiteralKind) {
        struct Case {
            StringView text;
            PanelCompare compare;
        };
        for (const Case& c : {Case{"a.x == 1", PanelCompare::Equal}, Case{"a.x != 1", PanelCompare::NotEqual}, Case{"a.x < 1", PanelCompare::Less}, Case{"a.x <= 1", PanelCompare::LessEqual},
                              Case{"a.x > 1", PanelCompare::Greater}, Case{"a.x >= 1", PanelCompare::GreaterEqual}}) {
            const PanelCondition condition = MustParse(c.text);
            EXPECT_EQ(condition.alternatives[0][0].compare, c.compare) << String(c.text).c_str();
            EXPECT_TRUE(condition.alternatives[0][0].numeric);
            EXPECT_EQ(condition.alternatives[0][0].number, 1.0);
        }
        EXPECT_EQ(MustParse("a.x == -2.5e1").alternatives[0][0].number, -25.0);

        const PanelCondition word = MustParse("a.mode != Busy");
        EXPECT_FALSE(word.alternatives[0][0].numeric);
        EXPECT_EQ(word.alternatives[0][0].word, "Busy");
        EXPECT_EQ(MustParse("a.flag == true").alternatives[0][0].word, "true");
    }

    TEST_F(PanelConditionTest, AQuotedKeyMayHoldBlanks) {
        const PanelCondition condition = MustParse("m.table[\"x y\"].on and m.n > 2");
        ASSERT_EQ(condition.alternatives[0].size(), 2u);
        EXPECT_EQ(condition.alternatives[0][0].binding.path, "table[\"x y\"].on");
    }

    TEST_F(PanelConditionTest, BadTextIsAShortLine) {
        const String tooLong(PanelAssetLimits::kMaxTextBytes + 1, 'a');
        String tooMany = "a.x";
        for (size_t i = 0; i < PanelAssetLimits::kMaxConditionTerms; ++i) {
            tooMany += " and a.x";
        }
        const Vector<String> bad = {
            "",           "   ",         "a.x and",   "a.x or",      "and a.x",      "a.x and and a.y", "a.x and or a.y", "a.x a.y",         "a.x==1",    "a.x ==",        "== 1",  "!a.x == 1",
            "a.x < Busy", "a.x >= true", "a.x == 1x", "a.x == -inf", "a.x == 1e999", "a.x == \"q\"",    "a.x == a b",     "a.x == Bu\x01sy", "a.x[ == 1", "a.x == 1 == 2", tooMany, tooLong};
        for (const String& text : bad) {
            const auto parsed = ParsePanelCondition(text);
            ASSERT_FALSE(parsed.has_value()) << text.c_str();
            EXPECT_FALSE(parsed.error().empty());
            EXPECT_EQ(parsed.error().find('\n'), String::npos);
        }

        String atLimit = "a.x";
        for (size_t i = 1; i < PanelAssetLimits::kMaxConditionTerms; ++i) {
            atLimit += " or a.x";
        }
        EXPECT_TRUE(ParsePanelCondition(atLimit).has_value());
    }

    TEST_F(PanelConditionTest, EvaluatesEveryOperatorOverANumber) {
        EXPECT_TRUE(Eval("p.count == 7"));
        EXPECT_FALSE(Eval("p.count == 8"));
        EXPECT_TRUE(Eval("p.count != 8"));
        EXPECT_TRUE(Eval("p.count < 8"));
        EXPECT_FALSE(Eval("p.count < 7"));
        EXPECT_TRUE(Eval("p.count <= 7"));
        EXPECT_TRUE(Eval("p.count > 6"));
        EXPECT_FALSE(Eval("p.count > 7"));
        EXPECT_TRUE(Eval("p.count >= 7"));
        EXPECT_TRUE(Eval("p.ratio == 0.5"));
        EXPECT_TRUE(Eval("p.count > -1"));
    }

    TEST_F(PanelConditionTest, EvaluatesAWordAsText) {
        EXPECT_TRUE(Eval("p.mode == Busy"));
        EXPECT_FALSE(Eval("p.mode == Idle"));
        EXPECT_TRUE(Eval("p.mode != Idle"));
        EXPECT_TRUE(Eval("p.title == Files"));
        EXPECT_FALSE(Eval("p.flag == true"));
        probe.flag = true;
        EXPECT_TRUE(Eval("p.flag == true"));
        EXPECT_TRUE(Eval("p.flag != false"));
    }

    TEST_F(PanelConditionTest, PlainTermsReadABoolAndItsNegation) {
        EXPECT_FALSE(Eval("p.flag"));
        EXPECT_TRUE(Eval("!p.flag"));
        probe.flag = true;
        EXPECT_TRUE(Eval("p.flag"));
    }

    TEST_F(PanelConditionTest, AndIsDecidedBeforeOr) {
        probe.flag = true;
        EXPECT_TRUE(Eval("p.flag and p.count == 7"));
        EXPECT_FALSE(Eval("p.flag and p.count == 8"));
        EXPECT_TRUE(Eval("p.count == 8 and p.flag or p.mode == Busy"));
        EXPECT_FALSE(Eval("p.count == 8 and p.flag or p.mode == Idle"));
        EXPECT_TRUE(Eval("p.count == 8 or p.count == 7 and p.flag"));
        EXPECT_FALSE(Eval("p.count == 8 or p.count == 9 and p.flag"));
    }

    TEST_F(PanelConditionTest, AnUnreadableTermCountsAsTheCallerSays) {
        EXPECT_TRUE(Eval("p.nope", true));
        EXPECT_FALSE(Eval("p.nope", false));
        EXPECT_TRUE(Eval("q.flag == true", true));
        EXPECT_FALSE(Eval("q.flag == true", false));
        // A number term over a text and a bool term over a number
        EXPECT_TRUE(Eval("p.title > 1", true));
        EXPECT_FALSE(Eval("p.count", false));
        EXPECT_FALSE(Eval("p.count and p.flag", false));
        // One unreadable term does not hide the others
        EXPECT_TRUE(Eval("p.nope and p.count == 7", true));
        EXPECT_FALSE(Eval("p.nope and p.count == 8", true));
    }

    TEST_F(PanelConditionTest, ANumberLiteralNeedsANumberField) {
        EXPECT_TRUE(Eval("p.mode == 1", true));
        EXPECT_FALSE(Eval("p.mode == 1", false));
    }

    TEST_F(PanelConditionTest, AnEmptyConditionIsTrue) {
        EXPECT_TRUE(EvaluatePanelCondition(reader, sources, PanelCondition{}, false, scratch));
    }

    TEST_F(PanelConditionTest, ASteadyEvaluationAllocatesNothing) {
        const PanelCondition condition = MustParse("p.flag and p.count > 3 or p.mode == Busy and p.title != Other or p.ratio < 1 and !p.flag");
        const PanelCondition other = MustParse("p.count >= 7 and p.mode != Idle");
        const auto run = [&] {
            EXPECT_TRUE(EvaluatePanelCondition(reader, sources, condition, true, scratch));
            EXPECT_TRUE(EvaluatePanelCondition(reader, sources, other, true, scratch));
        };
        run();
        ExpectNoAllocations(run);
    }

}  // namespace BECore::Tests
