#include <BECore/GameManager/CoreManager.h>
#include <BECore/Text/TextSettings.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        TextSettings Parse(const char* xml) {
            auto config = XmlConfig::Create();
            EXPECT_TRUE(config.LoadFromString(xml));
            return TextSettings::FromConfig(config.GetRoot());
        }

        constexpr const char* kTwoFamilies = R"(<TextConfig defaultFont="regular.ttf" defaultFamily="Body">
            <families>
              <family name="Body"><face weight="400" path="body-regular.ttf"/><face weight="700" path="body-bold.ttf"/></family>
              <family name="Display"><face weight="400" path="display.ttf"/></family>
            </families></TextConfig>)";

        bool Covers(const TextSettings& settings, char32_t codePoint) {
            for (const GlyphRange& range : settings.preload) {
                if (codePoint >= range.first && codePoint <= range.last) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    TEST(TextSettingsTest, TheDefaultsCoverLatinLatin1AndCyrillic) {
        const TextSettings settings = TextSettings::Default();

        EXPECT_TRUE(Covers(settings, U'A'));
        EXPECT_TRUE(Covers(settings, 0x00E9));
        EXPECT_TRUE(Covers(settings, 0x0416));
        EXPECT_TRUE(Covers(settings, 0x0451));
        EXPECT_TRUE(Covers(settings, 0x2026));
        EXPECT_FALSE(Covers(settings, 0x4E2D));
        EXPECT_EQ(settings.pageSize, 1024u);
        EXPECT_EQ(settings.defaultFont, "assets/fonts/PT_Sans-Web-Regular.ttf"_intern);
    }

    TEST(TextSettingsTest, AnEmptyNodeGivesTheDefaults) {
        const TextSettings settings = TextSettings::FromConfig(XmlNode{});

        EXPECT_EQ(settings.preload.size(), TextSettings::Default().preload.size());
    }

    TEST(TextSettingsTest, AttributesOverrideTheDefaults) {
        const TextSettings settings = Parse(R"(<TextConfig defaultFont="assets/fonts/Other.ttf" pageSize="512"/>)");

        EXPECT_EQ(settings.defaultFont, "assets/fonts/Other.ttf"_intern);
        EXPECT_EQ(settings.pageSize, 512u);
        EXPECT_EQ(settings.preload.size(), TextSettings::Default().preload.size());  // no <preload>: the default ranges stay
    }

    TEST(TextSettingsTest, APreloadElementReplacesTheRanges) {
        const TextSettings settings = Parse(R"(<TextConfig><preload><range from="65" to="90"/><range from="48" to="57"/></preload></TextConfig>)");

        ASSERT_EQ(settings.preload.size(), 2u);
        EXPECT_EQ(settings.preload[0].first, U'A');
        EXPECT_EQ(settings.preload[0].last, U'Z');
        EXPECT_FALSE(Covers(settings, U'a'));
    }

    TEST(TextSettingsTest, ABadRangeIsSkipped) {
        const TextSettings settings = Parse(R"(<TextConfig><preload><range from="90" to="65"/><range from="48"/><range from="48" to="57"/></preload></TextConfig>)");

        ASSERT_EQ(settings.preload.size(), 1u);
        EXPECT_EQ(settings.preload[0].first, U'0');
    }

    TEST(TextSettingsTest, ThePageSizeIsKeptInASaneRange) {
        EXPECT_EQ(Parse(R"(<TextConfig pageSize="1"/>)").pageSize, 64u);
        EXPECT_EQ(Parse(R"(<TextConfig pageSize="999999"/>)").pageSize, 8192u);
    }

    TEST(TextSettingsTest, TwoFamiliesAreParsedWithTheirFaces) {
        const TextSettings settings = Parse(kTwoFamilies);

        EXPECT_EQ(settings.defaultFamily, "Body"_intern);
        ASSERT_EQ(settings.families.size(), 2u);
        EXPECT_EQ(settings.families[0].name, "Body"_intern);
        ASSERT_EQ(settings.families[0].faces.size(), 2u);
        EXPECT_EQ(settings.families[0].faces[1].weight, 700);
        EXPECT_EQ(settings.families[0].faces[1].path, "body-bold.ttf"_intern);
        EXPECT_EQ(settings.families[1].name, "Display"_intern);
        EXPECT_EQ(settings.preload.size(), TextSettings::Default().preload.size());  // <families> leaves the ranges alone
    }

    TEST(TextSettingsTest, TheNearestWeightIsChosenByTheCssRule) {
        const TextSettings settings = Parse(kTwoFamilies);
        const PoolString regular = "body-regular.ttf"_intern;
        const PoolString bold = "body-bold.ttf"_intern;

        EXPECT_EQ(settings.ResolveFontPath("Body"_intern, 100), regular);
        EXPECT_EQ(settings.ResolveFontPath("Body"_intern, 300), regular);
        EXPECT_EQ(settings.ResolveFontPath("Body"_intern, 400), regular);
        EXPECT_EQ(settings.ResolveFontPath("Body"_intern, 500), regular);
        EXPECT_EQ(settings.ResolveFontPath("Body"_intern, 600), bold);
        EXPECT_EQ(settings.ResolveFontPath("Body"_intern, 700), bold);
        EXPECT_EQ(settings.ResolveFontPath("Body"_intern, 900), bold);
    }

    TEST(TextSettingsTest, TheOrderOfTheCssRuleHoldsWithGapsInTheFaces) {
        const TextSettings settings = Parse(R"(<TextConfig defaultFont="d.ttf"><families>
            <family name="F"><face weight="300" path="w300"/><face weight="500" path="w500"/><face weight="900" path="w900"/></family>
            <family name="G"><face weight="200" path="g200"/><face weight="800" path="g800"/></family>
            </families></TextConfig>)");

        EXPECT_EQ(settings.ResolveFontPath("F"_intern, 100), "w300"_intern);  // nothing below: look up
        EXPECT_EQ(settings.ResolveFontPath("F"_intern, 400), "w500"_intern);  // 400 looks up to 500 before it looks down
        EXPECT_EQ(settings.ResolveFontPath("F"_intern, 600), "w900"_intern);  // over 500: above first, though 500 is nearer
        EXPECT_EQ(settings.ResolveFontPath("G"_intern, 400), "g200"_intern);  // none in 400..500: below, then above 500
        EXPECT_EQ(settings.ResolveFontPath("G"_intern, 300), "g200"_intern);  // under 400: below first
        EXPECT_EQ(settings.ResolveFontPath("G"_intern, 700), "g800"_intern);
    }

    TEST(TextSettingsTest, TheWeightIsClamped) {
        const TextSettings settings = Parse(kTwoFamilies);

        EXPECT_EQ(settings.ResolveFontPath("Body"_intern, -5), "body-regular.ttf"_intern);
        EXPECT_EQ(settings.ResolveFontPath("Body"_intern, 5000), "body-bold.ttf"_intern);
    }

    TEST(TextSettingsTest, AnEmptyFamilyIsTheDefaultFamily) {
        const TextSettings settings = Parse(kTwoFamilies);
        bool known = false;

        EXPECT_EQ(settings.ResolveFontPath(PoolString{}, 700, &known), "body-bold.ttf"_intern);
        EXPECT_TRUE(known);
        EXPECT_EQ(settings.ResolveFontPath("Display"_intern, 700, &known), "display.ttf"_intern);  // its only face
        EXPECT_TRUE(known);
    }

    TEST(TextSettingsTest, AnUnknownFamilyGivesTheDefaultFontAndSaysSo) {
        const TextSettings settings = Parse(kTwoFamilies);
        bool known = true;

        EXPECT_EQ(settings.ResolveFontPath("Nope"_intern, 700, &known), "regular.ttf"_intern);
        EXPECT_FALSE(known);
        EXPECT_EQ(settings.ResolveFontPath("Nope"_intern, 700), "regular.ttf"_intern);  // known is optional
    }

    TEST(TextSettingsTest, AnEmptyTableGivesTheDefaultFont) {
        const TextSettings settings = Parse(R"(<TextConfig defaultFont="regular.ttf"/>)");
        bool known = false;

        EXPECT_TRUE(settings.families.empty());
        EXPECT_EQ(settings.ResolveFontPath(PoolString{}, 700, &known), "regular.ttf"_intern);
        EXPECT_TRUE(known);  // nothing was asked for by name, so there is nothing to report
        EXPECT_EQ(settings.ResolveFontPath("Body"_intern, 400, &known), "regular.ttf"_intern);
        EXPECT_FALSE(known);
    }

    TEST(TextSettingsTest, ABadFaceOrFamilyIsSkipped) {
        const TextSettings settings = Parse(R"(<TextConfig defaultFont="regular.ttf"><families>
            <family name="Body"><face weight="400" path=""/><face weight="0" path="zero"/><face weight="1001" path="big"/>
                                <face path="noweight"/><face weight="400" path="ok.ttf"/></family>
            <family name="Empty"><face weight="400"/></family>
            <family><face weight="400" path="noname.ttf"/></family>
            </families></TextConfig>)");

        ASSERT_EQ(settings.families.size(), 1u);
        ASSERT_EQ(settings.families[0].faces.size(), 1u);
        EXPECT_EQ(settings.ResolveFontPath("Body"_intern, 400), "ok.ttf"_intern);
        bool known = true;
        EXPECT_EQ(settings.ResolveFontPath("Empty"_intern, 400, &known), "regular.ttf"_intern);
        EXPECT_FALSE(known);
    }

    TEST(TextSettingsTest, TheShippedConfigMatchesTheDefaults) {
        EngineEnvironment::EnsureCoreServices();
        const XmlNode node = CoreManager::GetConfigManager().GetConfig("TextConfig"_intern);
        ASSERT_TRUE(node);

        const TextSettings shipped = TextSettings::FromConfig(node);
        const TextSettings defaults = TextSettings::Default();

        EXPECT_EQ(shipped.defaultFont, defaults.defaultFont);
        EXPECT_EQ(shipped.pageSize, defaults.pageSize);
        EXPECT_EQ(shipped.defaultFamily, defaults.defaultFamily);
        ASSERT_EQ(shipped.families.size(), 2u);
        EXPECT_EQ(shipped.ResolveFontPath("PT Sans"_intern, 400), "assets/fonts/PT_Sans-Web-Regular.ttf"_intern);
        EXPECT_EQ(shipped.ResolveFontPath("PT Sans"_intern, 700), "assets/fonts/PT_Sans-Web-Bold.ttf"_intern);
        EXPECT_EQ(shipped.ResolveFontPath("Russo One"_intern, 400), "assets/fonts/RussoOne-Regular.ttf"_intern);
        EXPECT_EQ(shipped.ResolveFontPath("Russo One"_intern, 700), "assets/fonts/RussoOne-Regular.ttf"_intern);  // the nearest face
        EXPECT_EQ(shipped.ResolveFontPath(PoolString{}, 400), shipped.defaultFont);
        ASSERT_EQ(shipped.preload.size(), defaults.preload.size());
        for (size_t i = 0; i < defaults.preload.size(); ++i) {
            EXPECT_EQ(shipped.preload[i].first, defaults.preload[i].first);
            EXPECT_EQ(shipped.preload[i].last, defaults.preload[i].last);
        }
    }

}  // namespace BECore::Tests
