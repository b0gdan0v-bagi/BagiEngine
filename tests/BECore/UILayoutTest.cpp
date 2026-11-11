#include <BECore/UI/UILayout.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        void ExpectRect(const Rect& rect, float x, float y, float w, float h) {
            EXPECT_NEAR(rect.x, x, kTolerance);
            EXPECT_NEAR(rect.y, y, kTolerance);
            EXPECT_NEAR(rect.w, w, kTolerance);
            EXPECT_NEAR(rect.h, h, kTolerance);
        }

        UIRectSpec Sized(float width, float height) {
            UIRectSpec spec;
            spec.width = UILength::Px(width);
            spec.height = UILength::Px(height);
            return spec;
        }

        const Rect kParent{100.0f, 50.0f, 400.0f, 200.0f};

        static_assert(UILength::Percent(10.0f).Resolve(200.0f) == 20.0f);
        static_assert(UILayout::AnchorFraction(UIAnchor::BottomRight).first == 1.0f);
        static_assert(UILayout::AnchorFraction(UIAnchor::Top).second == 0.0f);

    }  // namespace

    TEST(UILengthTest, PixelsAreTheValueAndAPercentIsOfTheParent) {
        EXPECT_FLOAT_EQ(UILength::Px(30.0f).Resolve(400.0f), 30.0f);
        EXPECT_FLOAT_EQ(UILength::Percent(25.0f).Resolve(400.0f), 100.0f);
        EXPECT_FLOAT_EQ(UILength::Percent(100.0f).Resolve(0.0f), 0.0f);
        EXPECT_EQ(UILength::Percent(50.0f), UILength::Percent(50.0f));
        EXPECT_NE(UILength::Px(50.0f), UILength::Percent(50.0f));
    }

    // ---- anchors ----------------------------------------------------------------------------------------------------------

    TEST(UILayoutTest, EveryAnchorPinsTheSamePointOfTheElementToThePointOfTheParent) {
        struct Case {
            UIAnchor anchor;
            float x;
            float y;
        };
        // A 40 x 20 element in the 400 x 200 parent at (100, 50)
        const Case cases[] = {
            {UIAnchor::TopLeft, 100.0f, 50.0f},     {UIAnchor::Top, 280.0f, 50.0f},     {UIAnchor::TopRight, 460.0f, 50.0f},
            {UIAnchor::Left, 100.0f, 140.0f},       {UIAnchor::Center, 280.0f, 140.0f}, {UIAnchor::Right, 460.0f, 140.0f},
            {UIAnchor::BottomLeft, 100.0f, 230.0f}, {UIAnchor::Bottom, 280.0f, 230.0f}, {UIAnchor::BottomRight, 460.0f, 230.0f},
        };
        for (const Case& c : cases) {
            UIRectSpec spec = Sized(40.0f, 20.0f);
            spec.anchor = c.anchor;
            SCOPED_TRACE(EnumUtils<UIAnchor>::ToString(c.anchor).data());
            ExpectRect(UILayout::ResolveRect(kParent, spec), c.x, c.y, 40.0f, 20.0f);
        }
    }

    TEST(UILayoutTest, TheOffsetMovesTheElementFromItsAnchor) {
        UIRectSpec spec = Sized(96.0f, 32.0f);
        spec.anchor = UIAnchor::BottomRight;
        spec.x = UILength::Px(-12.0f);
        spec.y = UILength::Px(-10.0f);

        // The bottom right corner of the element is 12 left of and 10 above the bottom right corner of the parent
        ExpectRect(UILayout::ResolveRect(kParent, spec), 500.0f - 12.0f - 96.0f, 250.0f - 10.0f - 32.0f, 96.0f, 32.0f);
    }

    TEST(UILayoutTest, APercentOffsetIsOfTheParentOnItsOwnAxis) {
        UIRectSpec spec = Sized(40.0f, 20.0f);
        spec.anchor = UIAnchor::BottomRight;
        spec.x = UILength::Percent(-5.0f);
        spec.y = UILength::Percent(-10.0f);

        // 5 percent of the width 400 is 20 and 10 percent of the height 200 is 20
        ExpectRect(UILayout::ResolveRect(kParent, spec), 500.0f - 20.0f - 40.0f, 250.0f - 20.0f - 20.0f, 40.0f, 20.0f);
        ExpectRect(UILayout::ResolveRect({0.0f, 0.0f, 1000.0f, 100.0f}, spec), 1000.0f - 50.0f - 40.0f, 100.0f - 10.0f - 20.0f, 40.0f, 20.0f);
    }

    TEST(UILayoutTest, PercentSizesFollowTheParentOnTheirOwnAxis) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Top;
        spec.y = UILength::Px(16.0f);
        spec.width = UILength::Percent(90.0f);
        spec.height = UILength::Percent(10.0f);

        ExpectRect(UILayout::ResolveRect(kParent, spec), 120.0f, 66.0f, 360.0f, 20.0f);
    }

    TEST(UILayoutTest, ThePlacementIsRelativeSoAParentOfAnotherSizeGivesAnotherRect) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Center;
        spec.width = UILength::Percent(50.0f);
        spec.height = UILength::Percent(50.0f);

        ExpectRect(UILayout::ResolveRect({0.0f, 0.0f, 1000.0f, 400.0f}, spec), 250.0f, 100.0f, 500.0f, 200.0f);
        ExpectRect(UILayout::ResolveRect({0.0f, 0.0f, 300.0f, 600.0f}, spec), 75.0f, 150.0f, 150.0f, 300.0f);
    }

    // ---- stretch ----------------------------------------------------------------------------------------------------------

    TEST(UILayoutTest, StretchFillsTheParentMinusTheMargins) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Stretch;
        spec.margin = {10.0f, 20.0f, 30.0f, 40.0f};

        ExpectRect(UILayout::ResolveRect(kParent, spec), 110.0f, 70.0f, 360.0f, 140.0f);
    }

    TEST(UILayoutTest, StretchIgnoresSizeAndOffset) {
        UIRectSpec spec = Sized(5.0f, 5.0f);
        spec.anchor = UIAnchor::Stretch;
        spec.x = UILength::Px(99.0f);
        spec.y = UILength::Px(99.0f);

        ExpectRect(UILayout::ResolveRect(kParent, spec), 100.0f, 50.0f, 400.0f, 200.0f);
    }

    TEST(UILayoutTest, MarginsLargerThanTheParentLeaveAnEmptyRectNotANegativeOne) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Stretch;
        spec.margin = {300.0f, 0.0f, 300.0f, 0.0f};

        const Rect rect = UILayout::ResolveRect(kParent, spec);

        EXPECT_FLOAT_EQ(rect.w, 0.0f);
        EXPECT_FLOAT_EQ(rect.h, 200.0f);
    }

    TEST(UILayoutTest, AStretchedElementThatALimitMakesSmallerIsCenteredInItsArea) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Stretch;
        spec.maxWidth = UILength::Px(100.0f);

        ExpectRect(UILayout::ResolveRect(kParent, spec), 250.0f, 50.0f, 100.0f, 200.0f);
    }

    // ---- limits -----------------------------------------------------------------------------------------------------------

    TEST(UILayoutTest, TheMaximumLimitsASizeAndAPercentLimitFollowsTheParent) {
        UIRectSpec spec = Sized(0.0f, 50.0f);
        spec.width = UILength::Percent(100.0f);
        spec.maxWidth = UILength::Px(300.0f);
        EXPECT_FLOAT_EQ(UILayout::ResolveRect(kParent, spec).w, 300.0f);

        spec.maxWidth = UILength::Percent(25.0f);
        EXPECT_FLOAT_EQ(UILayout::ResolveRect(kParent, spec).w, 100.0f);
        EXPECT_FLOAT_EQ(UILayout::ResolveRect({0.0f, 0.0f, 80.0f, 80.0f}, spec).w, 20.0f);
    }

    TEST(UILayoutTest, TheMinimumRaisesASize) {
        UIRectSpec spec = Sized(10.0f, 10.0f);
        spec.minWidth = UILength::Px(48.0f);
        spec.minHeight = UILength::Percent(30.0f);

        const Rect rect = UILayout::ResolveRect(kParent, spec);

        EXPECT_FLOAT_EQ(rect.w, 48.0f);
        EXPECT_FLOAT_EQ(rect.h, 60.0f);
    }

    TEST(UILayoutTest, WhenTheLimitsDisagreeTheMinimumWins) {
        UIRectSpec spec = Sized(100.0f, 20.0f);
        spec.minWidth = UILength::Px(80.0f);
        spec.maxWidth = UILength::Px(40.0f);

        EXPECT_FLOAT_EQ(UILayout::ResolveRect(kParent, spec).w, 80.0f);
    }

    TEST(UILayoutTest, ANegativeSizeIsZero) {
        EXPECT_FLOAT_EQ(UILayout::ResolveRect(kParent, Sized(-5.0f, 20.0f)).w, 0.0f);
    }

    // ---- aspect ratio -----------------------------------------------------------------------------------------------------

    TEST(UILayoutTest, FitMakesTheLargestRectOfTheRatioInsideTheBox) {
        UIRectSpec spec = Sized(200.0f, 100.0f);
        spec.aspectRatio = 1.0f;
        spec.aspectMode = UIAspectMode::Fit;
        ExpectRect(UILayout::ResolveRect({0.0f, 0.0f, 400.0f, 400.0f}, spec), 0.0f, 0.0f, 100.0f, 100.0f);

        UIRectSpec tall = Sized(100.0f, 200.0f);
        tall.aspectRatio = 2.0f;
        ExpectRect(UILayout::ResolveRect({0.0f, 0.0f, 400.0f, 400.0f}, tall), 0.0f, 0.0f, 100.0f, 50.0f);
    }

    TEST(UILayoutTest, FillMakesTheSmallestRectOfTheRatioThatCoversTheBox) {
        UIRectSpec spec = Sized(200.0f, 100.0f);
        spec.aspectRatio = 1.0f;
        spec.aspectMode = UIAspectMode::Fill;
        ExpectRect(UILayout::ResolveRect({0.0f, 0.0f, 400.0f, 400.0f}, spec), 0.0f, 0.0f, 200.0f, 200.0f);

        UIRectSpec tall = Sized(100.0f, 200.0f);
        tall.aspectRatio = 2.0f;
        tall.aspectMode = UIAspectMode::Fill;
        ExpectRect(UILayout::ResolveRect({0.0f, 0.0f, 400.0f, 400.0f}, tall), 0.0f, 0.0f, 400.0f, 200.0f);
    }

    TEST(UILayoutTest, TheAspectRatioKeepsTheRatioOfAStretchedImageInAnyParent) {
        UIRectSpec spec;
        spec.anchor = UIAnchor::Stretch;
        spec.aspectRatio = 1.0f;  // a square sprite in whatever box is left
        for (const Rect& parent : {Rect{0.0f, 0.0f, 640.0f, 360.0f}, Rect{0.0f, 0.0f, 360.0f, 640.0f}, Rect{0.0f, 0.0f, 100.0f, 100.0f}}) {
            const Rect rect = UILayout::ResolveRect(parent, spec);
            EXPECT_NEAR(rect.w, rect.h, kTolerance);
            EXPECT_NEAR(rect.w, std::min(parent.w, parent.h), kTolerance);
            EXPECT_NEAR(rect.x + rect.w * 0.5f, parent.x + parent.w * 0.5f, kTolerance);  // centered
            EXPECT_NEAR(rect.y + rect.h * 0.5f, parent.y + parent.h * 0.5f, kTolerance);
        }
    }

    TEST(UILayoutTest, AnAspectRatioOfAnEmptyBoxStaysEmpty) {
        UIRectSpec spec = Sized(0.0f, 100.0f);
        spec.aspectRatio = 2.0f;

        const Rect rect = UILayout::ResolveRect(kParent, spec);

        EXPECT_FLOAT_EQ(rect.w, 0.0f);
        EXPECT_FLOAT_EQ(rect.h, 100.0f);
    }

    // ---- stack ------------------------------------------------------------------------------------------------------------

    TEST(UIStackTest, AVerticalStackPutsTheChildrenOneBelowAnotherWithSpacing) {
        UIStackSpec stack;
        stack.spacing = 8.0f;
        const UIRectSpec children[] = {Sized(100.0f, 30.0f), Sized(60.0f, 40.0f), Sized(80.0f, 20.0f)};
        eastl::vector<Rect> rects;

        UILayout::LayoutStack(kParent, stack, children, rects);

        ASSERT_EQ(rects.size(), 3u);
        ExpectRect(rects[0], 100.0f, 50.0f, 100.0f, 30.0f);
        ExpectRect(rects[1], 100.0f, 88.0f, 60.0f, 40.0f);
        ExpectRect(rects[2], 100.0f, 136.0f, 80.0f, 20.0f);
    }

    TEST(UIStackTest, AHorizontalStackPutsThemSideBySide) {
        UIStackSpec stack;
        stack.direction = UIStackDirection::Horizontal;
        stack.spacing = 10.0f;
        const UIRectSpec children[] = {Sized(50.0f, 30.0f), Sized(70.0f, 40.0f)};
        eastl::vector<Rect> rects;

        UILayout::LayoutStack(kParent, stack, children, rects);

        ExpectRect(rects[0], 100.0f, 50.0f, 50.0f, 30.0f);
        ExpectRect(rects[1], 160.0f, 50.0f, 70.0f, 40.0f);
    }

    TEST(UIStackTest, PaddingInsetsTheContent) {
        UIStackSpec stack;
        stack.padding = {10.0f, 20.0f, 30.0f, 40.0f};
        const UIRectSpec children[] = {Sized(50.0f, 30.0f)};
        eastl::vector<Rect> rects;

        UILayout::LayoutStack(kParent, stack, children, rects);

        ExpectRect(rects[0], 110.0f, 70.0f, 50.0f, 30.0f);
    }

    TEST(UIStackTest, FlexChildrenShareWhatTheFixedOnesLeaveByWeight) {
        UIStackSpec stack;
        stack.direction = UIStackDirection::Horizontal;
        stack.spacing = 10.0f;
        UIRectSpec grow1 = Sized(0.0f, 20.0f);
        grow1.flex = 1.0f;
        UIRectSpec grow3 = Sized(0.0f, 20.0f);
        grow3.flex = 3.0f;
        const UIRectSpec children[] = {Sized(60.0f, 20.0f), grow1, grow3};
        eastl::vector<Rect> rects;

        UILayout::LayoutStack(kParent, stack, children, rects);

        // 400 wide: 60 fixed, two gaps of 10, so 320 is shared 1:3
        ExpectRect(rects[0], 100.0f, 50.0f, 60.0f, 20.0f);
        ExpectRect(rects[1], 170.0f, 50.0f, 80.0f, 20.0f);
        ExpectRect(rects[2], 260.0f, 50.0f, 240.0f, 20.0f);
    }

    TEST(UIStackTest, AFlexChildIsClampedByItsLimitsWithoutGivingTheRestToTheOthers) {
        UIStackSpec stack;
        stack.direction = UIStackDirection::Horizontal;
        UIRectSpec capped = Sized(0.0f, 20.0f);
        capped.flex = 1.0f;
        capped.maxWidth = UILength::Px(50.0f);
        UIRectSpec open = Sized(0.0f, 20.0f);
        open.flex = 1.0f;
        const UIRectSpec children[] = {capped, open};
        eastl::vector<Rect> rects;

        UILayout::LayoutStack(kParent, stack, children, rects);

        EXPECT_NEAR(rects[0].w, 50.0f, kTolerance);
        EXPECT_NEAR(rects[1].w, 200.0f, kTolerance);  // half of 400, not the 350 that are left
    }

    TEST(UIStackTest, FlexGetsNothingWhenTheFixedChildrenFillTheLine) {
        UIStackSpec stack;
        stack.direction = UIStackDirection::Horizontal;
        UIRectSpec grow = Sized(0.0f, 20.0f);
        grow.flex = 1.0f;
        const UIRectSpec children[] = {Sized(500.0f, 20.0f), grow};
        eastl::vector<Rect> rects;

        UILayout::LayoutStack(kParent, stack, children, rects);

        EXPECT_FLOAT_EQ(rects[0].w, 500.0f);  // the fixed child is not cut
        EXPECT_FLOAT_EQ(rects[1].w, 0.0f);
    }

    TEST(UIStackTest, PercentSizesOnTheMainAxisAreOfTheContent) {
        UIStackSpec stack;
        stack.padding = {0.0f, 0.0f, 0.0f, 100.0f};  // the content is 400 x 100
        UIRectSpec half;
        half.width = UILength::Px(10.0f);
        half.height = UILength::Percent(50.0f);
        const UIRectSpec children[] = {half};
        eastl::vector<Rect> rects;

        UILayout::LayoutStack(kParent, stack, children, rects);

        EXPECT_FLOAT_EQ(rects[0].h, 50.0f);
    }

    TEST(UIStackTest, CrossAlignmentPlacesTheChildrenInTheContentAcross) {
        const UIRectSpec children[] = {Sized(100.0f, 30.0f), Sized(40.0f, 30.0f)};
        eastl::vector<Rect> rects;
        UIStackSpec stack;

        stack.crossAlign = UIStackAlign::Start;
        UILayout::LayoutStack(kParent, stack, children, rects);
        EXPECT_FLOAT_EQ(rects[1].x, 100.0f);

        stack.crossAlign = UIStackAlign::Center;
        UILayout::LayoutStack(kParent, stack, children, rects);
        EXPECT_FLOAT_EQ(rects[0].x, 250.0f);  // (400 - 100) / 2
        EXPECT_FLOAT_EQ(rects[1].x, 280.0f);

        stack.crossAlign = UIStackAlign::End;
        UILayout::LayoutStack(kParent, stack, children, rects);
        EXPECT_FLOAT_EQ(rects[1].x, 460.0f);
    }

    TEST(UIStackTest, StretchMakesTheChildrenAsWideAsTheContentAndKeepsTheirLimits) {
        UIRectSpec limited = Sized(10.0f, 30.0f);
        limited.maxWidth = UILength::Px(120.0f);
        const UIRectSpec children[] = {Sized(10.0f, 30.0f), limited};
        UIStackSpec stack;
        stack.crossAlign = UIStackAlign::Stretch;
        eastl::vector<Rect> rects;

        UILayout::LayoutStack(kParent, stack, children, rects);

        EXPECT_FLOAT_EQ(rects[0].w, 400.0f);
        EXPECT_FLOAT_EQ(rects[1].w, 120.0f);
    }

    TEST(UIStackTest, WithoutWrapTheChildrenStayOnOneLineEvenWhenTheyOverflow) {
        UIStackSpec stack;
        stack.direction = UIStackDirection::Horizontal;
        const UIRectSpec children[] = {Sized(250.0f, 20.0f), Sized(250.0f, 20.0f)};
        eastl::vector<Rect> rects;

        UILayout::LayoutStack(kParent, stack, children, rects);

        EXPECT_FLOAT_EQ(rects[0].y, rects[1].y);
        EXPECT_FLOAT_EQ(rects[1].x, 350.0f);
    }

    TEST(UIStackTest, WrapStartsANewLineWhenTheNextChildDoesNotFit) {
        UIStackSpec stack;
        stack.direction = UIStackDirection::Horizontal;
        stack.wrap = true;
        stack.spacing = 10.0f;
        const UIRectSpec children[] = {Sized(150.0f, 20.0f), Sized(150.0f, 30.0f), Sized(150.0f, 25.0f), Sized(90.0f, 10.0f)};
        eastl::vector<Rect> rects;

        UILayout::LayoutStack(kParent, stack, children, rects);

        // 400 wide: 150 + 10 + 150 = 310 fits, a third 150 does not; the line is as tall as its tallest child
        ExpectRect(rects[0], 100.0f, 50.0f, 150.0f, 20.0f);
        ExpectRect(rects[1], 260.0f, 50.0f, 150.0f, 30.0f);
        ExpectRect(rects[2], 100.0f, 90.0f, 150.0f, 25.0f);
        ExpectRect(rects[3], 260.0f, 90.0f, 90.0f, 10.0f);  // 150 + 10 + 90 = 250 still fits the second line
    }

    TEST(UIStackTest, AChildWiderThanTheLineGetsALineOfItsOwnWhenWrapping) {
        UIStackSpec stack;
        stack.direction = UIStackDirection::Horizontal;
        stack.wrap = true;
        const UIRectSpec children[] = {Sized(600.0f, 20.0f), Sized(50.0f, 20.0f)};
        eastl::vector<Rect> rects;

        UILayout::LayoutStack(kParent, stack, children, rects);

        EXPECT_FLOAT_EQ(rects[0].y, 50.0f);
        EXPECT_FLOAT_EQ(rects[1].y, 70.0f);
        EXPECT_FLOAT_EQ(rects[1].x, 100.0f);
    }

    TEST(UIStackTest, WrappedLinesShareTheirFlexChildrenPerLine) {
        UIStackSpec stack;
        stack.direction = UIStackDirection::Horizontal;
        stack.wrap = true;
        UIRectSpec grow = Sized(100.0f, 20.0f);  // its own size only decides where the line breaks
        grow.flex = 1.0f;
        const UIRectSpec children[] = {Sized(300.0f, 20.0f), grow, grow};
        eastl::vector<Rect> rects;

        UILayout::LayoutStack(kParent, stack, children, rects);

        // Line 1: 300 + 100 = 400 fits; the flex child takes the 100 that is left. Line 2: the last child alone takes 400
        EXPECT_NEAR(rects[1].w, 100.0f, kTolerance);
        EXPECT_NEAR(rects[2].w, 400.0f, kTolerance);
        EXPECT_NEAR(rects[2].x, 100.0f, kTolerance);
    }

    TEST(UIStackTest, AnEmptyStackGivesNoRects) {
        eastl::vector<Rect> rects{Rect{1.0f, 2.0f, 3.0f, 4.0f}};

        UILayout::LayoutStack(kParent, UIStackSpec{}, {}, rects);

        EXPECT_TRUE(rects.empty());
    }

    TEST(UILayoutTest, CompileTimeChecks) {}

}  // namespace BECore::Tests
