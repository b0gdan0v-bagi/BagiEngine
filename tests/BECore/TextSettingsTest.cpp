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

    TEST(TextSettingsTest, TheShippedConfigMatchesTheDefaults) {
        EngineEnvironment::EnsureCoreServices();
        const XmlNode node = CoreManager::GetConfigManager().GetConfig("TextConfig"_intern);
        ASSERT_TRUE(node);

        const TextSettings shipped = TextSettings::FromConfig(node);
        const TextSettings defaults = TextSettings::Default();

        EXPECT_EQ(shipped.defaultFont, defaults.defaultFont);
        EXPECT_EQ(shipped.pageSize, defaults.pageSize);
        ASSERT_EQ(shipped.preload.size(), defaults.preload.size());
        for (size_t i = 0; i < defaults.preload.size(); ++i) {
            EXPECT_EQ(shipped.preload[i].first, defaults.preload[i].first);
            EXPECT_EQ(shipped.preload[i].last, defaults.preload[i].last);
        }
    }

}  // namespace BECore::Tests
