#include <BECore/UI/UIVars.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String Expand(const UIVars& vars, const char* text) {
            return vars.Expand(text);
        }

    }  // namespace

    TEST(UIVarsTest, ASetValueCanBeRead) {
        UIVars vars;
        EXPECT_FALSE(vars.Has("score"_intern));
        EXPECT_EQ(vars.Get("score"_intern), "");

        vars.Set("score"_intern, "120");
        EXPECT_TRUE(vars.Has("score"_intern));
        EXPECT_EQ(vars.Get("score"_intern), "120");

        vars.Set("score"_intern, "130");
        EXPECT_EQ(vars.Get("score"_intern), "130");
    }

    TEST(UIVarsTest, NumbersAreWrittenLikeFormatWritesThem) {
        UIVars vars;
        vars.Set("int"_intern, 120);
        vars.Set("float"_intern, 0.5f);
        vars.Set("negative"_intern, -3);
        vars.Set("flag"_intern, true);

        EXPECT_EQ(vars.Get("int"_intern), "120");
        EXPECT_EQ(vars.Get("float"_intern), "0.5");
        EXPECT_EQ(vars.Get("negative"_intern), "-3");
        EXPECT_EQ(vars.Get("flag"_intern), "true");
    }

    TEST(UIVarsTest, TheVersionGrowsOnlyWhenSomethingChanged) {
        UIVars vars;
        const uint64_t start = vars.GetVersion();

        vars.Set("a"_intern, "1");
        const uint64_t afterFirst = vars.GetVersion();
        EXPECT_GT(afterFirst, start);

        vars.Set("a"_intern, "1");  // the same value: nothing for a label to do
        EXPECT_EQ(vars.GetVersion(), afterFirst);

        vars.Set("a"_intern, "2");
        const uint64_t afterChange = vars.GetVersion();
        EXPECT_GT(afterChange, afterFirst);

        EXPECT_FALSE(vars.Remove("missing"_intern));
        EXPECT_EQ(vars.GetVersion(), afterChange);
        EXPECT_TRUE(vars.Remove("a"_intern));
        EXPECT_GT(vars.GetVersion(), afterChange);
        EXPECT_FALSE(vars.Has("a"_intern));
    }

    TEST(UIVarsTest, ClearDropsEverythingAndTheVersionKeepsGrowing) {
        UIVars vars;
        vars.Set("a"_intern, "1");
        vars.Set("b"_intern, "2");
        const uint64_t before = vars.GetVersion();

        vars.Clear();

        EXPECT_FALSE(vars.Has("a"_intern));
        EXPECT_FALSE(vars.Has("b"_intern));
        EXPECT_GT(vars.GetVersion(), before) << "a label that showed the old values must notice";
    }

    TEST(UIVarsTest, PlaceholdersAreReplaced) {
        UIVars vars;
        vars.Set("score"_intern, 120);
        vars.Set("best"_intern, "300");

        EXPECT_EQ(Expand(vars, "Score: {score}"), "Score: 120");
        EXPECT_EQ(Expand(vars, "{score}/{best}"), "120/300");
        EXPECT_EQ(Expand(vars, "{score}{best}"), "120300");
        EXPECT_EQ(Expand(vars, "no placeholders"), "no placeholders");
        EXPECT_EQ(Expand(vars, ""), "");
    }

    TEST(UIVarsTest, AnUnsetVariableIsEmpty) {
        UIVars vars;
        EXPECT_EQ(Expand(vars, "Score: {score}!"), "Score: !");
        EXPECT_EQ(Expand(vars, "{}"), "");
    }

    TEST(UIVarsTest, DoubledBracesAreLiteral) {
        UIVars vars;
        vars.Set("a"_intern, "X");

        EXPECT_EQ(Expand(vars, "{{a}}"), "{a}");
        EXPECT_EQ(Expand(vars, "{{{a}}}"), "{X}");
        EXPECT_EQ(Expand(vars, "a }} b"), "a } b");
    }

    TEST(UIVarsTest, AnUnclosedBraceStaysAsItIs) {
        UIVars vars;
        vars.Set("a"_intern, "X");

        EXPECT_EQ(Expand(vars, "open { brace"), "open { brace");
        EXPECT_EQ(Expand(vars, "{a"), "{a");
        EXPECT_EQ(Expand(vars, "a} lone"), "a} lone");
    }

    TEST(UIVarsTest, TextInAnyScriptPasses) {
        UIVars vars;
        vars.Set("name"_intern, "Мир");

        EXPECT_EQ(Expand(vars, "Привет, {name}!"), "Привет, Мир!");
    }

    TEST(UIVarsTest, OnlyATextWithBracesNeedsExpanding) {
        EXPECT_FALSE(UIVars::HasPlaceholders("plain"));
        EXPECT_TRUE(UIVars::HasPlaceholders("Score: {score}"));
        EXPECT_TRUE(UIVars::HasPlaceholders("{{literal}}"));
    }

}  // namespace BECore::Tests
