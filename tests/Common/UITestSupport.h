#pragma once

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UITextComponent.h>
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

    /// Replaces every @p what in @p text (a placeholder of a template)
    inline void ReplaceAll(String& text, StringView what, StringView with) {
        for (size_t at = text.find(what.data(), 0, what.size()); at != String::npos; at = text.find(what.data(), at + with.size(), what.size())) {
            text.replace(at, what.size(), with.data(), with.size());
        }
    }

    /// Replaces the first @p what in @p text and fails the test when it is not there: a broken copy that changed nothing would prove nothing. Call it through
    /// `ASSERT_NO_FATAL_FAILURE` where the edit must have happened
    inline void Replace(String& text, StringView what, StringView with) {
        const size_t at = text.find(what.data(), 0, what.size());
        ASSERT_NE(at, String::npos) << String(what.data(), what.size()).c_str();
        text.replace(at, what.size(), with.data(), with.size());
    }

    /// The text of a button keeps this much from each edge of its fill
    inline constexpr float kButtonAir = 8.0f;

    /// A button is a text and a clicker on one element (`ui-controls.md`, "Components"): its text is centred on its own fill, so it keeps kButtonAir. A slider, a toggle row, a
    /// segmented group or a dropdown header is interactive too, but its text is a child caption or sits in an area the control leaves it, so it gets no air
    [[nodiscard]] inline bool IsButton(const UIElement& element) {
        return element.GetComponent<UIInputClicker>() != nullptr && element.GetComponent<UITextComponent>() != nullptr;
    }

    /// The text @p element shows (a dropdown header: the chosen caption), empty when it has no text component
    [[nodiscard]] inline String ShownText(const UIElement& element) {
        const UITextComponent* text = element.GetComponent<UITextComponent>();
        return text ? String(text->GetDisplayText(element)) : String();
    }

    /// Every control of the tree that shows text or takes a press, depth first (the root itself is the canvas and has none). The options of a closed popup have no rect and
    /// are not looked at
    inline void CollectControls(const UIElement& element, Vector<const UIElement*>& found) {
        if (element.GetComponent<UITextComponent>() || element.IsInteractive()) {
            found.push_back(&element);
        }
        if (element.HasPopupChildren() && !element.IsPopupOpen()) {
            return;
        }
        for (const auto& child : element.children) {
            CollectControls(*child, found);
        }
    }

    /// The laid-out @p control does not fit what it shows at @p scale, a button with kButtonAir on each side. The validator does not see this for an ellipsis text, which
    /// "can shrink"
    [[nodiscard]] inline bool CutsItsText(const UIElement& control, float scale) {
        const float air = IsButton(control) ? kButtonAir : 0.0f;
        const UIContentFit fit = control.CheckContent(control.rect.w - 2.0f * air, control.rect.h, scale);
        return fit.hasContent && !fit.fits;
    }

    /// The names of the shown controls under @p root that cut their text in the language that is set now, each followed by a space; empty when every word fits
    [[nodiscard]] inline String CutTexts(const UIElement& root, float scale) {
        Vector<const UIElement*> controls;
        CollectControls(root, controls);
        String cut;
        for (const UIElement* control : controls) {
            if (!control->rect.IsEmpty() && CutsItsText(*control, scale)) {
                cut += Format("{} ", control->name.ToStringView());
            }
        }
        return cut;
    }

}  // namespace BECore::Tests
