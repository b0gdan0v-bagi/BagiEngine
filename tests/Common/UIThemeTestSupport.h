#pragma once

#include <BECore/UI/UIThemes.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// Runs @p check once for every theme of the table and puts the theme that was current back. A theme added to the table is covered without touching a test. The same shape as
    /// ForEachLanguage of the screen tests; use it beside that one, a screen is checked once per language and once per theme
    template <typename Check>
    void ForEachTheme(UIThemes& themes, Check&& check) {
        const PoolString saved = themes.GetTheme();
        const eastl::vector<PoolString> ids = themes.GetThemes();  // a copy: a check may set more
        for (const PoolString theme : ids) {
            ASSERT_TRUE(themes.SetTheme(theme)) << theme.ToStringView().data();
            check(theme);
        }
        themes.SetTheme(saved);
    }

    /// What is missing in the table, one problem to a line: a theme that does not define a token another theme defines (it would show the default theme's colour: allowed, but a game
    /// that wants every theme complete says so with ExpectThemesComplete). Empty when every theme defines every token the table knows
    inline String MissingThemeTokens(const UIThemes& themes) {
        String problems;
        for (const PoolString theme : themes.GetThemes()) {
            for (const PoolString token : themes.GetTokens()) {
                if (!themes.HasToken(theme, token.ToStringView())) {
                    problems += Format("theme '{}' does not define the token '{}'\n", theme.ToStringView(), token.ToStringView());
                }
            }
        }
        return problems;
    }

    /// Every theme defines every token the table knows; the failure names the theme and the token
    inline void ExpectThemesComplete(const UIThemes& themes) {
        const String problems = MissingThemeTokens(themes);
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

}  // namespace BECore::Tests
