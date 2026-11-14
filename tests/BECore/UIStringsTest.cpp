#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UIVars.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String Resolve(UIStrings& strings, const char* text) {
            return strings.Resolve(text);
        }

        /// English and Russian, "menu.start" in both and "menu.quit" in English only
        UIStrings MakeTable() {
            UIStrings strings;
            strings.Set("en"_intern, "menu.start", "Start");
            strings.Set("en"_intern, "menu.quit", "Quit");
            strings.Set("ru"_intern, "menu.start", "Начать");
            return strings;
        }

        Vector<String> Load(UIStrings& strings, const char* xml) {
            auto config = XmlConfig::Create();
            EXPECT_TRUE(config.LoadFromString(xml));
            return strings.LoadFromXml(config.GetRoot());
        }

        bool Mentions(const Vector<String>& problems, const char* word) {
            for (const String& problem : problems) {
                if (problem.find(word) != String::npos) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    TEST(UIStringsTest, ASetStringCanBeFound) {
        UIStrings strings;
        EXPECT_FALSE(strings.Find("menu.start").has_value());

        EXPECT_TRUE(strings.Set("en"_intern, "menu.start", "Start"));

        ASSERT_TRUE(strings.Find("menu.start").has_value());
        EXPECT_EQ(*strings.Find("menu.start"), "Start");
        EXPECT_FALSE(strings.Find("menu.quit").has_value());
    }

    TEST(UIStringsTest, TheFirstLanguageSetIsTheDefaultAndTheCurrentOne) {
        UIStrings strings = MakeTable();

        EXPECT_EQ(strings.GetDefaultLanguage(), "en"_intern);
        EXPECT_EQ(strings.GetLanguage(), "en"_intern);
        ASSERT_EQ(strings.GetLanguages().size(), 2u);
        EXPECT_EQ(strings.GetLanguages()[0], "en"_intern);
        EXPECT_EQ(strings.GetLanguages()[1], "ru"_intern);
    }

    TEST(UIStringsTest, TheCurrentLanguageWinsOverTheDefaultOne) {
        UIStrings strings = MakeTable();

        EXPECT_TRUE(strings.SetLanguage("ru"_intern));

        EXPECT_EQ(strings.GetLanguage(), "ru"_intern);
        EXPECT_EQ(*strings.Find("menu.start"), "Начать");
    }

    TEST(UIStringsTest, AKeyMissingInTheCurrentLanguageFallsBackToTheDefaultOne) {
        UIStrings strings = MakeTable();
        strings.SetLanguage("ru"_intern);

        ASSERT_TRUE(strings.Find("menu.quit").has_value());
        EXPECT_EQ(*strings.Find("menu.quit"), "Quit");
    }

    TEST(UIStringsTest, AKeyMissingEverywhereShowsTheKeyAndIsRecordedOnce) {
        UIStrings strings = MakeTable();

        EXPECT_EQ(Resolve(strings, "{#menu.options}"), "menu.options");
        EXPECT_EQ(Resolve(strings, "{#menu.options}"), "menu.options");
        EXPECT_EQ(Resolve(strings, "a {#menu.options} b"), "a menu.options b");

        ASSERT_EQ(strings.GetMissing().size(), 1u);
        EXPECT_EQ(strings.GetMissing()[0].language, "en"_intern);
        EXPECT_EQ(strings.GetMissing()[0].key, "menu.options");
    }

    TEST(UIStringsTest, AnotherLanguageRecordsTheSameKeyAgain) {
        UIStrings strings = MakeTable();
        Resolve(strings, "{#menu.options}");
        strings.SetLanguage("ru"_intern);

        Resolve(strings, "{#menu.options}");
        Resolve(strings, "{#menu.other}");

        ASSERT_EQ(strings.GetMissing().size(), 3u);
        EXPECT_EQ(strings.GetMissing()[1].language, "ru"_intern);
        EXPECT_EQ(strings.GetMissing()[1].key, "menu.options");
        EXPECT_EQ(strings.GetMissing()[2].key, "menu.other");
    }

    TEST(UIStringsTest, AFoundKeyIsNotRecordedAndFindNeverRecords) {
        UIStrings strings = MakeTable();
        strings.SetLanguage("ru"_intern);

        Resolve(strings, "{#menu.start} {#menu.quit}");
        strings.Find("nothing");

        EXPECT_TRUE(strings.GetMissing().empty());
    }

    TEST(UIStringsTest, ResolvingNeverAddsToThePoolOfNames) {
        UIStrings strings = MakeTable();

        Resolve(strings, "{#never.interned.key.of.uistringstest}");

        EXPECT_FALSE(PoolString::Find("never.interned.key.of.uistringstest").has_value());
    }

    TEST(UIStringsTest, ALanguageWithoutStringsIsRefusedAndTheOldOneKept) {
        UIStrings strings = MakeTable();
        strings.SetLanguage("ru"_intern);
        const uint64_t version = strings.GetVersion();

        EXPECT_FALSE(strings.SetLanguage("de"_intern));
        EXPECT_FALSE(strings.SetLanguage(PoolString{}));

        EXPECT_EQ(strings.GetLanguage(), "ru"_intern);
        EXPECT_EQ(strings.GetVersion(), version);
        EXPECT_FALSE(strings.HasLanguage("de"_intern));
        EXPECT_TRUE(strings.HasLanguage("ru"_intern));
    }

    TEST(UIStringsTest, ABadKeyOrLanguageIsRefused) {
        UIStrings strings;

        EXPECT_FALSE(strings.Set("en"_intern, "", "x"));
        EXPECT_FALSE(strings.Set("en"_intern, "a}b", "x"));
        EXPECT_FALSE(strings.Set(PoolString{}, "key", "x"));

        EXPECT_TRUE(strings.GetLanguages().empty());
        EXPECT_EQ(strings.GetVersion(), 0u);
    }

    TEST(UIStringsTest, TheVersionGrowsOnAChangeOnly) {
        UIStrings strings;
        EXPECT_EQ(strings.GetVersion(), 0u);

        strings.Set("en"_intern, "k", "one");
        const uint64_t afterSet = strings.GetVersion();
        EXPECT_GT(afterSet, 0u);

        strings.Set("en"_intern, "k", "one");
        EXPECT_EQ(strings.GetVersion(), afterSet);

        strings.Set("en"_intern, "k", "two");
        const uint64_t afterChange = strings.GetVersion();
        EXPECT_GT(afterChange, afterSet);

        strings.Set("ru"_intern, "k", "ru");
        const uint64_t afterSecond = strings.GetVersion();

        EXPECT_TRUE(strings.SetLanguage("ru"_intern));
        const uint64_t afterSwitch = strings.GetVersion();
        EXPECT_GT(afterSwitch, afterSecond);

        EXPECT_TRUE(strings.SetLanguage("ru"_intern));
        EXPECT_EQ(strings.GetVersion(), afterSwitch);

        strings.SetDefaultLanguage("ru"_intern);
        const uint64_t afterDefault = strings.GetVersion();
        EXPECT_GT(afterDefault, afterSwitch);
        strings.SetDefaultLanguage("ru"_intern);
        EXPECT_EQ(strings.GetVersion(), afterDefault);

        strings.Clear();
        EXPECT_GT(strings.GetVersion(), afterDefault);
    }

    TEST(UIStringsTest, ClearDropsEverythingAndTheVersionNeverGoesBack) {
        UIStrings strings = MakeTable();
        strings.SetLanguage("ru"_intern);
        Resolve(strings, "{#missing}");
        const uint64_t before = strings.GetVersion();

        strings.Clear();

        EXPECT_TRUE(strings.GetLanguages().empty());
        EXPECT_TRUE(strings.GetMissing().empty());
        EXPECT_TRUE(strings.GetDefaultLanguage().Empty());
        EXPECT_TRUE(strings.GetLanguage().Empty());
        EXPECT_FALSE(strings.Find("menu.start").has_value());
        EXPECT_GT(strings.GetVersion(), before);

        strings.Clear();
        EXPECT_GT(strings.GetVersion(), before + 1);
    }

    TEST(UIStringsTest, ChangingTheDefaultLanguageMovesTheEffectiveOneUntilALanguageIsPicked) {
        UIStrings strings = MakeTable();

        strings.SetDefaultLanguage("ru"_intern);
        EXPECT_EQ(strings.GetLanguage(), "ru"_intern);
        EXPECT_EQ(*strings.Find("menu.start"), "Начать");

        strings.SetLanguage("en"_intern);
        strings.SetDefaultLanguage("ru"_intern);
        EXPECT_EQ(strings.GetLanguage(), "en"_intern);
    }

    TEST(UIStringsTest, ResolveReplacesEveryReference) {
        UIStrings strings = MakeTable();

        EXPECT_EQ(Resolve(strings, "{#menu.start}"), "Start");
        EXPECT_EQ(Resolve(strings, "{#menu.start}/{#menu.quit}"), "Start/Quit");
        EXPECT_EQ(Resolve(strings, "[ {#menu.start} ] and ({#menu.quit})"), "[ Start ] and (Quit)");
        EXPECT_EQ(Resolve(strings, "plain text"), "plain text");
        EXPECT_EQ(Resolve(strings, ""), "");

        strings.SetLanguage("ru"_intern);
        EXPECT_EQ(Resolve(strings, "{#menu.start}: {#menu.quit}"), "Начать: Quit");
    }

    TEST(UIStringsTest, VariablesAndEscapesAreLeftForTheVariableStep) {
        UIStrings strings = MakeTable();
        strings.Set("en"_intern, "score.title", "Score");

        EXPECT_EQ(Resolve(strings, "{#score.title}: {score}"), "Score: {score}");
        EXPECT_EQ(Resolve(strings, "{{#menu.start}}"), "{{#menu.start}}");
        EXPECT_EQ(Resolve(strings, "{{ {#menu.start} }}"), "{{ Start }}");

        UIVars vars;
        vars.Set("score"_intern, 120);
        EXPECT_EQ(vars.Expand(Resolve(strings, "{{#menu.start}}")), "{#menu.start}");
        EXPECT_EQ(vars.Expand(Resolve(strings, "{#score.title}: {score}")), "Score: 120");
    }

    TEST(UIStringsTest, AStringIsNotResolvedAgainAndKeepsItsVariables) {
        UIStrings strings;
        strings.Set("en"_intern, "a", "see {#b}");
        strings.Set("en"_intern, "b", "bee");
        strings.Set("en"_intern, "points", "{score} points {{x}}");

        EXPECT_EQ(Resolve(strings, "{#a}"), "see {#b}");
        EXPECT_EQ(Resolve(strings, "{#points}"), "{score} points {{x}}");

        UIVars vars;
        vars.Set("score"_intern, 7);
        EXPECT_EQ(vars.Expand(Resolve(strings, "{#points}")), "7 points {x}");
        EXPECT_TRUE(strings.GetMissing().empty());
    }

    TEST(UIStringsTest, AReferenceInsideAStringIsReadAsAVariableByTheVariableStepSoALiteralOneIsDoubled) {
        UIStrings strings;
        strings.Set("en"_intern, "a", "see {#b}");
        strings.Set("en"_intern, "literal", "see {{#b}}");
        strings.Set("en"_intern, "b", "bee");
        UIVars vars;

        // `{#b}` left in a string is not a second string step: `UIVars::Expand` reads it as the variable named "#b", which nobody set, so it ends as nothing
        EXPECT_EQ(vars.Expand(Resolve(strings, "{#a}")), "see ");
        vars.Set("#b"_intern, "the variable");
        EXPECT_EQ(vars.Expand(Resolve(strings, "{#a}")), "see the variable");

        // a translation that wants the literal text `{#b}` writes it with doubled braces
        EXPECT_EQ(vars.Expand(Resolve(strings, "{#literal}")), "see {#b}");
    }

    TEST(UIStringsTest, AnUnclosedOrEmptyReferenceStaysAsWritten) {
        UIStrings strings = MakeTable();

        EXPECT_EQ(Resolve(strings, "{#menu.start"), "{#menu.start");
        EXPECT_EQ(Resolve(strings, "a {# b"), "a {# b");
        EXPECT_EQ(Resolve(strings, "{#}"), "{#}");
        EXPECT_EQ(Resolve(strings, "{#} {#menu.start}"), "{#} Start");
        EXPECT_EQ(Resolve(strings, "{menu.start}"), "{menu.start}");
        EXPECT_EQ(Resolve(strings, "#menu.start}"), "#menu.start}");
        EXPECT_TRUE(strings.GetMissing().empty());
    }

    TEST(UIStringsTest, KeysWithDotsAndTextsInOtherScriptsWork) {
        UIStrings strings;
        strings.Set("ru"_intern, "settings.sound.title", "Звук и музыка");
        strings.Set("ru"_intern, "ключ", "значение");
        strings.Set("ru"_intern, "empty", "");

        EXPECT_EQ(Resolve(strings, "{#settings.sound.title}!"), "Звук и музыка!");
        EXPECT_EQ(Resolve(strings, "{#ключ}"), "значение");
        EXPECT_EQ(Resolve(strings, "[{#empty}]"), "[]");
        EXPECT_TRUE(strings.GetMissing().empty());
    }

    TEST(UIStringsTest, HasReferencesIsACheapPreCheck) {
        EXPECT_TRUE(UIStrings::HasReferences("{#a}"));
        EXPECT_TRUE(UIStrings::HasReferences("x {#a.b} y"));
        EXPECT_TRUE(UIStrings::HasReferences("{{ {#a}"));
        EXPECT_FALSE(UIStrings::HasReferences(""));
        EXPECT_FALSE(UIStrings::HasReferences("plain"));
        EXPECT_FALSE(UIStrings::HasReferences("{score}"));
        EXPECT_FALSE(UIStrings::HasReferences("{{#a}}"));
        EXPECT_FALSE(UIStrings::HasReferences("{#a"));
        EXPECT_FALSE(UIStrings::HasReferences("{#}"));
        EXPECT_FALSE(UIStrings::HasReferences("#a}"));
    }

    TEST(UIStringsTest, LoadFromXmlReadsLanguagesAndTheDefault) {
        UIStrings strings;

        const auto problems = Load(strings, R"(
            <UIStrings default="ru">
                <language id="en"><string key="menu.start" text="Start"/><string key="menu.quit" text="Quit"/></language>
                <language id="ru"><string key="menu.start" text="Начать"/></language>
            </UIStrings>)");

        EXPECT_TRUE(problems.empty());
        EXPECT_EQ(strings.GetDefaultLanguage(), "ru"_intern);
        EXPECT_EQ(strings.GetLanguage(), "ru"_intern);
        ASSERT_EQ(strings.GetLanguages().size(), 2u);
        EXPECT_EQ(*strings.Find("menu.start"), "Начать");
        EXPECT_FALSE(strings.Find("menu.quit").has_value());  // only "en" has it, and "en" is neither the current nor the default language
        EXPECT_TRUE(strings.SetLanguage("en"_intern));
        EXPECT_EQ(*strings.Find("menu.start"), "Start");
    }

    TEST(UIStringsTest, LoadFromXmlWithoutADefaultTakesTheFirstLanguage) {
        UIStrings strings;

        const auto problems = Load(strings, R"(<UIStrings><language id="de"><string key="k" text="Ja"/></language><language id="en"><string key="k" text="Yes"/></language></UIStrings>)");

        EXPECT_TRUE(problems.empty());
        EXPECT_EQ(strings.GetDefaultLanguage(), "de"_intern);
        EXPECT_EQ(*strings.Find("k"), "Ja");
    }

    TEST(UIStringsTest, LoadFromXmlReportsEveryKindOfProblemAndKeepsTheRest) {
        UIStrings strings;

        const auto problems = Load(strings, R"(
            <UIStrings default="fr">
                <language id="en">
                    <string key="ok" text="fine"/>
                    <string key="dup" text="first"/>
                    <string key="dup" text="second"/>
                    <string text="no key"/>
                    <string key="" text="empty key"/>
                    <string key="notext"/>
                    <string key="bad}key" text="x"/>
                    <word key="k" text="not a string"/>
                </language>
                <language><string key="orphan" text="x"/></language>
                <language id="empty"/>
                <region id="x"/>
            </UIStrings>)");

        EXPECT_EQ(problems.size(), 10u) << problems.size();
        EXPECT_TRUE(Mentions(problems, "'dup' is set twice"));
        EXPECT_TRUE(Mentions(problems, "has no key"));
        EXPECT_TRUE(Mentions(problems, "'notext' has no text"));
        EXPECT_TRUE(Mentions(problems, "'bad}key'"));
        EXPECT_TRUE(Mentions(problems, "unknown element 'word'"));
        EXPECT_TRUE(Mentions(problems, "has no id"));
        EXPECT_TRUE(Mentions(problems, "'empty' has no strings"));
        EXPECT_TRUE(Mentions(problems, "unknown element: region"));
        EXPECT_TRUE(Mentions(problems, "default language 'fr'"));

        EXPECT_EQ(*strings.Find("ok"), "fine");
        EXPECT_EQ(*strings.Find("dup"), "first");
        EXPECT_FALSE(strings.Find("orphan").has_value());
        EXPECT_FALSE(strings.Find("k").has_value());
        EXPECT_EQ(strings.GetLanguages().size(), 1u);
        EXPECT_EQ(strings.GetDefaultLanguage(), "en"_intern);
    }

    TEST(UIStringsTest, LoadFromXmlOfAnEmptyNodeAddsNothing) {
        UIStrings strings;

        EXPECT_TRUE(strings.LoadFromXml(XmlNode{}).empty());
        EXPECT_TRUE(Load(strings, "<UIStrings/>").empty());
        EXPECT_TRUE(strings.GetLanguages().empty());
        EXPECT_EQ(strings.GetVersion(), 0u);
    }

    TEST(UIStringsTest, LoadFromXmlAddsToWhatTheTableHolds) {
        UIStrings strings = MakeTable();

        const auto problems = Load(strings, R"(<UIStrings><language id="en"><string key="menu.start" text="Go"/><string key="extra" text="More"/></language></UIStrings>)");

        EXPECT_EQ(problems.size(), 1u);  // "menu.start" is set twice: the table's value stays
        EXPECT_EQ(*strings.Find("menu.start"), "Start");
        EXPECT_EQ(*strings.Find("extra"), "More");
    }

    TEST(UIStringsTest, TheXmlTextMayHoldVariablesEscapesAndLineBreaks) {
        UIStrings strings;
        Load(strings, R"(<UIStrings><language id="en"><string key="k" text="{score} &amp; {{x}}&#10;next"/></language></UIStrings>)");

        EXPECT_EQ(*strings.Find("k"), "{score} & {{x}}\nnext");
    }

    TEST(UIStringsTest, TheCoreManagerHoldsAnEmptyTableWhenNoGameShipsOne) {
        EngineEnvironment::EnsureCoreServices();
        UIStrings& strings = CoreManager::GetUIStrings();
        strings.Clear();

        EXPECT_TRUE(strings.GetLanguages().empty());
        EXPECT_TRUE(strings.GetLanguage().Empty());
        EXPECT_EQ(&strings, &CoreManager::GetUIStrings());

        UIStrings loaded;
        loaded.Initialize();  // no UIStrings config is mounted: an empty table, no warning
        EXPECT_TRUE(loaded.GetLanguages().empty());

        strings.Set("en"_intern, "k", "v");
        EXPECT_EQ(*CoreManager::GetUIStrings().Find("k"), "v");

        strings.Clear();  // the table is global: leave it empty for the next test
    }

}  // namespace BECore::Tests
