#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIStateLook.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr Color kFill{100, 110, 120, 255};
        constexpr Color kEnd{60, 80, 150, 255};
        constexpr Color kBorder{200, 150, 100, 255};
        constexpr Color kAccent{250, 240, 230, 255};
        constexpr Color kHighlight{255, 255, 255, 80};
        constexpr Rect kRect{10.0f, 20.0f, 100.0f, 40.0f};

        constexpr UIStateLook::State kNormal{};
        constexpr UIStateLook::State kHover{true, false, false, false};
        constexpr UIStateLook::State kPressed{true, true, false, false};
        constexpr UIStateLook::State kSelected{false, false, false, true};
        constexpr UIStateLook::State kDisabled{false, false, true, false};

        UIBoxStyle StyledBox() {
            UIBoxStyle box;
            box.cornerRadius = UICorners::Uniform(8.0f);
            box.borderWidth = 2.0f;
            box.borderColor = kBorder;
            box.gradientDirection = UIGradientDirection::Vertical;
            box.gradientColor = kEnd;
            box.shadow = {0.0f, 4.0f, 12.0f, 0.0f, Color{0, 0, 0, 160}};
            box.highlight = kHighlight;
            return box;
        }

        UIStateLook::Look Resolve(const UIBoxStyle& box, const UIStateLook::State& state, const UIStateLook::Colors& colors = {}) {
            return UIStateLook::ResolveBox(box, kFill, colors, state, kAccent);
        }

        void ExpectColor(const Color& actual, const Color& expected, const char* what) {
            EXPECT_TRUE(ColorsEqual(actual, expected)) << what << ": got " << int(actual.r) << "," << int(actual.g) << "," << int(actual.b) << "," << int(actual.a) << " expected " << int(expected.r)
                                                       << "," << int(expected.g) << "," << int(expected.b) << "," << int(expected.a);
        }

    }  // namespace

    // ---- ResolveBox -------------------------------------------------------------------------------------------------------

    TEST(UIStateLookTest, ANormalStateKeepsTheBoxAndTheFills) {
        const auto look = Resolve(StyledBox(), kNormal);
        EXPECT_TRUE(look.box == StyledBox());
        ExpectColor(look.fill, kFill, "fill");
        ExpectColor(look.gradientEnd, kEnd, "gradient end");
    }

    TEST(UIStateLookTest, HoverLightensBothEndsAndTheBorderAndLeavesTheShadow) {
        const auto look = Resolve(StyledBox(), kHover);
        ExpectColor(look.fill, ShiftColor(kFill, kHoverBrighten), "fill");
        ExpectColor(look.gradientEnd, ShiftColor(kEnd, kHoverBrighten), "gradient end");
        ExpectColor(look.box.borderColor, ShiftColor(kBorder, kHoverBrighten), "border");
        EXPECT_TRUE(look.box.shadow == StyledBox().shadow);
        ExpectColor(look.box.highlight, kHighlight, "highlight");
    }

    TEST(UIStateLookTest, PressDarkensShrinksTheShadowAndDropsTheHighlight) {
        const auto look = Resolve(StyledBox(), kPressed);
        ExpectColor(look.fill, ShiftColor(kFill, -kPressDarken), "fill");
        ExpectColor(look.gradientEnd, ShiftColor(kEnd, -kPressDarken), "gradient end");
        ExpectColor(look.box.borderColor, ShiftColor(kBorder, -kPressDarken), "border");
        EXPECT_FLOAT_EQ(look.box.shadow.offsetY, 4.0f * UIStateLook::kPressShadowOffsetScale);
        EXPECT_FLOAT_EQ(look.box.shadow.blur, 12.0f * UIStateLook::kPressShadowBlurScale);
        EXPECT_EQ(look.box.highlight.a, 0);
    }

    TEST(UIStateLookTest, PressedWinsOverHoveredWhenBothAreSet) {
        const auto both = Resolve(StyledBox(), {true, true, false, false});
        const auto pressed = Resolve(StyledBox(), kPressed);
        ExpectColor(both.fill, pressed.fill, "fill");
    }

    TEST(UIStateLookTest, SelectedBrightensRaisesTheBorderToTheAccentAndAddsAGlow) {
        const auto look = Resolve(StyledBox(), kSelected);
        ExpectColor(look.fill, ShiftColor(kFill, UIStateLook::kSelectedBrighten), "fill");
        ExpectColor(look.gradientEnd, ShiftColor(kEnd, UIStateLook::kSelectedBrighten), "gradient end");
        ExpectColor(look.box.borderColor, kAccent, "border");
        EXPECT_FLOAT_EQ(look.box.borderWidth, 2.0f) << "a border wider than the least keeps its width";
        ExpectColor(look.box.glow.color, WithAlpha(kAccent, UIStateLook::kSelectedGlowAlpha), "glow");
        EXPECT_FLOAT_EQ(look.box.glow.blur, UIStateLook::kSelectedGlowBlur);
    }

    TEST(UIStateLookTest, SelectedGivesABorderlessBoxTheLeastBorderAndKeepsAnAuthoredGlow) {
        UIBoxStyle box = StyledBox();
        box.borderWidth = 0.0f;
        box.glow = {1.0f, 2.0f, 3.0f, 4.0f, Color{9, 8, 7, 6}};
        const auto look = Resolve(box, kSelected);
        EXPECT_FLOAT_EQ(look.box.borderWidth, UIStateLook::kSelectedBorderMin);
        EXPECT_TRUE(look.box.glow == box.glow);
    }

    TEST(UIStateLookTest, SelectedAndHoveredIsBrighterThanHoveredAlone) {
        const auto selected = Resolve(StyledBox(), {true, false, false, true});
        const auto hovered = Resolve(StyledBox(), kHover);
        EXPECT_GT(selected.fill.r, hovered.fill.r);
    }

    TEST(UIStateLookTest, DisabledDropsShadowAndGlowAndHalvesTheAlphas) {
        UIBoxStyle box = StyledBox();
        box.glow = {0.0f, 0.0f, 5.0f, 0.0f, Color{9, 8, 7, 200}};
        const auto look = Resolve(box, kDisabled);
        EXPECT_EQ(look.box.shadow.color.a, 0);
        EXPECT_EQ(look.box.glow.color.a, 0);
        ExpectColor(look.fill, WithAlpha(kFill, kDisabledAlpha), "fill");
        ExpectColor(look.gradientEnd, WithAlpha(kEnd, kDisabledAlpha), "gradient end");
        ExpectColor(look.box.borderColor, WithAlpha(kBorder, kDisabledAlpha), "border");
        ExpectColor(look.box.highlight, WithAlpha(kHighlight, kDisabledAlpha), "highlight");
    }

    TEST(UIStateLookTest, DisabledAndSelectedKeepsTheRaisedBorderFadedAndNoSelectedFill) {
        const auto look = Resolve(StyledBox(), {true, true, true, true});
        ExpectColor(look.fill, WithAlpha(kFill, kDisabledAlpha), "fill: neither the press nor the selection shows");
        ExpectColor(look.box.borderColor, WithAlpha(kAccent, kDisabledAlpha), "border");
        EXPECT_EQ(look.box.glow.color.a, 0);
    }

    TEST(UIStateLookTest, AGradientKeepsTwoDistinctEndsInEveryState) {
        for (const auto& state : {kNormal, kHover, kPressed, kSelected, kDisabled}) {
            const auto look = Resolve(StyledBox(), state);
            EXPECT_FALSE(ColorsEqual(look.fill, look.gradientEnd));
            EXPECT_TRUE(ColorsEqual(look.gradientEnd, look.box.gradientColor)) << "the look says one end";
        }
    }

    TEST(UIStateLookTest, AnExplicitStateColorReplacesTheStartAndTheEndKeepsItsOffset) {
        // The end is (-40, -30, +30, 0) from the start; the end follows the new start by the same offset
        const Color explicitColor{150, 160, 170, 255};
        const Color moved{110, 130, 200, 255};
        const struct {
            const char* name;
            UIStateLook::State state;
            UIStateLook::Colors colors;
        } cases[] = {{"hover", kHover, {explicitColor, {}, {}, {}}},
                     {"pressed", kPressed, {{}, explicitColor, {}, {}}},
                     {"disabled", kDisabled, {{}, {}, explicitColor, {}}},
                     {"selected", kSelected, {{}, {}, {}, explicitColor}}};
        for (const auto& item : cases) {
            const auto look = Resolve(StyledBox(), item.state, item.colors);
            ExpectColor(look.fill, explicitColor, item.name);
            ExpectColor(look.gradientEnd, moved, item.name);
        }
    }

    TEST(UIStateLookTest, TheEndOffsetIsClamped) {
        const auto look = Resolve(StyledBox(), kHover, {Color{250, 250, 250, 255}, {}, {}, {}});
        ExpectColor(look.gradientEnd, Color{210, 220, 255, 255}, "end");
    }

    TEST(UIStateLookTest, AnExplicitColorOfAnotherStateIsNotUsed) {
        const Color explicitColor{150, 160, 170, 255};
        const auto look = Resolve(StyledBox(), kHover, {{}, explicitColor, explicitColor, explicitColor});
        ExpectColor(look.fill, ShiftColor(kFill, kHoverBrighten), "fill");
    }

    TEST(UIStateLookTest, ABoxWithNoEffectShiftsOnlyTheFillAndLeavesTheBoxAlone) {
        const UIBoxStyle plain;
        ASSERT_FALSE(plain.HasEffect());
        for (const auto& state : {kNormal, kHover, kPressed, kSelected, kDisabled}) {
            const auto look = Resolve(plain, state);
            EXPECT_TRUE(look.box == plain);
        }
        ExpectColor(Resolve(plain, kHover).fill, ShiftColor(kFill, kHoverBrighten), "hover");
        ExpectColor(Resolve(plain, kPressed).fill, ShiftColor(kFill, -kPressDarken), "pressed");
        ExpectColor(Resolve(plain, kSelected).fill, ShiftColor(kFill, UIStateLook::kSelectedBrighten), "selected");
        ExpectColor(Resolve(plain, kDisabled).fill, WithAlpha(kFill, kDisabledAlpha), "disabled");
    }

    // ---- ResolveTint ------------------------------------------------------------------------------------------------------

    TEST(UIStateLookTest, ATintIsDerivedFromTheBaseInEachState) {
        ExpectColor(UIStateLook::ResolveTint(kFill, {}, kNormal), kFill, "normal");
        ExpectColor(UIStateLook::ResolveTint(kFill, {}, kHover), ShiftColor(kFill, kHoverBrighten), "hover");
        ExpectColor(UIStateLook::ResolveTint(kFill, {}, kPressed), ShiftColor(kFill, -kPressDarken), "pressed");
        ExpectColor(UIStateLook::ResolveTint(kFill, {}, kDisabled), WithAlpha(kFill, kDisabledAlpha), "disabled");
        ExpectColor(UIStateLook::ResolveTint(kFill, {}, kSelected), kFill, "selected leaves a picture alone");
    }

    TEST(UIStateLookTest, AnExplicitTintReplacesTheDerivedOneOfItsStateOnly) {
        const UIStateLook::Tints tints{Color{1, 2, 3, 255}, Color{4, 5, 6, 255}, Color{7, 8, 9, 255}};
        ExpectColor(UIStateLook::ResolveTint(kFill, tints, kHover), tints.hover, "hover");
        ExpectColor(UIStateLook::ResolveTint(kFill, tints, kPressed), tints.pressed, "pressed");
        ExpectColor(UIStateLook::ResolveTint(kFill, tints, kDisabled), tints.disabled, "disabled");
        ExpectColor(UIStateLook::ResolveTint(kFill, tints, kNormal), kFill, "normal");
        ExpectColor(UIStateLook::ResolveTint(kFill, {Color{1, 2, 3, 255}, {}, {}}, kPressed), ShiftColor(kFill, -kPressDarken), "a state with no tint is derived");
    }

    TEST(UIStateLookTest, DisabledWinsOverPressedOverHoveredForATint) {
        const UIStateLook::Tints tints{Color{1, 2, 3, 255}, Color{4, 5, 6, 255}, Color{7, 8, 9, 255}};
        ExpectColor(UIStateLook::ResolveTint(kFill, tints, {true, true, true, false}), tints.disabled, "disabled");
        ExpectColor(UIStateLook::ResolveTint(kFill, tints, {true, true, false, false}), tints.pressed, "pressed");
    }

    // ---- the selected bar -------------------------------------------------------------------------------------------------

    TEST(UIStateLookTest, TheBarSpansTheBottomEdgeOfASquareBox) {
        const Rect bar = UIStateLook::SelectedBar(kRect, UIBoxStyle{});
        EXPECT_FLOAT_EQ(bar.x, kRect.x);
        EXPECT_FLOAT_EQ(bar.w, kRect.w);
        EXPECT_FLOAT_EQ(bar.h, UIStateLook::kSelectedBarHeight);
        EXPECT_FLOAT_EQ(bar.y + bar.h, kRect.y + kRect.h);
    }

    TEST(UIStateLookTest, TheBarIsInsetByTheBottomRadiiAndGoneWhenNoStraightPartIsLeft) {
        UIBoxStyle box;
        box.cornerRadius = UICorners::Uniform(8.0f);
        const Rect bar = UIStateLook::SelectedBar(kRect, box);
        EXPECT_FLOAT_EQ(bar.x, kRect.x + 8.0f);
        EXPECT_FLOAT_EQ(bar.w, kRect.w - 16.0f);

        box.cornerRadius = UICorners::Uniform(400.0f);  // clamped to the shorter half: a pill 100 wide and 40 high keeps a straight part of 20
        EXPECT_FLOAT_EQ(UIStateLook::SelectedBar(kRect, box).w, kRect.w - kRect.h);
        EXPECT_TRUE(UIStateLook::SelectedBar({0.0f, 0.0f, 40.0f, 40.0f}, box).IsEmpty()) << "a circle has no straight bottom edge";
        EXPECT_TRUE(UIStateLook::SelectedBar({}, UIBoxStyle{}).IsEmpty());
    }

    TEST(UIStateLookTest, TheBarIsNoHigherThanTheBox) {
        EXPECT_FLOAT_EQ(UIStateLook::SelectedBar({0.0f, 0.0f, 50.0f, 2.0f}, UIBoxStyle{}).h, 2.0f);
    }

    // ---- what an element makes of its flags -------------------------------------------------------------------------------

    TEST(UIStateLookTest, APressThatMovedOffTheElementIsNotShownAsPressed) {
        UIElement element;
        element.pressed = true;
        EXPECT_FALSE(element.GetVisualState().pressed);
        element.hovered = true;
        EXPECT_TRUE(element.GetVisualState().pressed);
        EXPECT_TRUE(element.GetVisualState().hovered);
    }

    TEST(UIStateLookTest, TheGroupDisablesAnElementAndSelectedIsCarried) {
        UIElement element;
        EXPECT_FALSE(element.IsDisabled());
        element.groupDisabled = true;
        element.selected = true;
        const auto state = element.GetVisualState();
        EXPECT_TRUE(state.disabled);
        EXPECT_TRUE(state.selected);
    }

    TEST(UIStateLookTest, AnElementWithNoComponentIsDrawnWithItsOwnFillWhateverItsState) {
        UIElement element;
        element.color = kFill;
        element.hovered = true;
        element.selected = true;
        EXPECT_FALSE(element.GivesStateLook());
        EXPECT_TRUE(element.GetTooltipText().empty());
    }

}  // namespace BECore::Tests
