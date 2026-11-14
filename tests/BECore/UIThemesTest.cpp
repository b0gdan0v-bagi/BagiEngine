#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIThemes.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr Color LightPrimary{51, 170, 102, 255};
        constexpr Color DarkPrimary{0, 90, 40, 255};
        constexpr Color LightText{20, 20, 20, 255};

        /// "light" with `primary` and `text`, "dark" with `primary` only
        UIThemes MakeTable() {
            UIThemes themes;
            themes.Set("light"_intern, "primary", LightPrimary);
            themes.Set("light"_intern, "text", LightText);
            themes.Set("dark"_intern, "primary", DarkPrimary);
            return themes;
        }

        Vector<String> Load(UIThemes& themes, const char* xml) {
            auto config = XmlConfig::Create();
            EXPECT_TRUE(config.LoadFromString(xml));
            return themes.LoadFromXml(config.GetRoot());
        }

        bool Mentions(const Vector<String>& problems, const char* word) {
            for (const String& problem : problems) {
                if (problem.find(word) != String::npos) {
                    return true;
                }
            }
            return false;
        }

        class UIThemesTest : public ::testing::Test {
        protected:
            void TearDown() override {
                CoreManager::GetUIThemes().Clear();  // the table is global: the next test starts with none, and with no current theme
            }
        };

    }  // namespace

    TEST_F(UIThemesTest, ASetColorCanBeFound) {
        UIThemes themes;
        EXPECT_FALSE(themes.Find("primary").has_value());

        EXPECT_TRUE(themes.Set("light"_intern, "primary", LightPrimary));

        ASSERT_TRUE(themes.Find("primary").has_value());
        EXPECT_TRUE(ColorsEqual(*themes.Find("primary"), LightPrimary));
        EXPECT_FALSE(themes.Find("text").has_value());
    }

    TEST_F(UIThemesTest, TheFirstThemeSetIsTheDefaultAndTheCurrentOne) {
        UIThemes themes = MakeTable();

        EXPECT_EQ(themes.GetDefaultTheme(), "light"_intern);
        EXPECT_EQ(themes.GetTheme(), "light"_intern);
        ASSERT_EQ(themes.GetThemes().size(), 2u);
        EXPECT_EQ(themes.GetThemes()[0], "light"_intern);
        EXPECT_EQ(themes.GetThemes()[1], "dark"_intern);
    }

    TEST_F(UIThemesTest, TheCurrentThemeWinsOverTheDefaultOne) {
        UIThemes themes = MakeTable();

        EXPECT_TRUE(themes.SetTheme("dark"_intern));

        EXPECT_EQ(themes.GetTheme(), "dark"_intern);
        EXPECT_TRUE(ColorsEqual(*themes.Find("primary"), DarkPrimary));
    }

    TEST_F(UIThemesTest, ATokenMissingInTheCurrentThemeFallsBackToTheDefaultOne) {
        UIThemes themes = MakeTable();
        themes.SetTheme("dark"_intern);

        ASSERT_TRUE(themes.Find("text").has_value());
        EXPECT_TRUE(ColorsEqual(*themes.Find("text"), LightText));
        EXPECT_FALSE(themes.HasToken("dark"_intern, "text"));
    }

    TEST_F(UIThemesTest, ATokenOfNoThemeIsNotFoundAndFindingNeverAddsToThePoolOfNames) {
        UIThemes themes = MakeTable();

        EXPECT_FALSE(themes.Find("never-interned-token-of-uithemestest").has_value());

        EXPECT_FALSE(PoolString::Find("never-interned-token-of-uithemestest").has_value());
    }

    TEST_F(UIThemesTest, AThemeWithoutColorsIsRefusedAndTheOldOneKept) {
        UIThemes themes = MakeTable();
        themes.SetTheme("dark"_intern);
        const uint64_t version = themes.GetVersion();

        EXPECT_FALSE(themes.SetTheme("sepia"_intern));
        EXPECT_FALSE(themes.SetTheme(PoolString{}));

        EXPECT_EQ(themes.GetTheme(), "dark"_intern);
        EXPECT_EQ(themes.GetVersion(), version);
        EXPECT_FALSE(themes.HasTheme("sepia"_intern));
        EXPECT_TRUE(themes.HasTheme("dark"_intern));
    }

    TEST_F(UIThemesTest, ABadTokenNameOrAnEmptyThemeIsRefused) {
        UIThemes themes;

        EXPECT_FALSE(themes.Set("light"_intern, "", LightPrimary));
        EXPECT_FALSE(themes.Set("light"_intern, "1st", LightPrimary));
        EXPECT_FALSE(themes.Set("light"_intern, "-x", LightPrimary));
        EXPECT_FALSE(themes.Set("light"_intern, "_x", LightPrimary));
        EXPECT_FALSE(themes.Set("light"_intern, "a b", LightPrimary));
        EXPECT_FALSE(themes.Set("light"_intern, "a.b", LightPrimary));
        EXPECT_FALSE(themes.Set("light"_intern, "@a", LightPrimary));
        EXPECT_FALSE(themes.Set(PoolString{}, "primary", LightPrimary));

        EXPECT_TRUE(themes.GetThemes().empty());
        EXPECT_TRUE(themes.GetTokens().empty());
        EXPECT_EQ(themes.GetVersion(), 1u);
    }

    TEST_F(UIThemesTest, ATokenNameIsALetterThenLettersDigitsUnderscoresAndHyphens) {
        EXPECT_TRUE(UIThemes::IsValidToken("primary"));
        EXPECT_TRUE(UIThemes::IsValidToken("Panel2"));
        EXPECT_TRUE(UIThemes::IsValidToken("text-muted_2"));
        EXPECT_FALSE(UIThemes::IsValidToken(""));
        EXPECT_FALSE(UIThemes::IsValidToken("2panel"));
        EXPECT_FALSE(UIThemes::IsValidToken("panel color"));
        EXPECT_FALSE(UIThemes::IsValidToken("panel)"));
    }

    TEST_F(UIThemesTest, TheVersionStartsAtOneAndGrowsOnAChangeOnly) {
        UIThemes themes;
        EXPECT_EQ(themes.GetVersion(), 1u);

        themes.Set("light"_intern, "primary", LightPrimary);
        const uint64_t afterSet = themes.GetVersion();
        EXPECT_GT(afterSet, 1u);

        themes.Set("light"_intern, "primary", LightPrimary);
        EXPECT_EQ(themes.GetVersion(), afterSet);

        themes.Set("light"_intern, "primary", DarkPrimary);
        const uint64_t afterChange = themes.GetVersion();
        EXPECT_GT(afterChange, afterSet);

        themes.Set("dark"_intern, "primary", DarkPrimary);
        const uint64_t afterSecond = themes.GetVersion();

        EXPECT_TRUE(themes.SetTheme("dark"_intern));
        const uint64_t afterSwitch = themes.GetVersion();
        EXPECT_GT(afterSwitch, afterSecond);

        EXPECT_TRUE(themes.SetTheme("dark"_intern));
        EXPECT_EQ(themes.GetVersion(), afterSwitch);

        themes.SetDefaultTheme("dark"_intern);
        const uint64_t afterDefault = themes.GetVersion();
        EXPECT_GT(afterDefault, afterSwitch);
        themes.SetDefaultTheme("dark"_intern);
        EXPECT_EQ(themes.GetVersion(), afterDefault);

        themes.Clear();
        EXPECT_GT(themes.GetVersion(), afterDefault);
    }

    TEST_F(UIThemesTest, ChoosingTheCurrentThemeAsTheEffectiveOneKeepsTheVersion) {
        UIThemes themes = MakeTable();
        const uint64_t version = themes.GetVersion();

        EXPECT_TRUE(themes.SetTheme("light"_intern));  // the default is the effective theme already

        EXPECT_EQ(themes.GetVersion(), version);
    }

    TEST_F(UIThemesTest, ClearDropsEverythingAndTheVersionNeverGoesBack) {
        UIThemes themes = MakeTable();
        themes.SetTheme("dark"_intern);
        const uint64_t before = themes.GetVersion();

        themes.Clear();

        EXPECT_TRUE(themes.GetThemes().empty());
        EXPECT_TRUE(themes.GetTokens().empty());
        EXPECT_TRUE(themes.GetDefaultTheme().Empty());
        EXPECT_TRUE(themes.GetTheme().Empty());
        EXPECT_FALSE(themes.Find("primary").has_value());
        EXPECT_GT(themes.GetVersion(), before);

        themes.Clear();
        EXPECT_GT(themes.GetVersion(), before + 1);
    }

    TEST_F(UIThemesTest, TheTokensAreTheUnionOfTheThemesAndHasTokenLooksAtOneTheme) {
        UIThemes themes = MakeTable();
        themes.Set("dark"_intern, "accent", DarkPrimary);

        ASSERT_EQ(themes.GetTokens().size(), 3u);
        EXPECT_EQ(themes.GetTokens()[0], "primary"_intern);
        EXPECT_EQ(themes.GetTokens()[1], "text"_intern);
        EXPECT_EQ(themes.GetTokens()[2], "accent"_intern);
        EXPECT_TRUE(themes.HasToken("light"_intern, "primary"));
        EXPECT_TRUE(themes.HasToken("light"_intern, "text"));
        EXPECT_FALSE(themes.HasToken("light"_intern, "accent"));
        EXPECT_FALSE(themes.HasToken("dark"_intern, "text"));
        EXPECT_FALSE(themes.HasToken("sepia"_intern, "primary"));
    }

    TEST_F(UIThemesTest, ADefaultThemeWithNoColorsIsATableWithNoFallback) {
        UIThemes themes = MakeTable();
        themes.SetDefaultTheme("sepia"_intern);
        themes.SetTheme("dark"_intern);

        EXPECT_TRUE(themes.Find("primary").has_value());
        EXPECT_FALSE(themes.Find("text").has_value());
    }

    TEST_F(UIThemesTest, LoadFromXmlReadsThemesColorsAndTheDefault) {
        UIThemes themes;

        const auto problems = Load(themes, R"(<UIThemes default="dark">
            <theme id="light"><color name="primary" value="#33aa66"/><color name="text" value="20,20,20,255"/></theme>
            <theme id="dark"><color name="primary" value="#005A28FF"/></theme>
        </UIThemes>)");

        EXPECT_TRUE(problems.empty());
        EXPECT_EQ(themes.GetDefaultTheme(), "dark"_intern);
        EXPECT_TRUE(ColorsEqual(*themes.Find("primary"), DarkPrimary));
        EXPECT_FALSE(themes.Find("text").has_value());
        themes.SetTheme("light"_intern);
        EXPECT_TRUE(ColorsEqual(*themes.Find("primary"), LightPrimary));
        EXPECT_TRUE(ColorsEqual(*themes.Find("text"), LightText));
    }

    TEST_F(UIThemesTest, LoadFromXmlReportsWhatItSkipsAndLoadsTheRest) {
        UIThemes themes;

        const auto problems = Load(themes, R"(<UIThemes default="sepia">
            <theme><color name="a" value="#000000"/></theme>
            <theme id="light">
                <color value="#000000"/>
                <color name="1bad" value="#000000"/>
                <color name="nov"/>
                <color name="notcolor" value="purple-ish"/>
                <color name="dup" value="#111111"/>
                <color name="dup" value="#222222"/>
                <color name="good" value="#333333"/>
                <stray/>
            </theme>
            <theme id="empty"/>
            <thing/>
        </UIThemes>)");

        EXPECT_TRUE(Mentions(problems, "has no id"));
        EXPECT_TRUE(Mentions(problems, "has no name"));
        EXPECT_TRUE(Mentions(problems, "1bad"));
        EXPECT_TRUE(Mentions(problems, "'nov' has no value"));
        EXPECT_TRUE(Mentions(problems, "purple-ish"));
        EXPECT_TRUE(Mentions(problems, "'dup' is set twice"));
        EXPECT_TRUE(Mentions(problems, "stray"));
        EXPECT_TRUE(Mentions(problems, "'empty' has no colours"));
        EXPECT_TRUE(Mentions(problems, "thing"));
        EXPECT_TRUE(Mentions(problems, "default theme 'sepia'"));
        EXPECT_EQ(problems.size(), 10u);

        EXPECT_EQ(themes.GetDefaultTheme(), "light"_intern);
        ASSERT_EQ(themes.GetThemes().size(), 1u);
        EXPECT_TRUE(ColorsEqual(*themes.Find("dup"), Color{17, 17, 17, 255}));
        EXPECT_TRUE(ColorsEqual(*themes.Find("good"), Color{51, 51, 51, 255}));
        EXPECT_EQ(themes.GetTokens().size(), 2u);
    }

    TEST_F(UIThemesTest, LoadFromXmlOfAnEmptyNodeAddsNothing) {
        UIThemes themes;

        EXPECT_TRUE(themes.LoadFromXml(XmlNode{}).empty());
        EXPECT_TRUE(Load(themes, "<UIThemes/>").empty());

        EXPECT_TRUE(themes.GetThemes().empty());
        EXPECT_EQ(themes.GetVersion(), 1u);
    }

    TEST_F(UIThemesTest, TheEngineHoldsATableAndShipsNoThemes) {
        UIThemes& themes = CoreManager::GetUIThemes();

        EXPECT_TRUE(themes.GetThemes().empty());
        EXPECT_TRUE(themes.Set("light"_intern, "primary", LightPrimary));
        EXPECT_TRUE(ColorsEqual(*CoreManager::GetUIThemes().Find("primary"), LightPrimary));
    }

}  // namespace BECore::Tests
