#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIStrings.h>
#include <cmath>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// How far a laid-out edge may be from the expected one: layout is float arithmetic on whole design units, so anything above
    /// rounding noise is a real difference. A test whose rects come from scaled or rounded pixels passes its own tolerance
    inline constexpr float kRectTolerance = 1e-3f;

    inline void ExpectRect(const Rect& rect, float x, float y, float w, float h, float tolerance = kRectTolerance) {
        EXPECT_NEAR(rect.x, x, tolerance);
        EXPECT_NEAR(rect.y, y, tolerance);
        EXPECT_NEAR(rect.w, w, tolerance);
        EXPECT_NEAR(rect.h, h, tolerance);
    }

    inline void ExpectRect(const Rect& actual, const Rect& expected, float tolerance = kRectTolerance) {
        ExpectRect(actual, expected.x, expected.y, expected.w, expected.h, tolerance);
    }

    /// Every edge of @p a is within @p tolerance of the same edge of @p b
    [[nodiscard]] inline bool NearRect(const Rect& a, const Rect& b, float tolerance = kRectTolerance) {
        return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.w - b.w) <= tolerance && std::abs(a.h - b.h) <= tolerance;
    }

    /// @p inner lies within @p outer, give or take @p tolerance on every side
    [[nodiscard]] inline bool RectInside(const Rect& inner, const Rect& outer, float tolerance = kRectTolerance) {
        return inner.x >= outer.x - tolerance && inner.y >= outer.y - tolerance && inner.x + inner.w <= outer.x + outer.w + tolerance && inner.y + inner.h <= outer.y + outer.h + tolerance;
    }

    /// Runs @p check once for every language of the strings table and puts the language that was current back. A language added
    /// to the table is covered without touching a test. The twin of ForEachTheme (UIThemeTestSupport.h): a screen is checked once
    /// per language and once per theme
    template <typename Check>
    void ForEachLanguage(Check&& check) {
        UIStrings& strings = CoreManager::GetUIStrings();
        const PoolString saved = strings.GetLanguage();
        const Vector<PoolString> languages = strings.GetLanguages();  // a copy: a check may set more
        for (const PoolString language : languages) {
            ASSERT_TRUE(strings.SetLanguage(language)) << language.ToStringView().data();
            check(language);
        }
        strings.SetLanguage(saved);
    }

}  // namespace BECore::Tests
