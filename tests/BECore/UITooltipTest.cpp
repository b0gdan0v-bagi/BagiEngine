#include <BECore/UI/UILabel.h>
#include <BECore/UI/UITooltip.h>
#include <Common/EngineEnvironment.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kEpsilon = 1e-3f;
        constexpr Rect kBounds{0.0f, 0.0f, 800.0f, 600.0f};
        constexpr Vec2 kPlate{120.0f, 40.0f};

        bool Near(const Rect& a, const Rect& b) {
            return std::abs(a.x - b.x) <= kEpsilon && std::abs(a.y - b.y) <= kEpsilon && std::abs(a.w - b.w) <= kEpsilon && std::abs(a.h - b.h) <= kEpsilon;
        }

        bool Inside(const Rect& inner, const Rect& outer) {
            return inner.x >= outer.x - kEpsilon && inner.y >= outer.y - kEpsilon && inner.x + inner.w <= outer.x + outer.w + kEpsilon && inner.y + inner.h <= outer.y + outer.h + kEpsilon;
        }

        bool Disjoint(const Rect& a, const Rect& b) {
            return a.x + a.w <= b.x + kEpsilon || b.x + b.w <= a.x + kEpsilon || a.y + a.h <= b.y + kEpsilon || b.y + b.h <= a.y + kEpsilon;
        }

    }  // namespace

    // ---- Place ---------------------------------------------------------------------------------------------------------------

    TEST(UITooltipPlaceTest, WithRoomBelowThePlateIsCentredUnderTheButton) {
        const Rect target{300.0f, 100.0f, 48.0f, 48.0f};
        const Rect plate = UITooltip::Place(target, kPlate, kBounds);

        EXPECT_TRUE(Near(plate, {300.0f + 24.0f - 60.0f, 100.0f + 48.0f + UITooltip::kGap, kPlate.x, kPlate.y}));
    }

    TEST(UITooltipPlaceTest, WithNoRoomBelowAndRoomAboveItGoesAboveTheButton) {
        const Rect target{300.0f, 540.0f, 48.0f, 48.0f};
        const Rect plate = UITooltip::Place(target, kPlate, kBounds);

        EXPECT_NEAR(plate.y, 540.0f - UITooltip::kGap - kPlate.y, kEpsilon);
        EXPECT_NEAR(plate.x, 300.0f + 24.0f - 60.0f, kEpsilon);
        EXPECT_TRUE(Disjoint(plate, target));
    }

    TEST(UITooltipPlaceTest, WithNoRoomOnEitherSideItIsMovedIntoTheBounds) {
        const Rect shortBounds{0.0f, 0.0f, 800.0f, 100.0f};
        const Rect target{300.0f, 30.0f, 48.0f, 48.0f};  // 30 above, 22 below: a 40 high plate fits neither side
        const Rect plate = UITooltip::Place(target, kPlate, shortBounds);

        EXPECT_TRUE(Inside(plate, shortBounds));
        EXPECT_NEAR(plate.w, kPlate.x, kEpsilon);
        EXPECT_NEAR(plate.h, kPlate.y, kEpsilon);
    }

    TEST(UITooltipPlaceTest, AButtonAtTheLeftEdgeKeepsThePlateInside) {
        const Rect plate = UITooltip::Place({0.0f, 100.0f, 48.0f, 48.0f}, kPlate, kBounds);

        EXPECT_NEAR(plate.x, kBounds.x, kEpsilon);
        EXPECT_TRUE(Inside(plate, kBounds));
    }

    TEST(UITooltipPlaceTest, AButtonAtTheRightEdgeKeepsThePlateInside) {
        const Rect plate = UITooltip::Place({752.0f, 100.0f, 48.0f, 48.0f}, kPlate, kBounds);

        EXPECT_NEAR(plate.x + plate.w, kBounds.x + kBounds.w, kEpsilon);
        EXPECT_TRUE(Inside(plate, kBounds));
    }

    TEST(UITooltipPlaceTest, TheSafeAreaIsTheLimitNotTheCanvas) {
        const Rect safe{20.0f, 40.0f, 760.0f, 520.0f};
        const Rect plate = UITooltip::Place({20.0f, 40.0f, 48.0f, 48.0f}, kPlate, safe);

        EXPECT_NEAR(plate.x, 20.0f, kEpsilon);
        EXPECT_TRUE(Inside(plate, safe));
    }

    TEST(UITooltipPlaceTest, APlateLargerThanTheBoundsStartsAtTheirTopLeftEdge) {
        const Rect small{50.0f, 60.0f, 100.0f, 30.0f};
        const Rect plate = UITooltip::Place({70.0f, 65.0f, 20.0f, 20.0f}, kPlate, small);

        EXPECT_NEAR(plate.x, small.x, kEpsilon);
        EXPECT_NEAR(plate.y, small.y, kEpsilon);
        EXPECT_NEAR(plate.w, kPlate.x, kEpsilon) << "the size is kept";
    }

    // ---- the show rule -------------------------------------------------------------------------------------------------------

    class UITooltipTrackerTest : public ::testing::Test {
    protected:
        UIElement _a;
        UIElement _b;
        UITooltip::Tracker _tracker;
    };

    TEST_F(UITooltipTrackerTest, AMouseShowsItAfterTheDelayAndNotBefore) {
        _tracker.Update(&_a, false, false, 10.0);
        EXPECT_FALSE(_tracker.IsShown(&_a));
        _tracker.Update(&_a, false, false, 10.0 + UITooltip::kHoverDelay - 0.01);
        EXPECT_FALSE(_tracker.IsShown(&_a));
        _tracker.Update(&_a, false, false, 10.0 + UITooltip::kHoverDelay);
        EXPECT_TRUE(_tracker.IsShown(&_a));
        EXPECT_FALSE(_tracker.IsShown(&_b)) << "only the target's tooltip";
        EXPECT_FALSE(_tracker.IsLongPress());
    }

    TEST_F(UITooltipTrackerTest, AnotherTargetRestartsTheDelay) {
        _tracker.Update(&_a, false, false, 0.0);
        _tracker.Update(&_a, false, false, 0.6);
        ASSERT_TRUE(_tracker.IsShown(&_a));

        _tracker.Update(&_b, false, false, 0.7);
        EXPECT_FALSE(_tracker.IsShown(&_a));
        EXPECT_FALSE(_tracker.IsShown(&_b));
        _tracker.Update(&_b, false, false, 0.7 + UITooltip::kHoverDelay);
        EXPECT_TRUE(_tracker.IsShown(&_b));
    }

    TEST_F(UITooltipTrackerTest, LeavingTheTargetHidesItAndComingBackStartsAgain) {
        _tracker.Update(&_a, false, false, 0.0);
        _tracker.Update(&_a, false, false, 1.0);
        ASSERT_TRUE(_tracker.IsShown(&_a));

        _tracker.Update(nullptr, false, false, 1.1);
        EXPECT_FALSE(_tracker.IsShown(&_a));
        _tracker.Update(&_a, false, false, 1.2);
        EXPECT_FALSE(_tracker.IsShown(&_a)) << "back on it: the delay runs again";
        _tracker.Update(&_a, false, false, 1.2 + UITooltip::kHoverDelay);
        EXPECT_TRUE(_tracker.IsShown(&_a));
    }

    TEST_F(UITooltipTrackerTest, APressHidesItAndKeepsItHiddenUntilThePointerHasLeft) {
        _tracker.Update(&_a, false, false, 0.0);
        _tracker.Update(&_a, false, false, 1.0);
        ASSERT_TRUE(_tracker.IsShown(&_a));

        _tracker.Update(&_a, false, true, 1.1);
        EXPECT_FALSE(_tracker.IsShown(&_a));
        _tracker.Update(&_a, false, false, 1.2);
        _tracker.Update(&_a, false, false, 5.0);
        EXPECT_FALSE(_tracker.IsShown(&_a)) << "released on the button: still hidden, however long the wait";

        _tracker.Update(nullptr, false, false, 5.1);
        _tracker.Update(&_a, false, false, 5.2);
        _tracker.Update(&_a, false, false, 5.2 + UITooltip::kHoverDelay);
        EXPECT_TRUE(_tracker.IsShown(&_a));
    }

    TEST_F(UITooltipTrackerTest, APressBeforeAnythingWasShownKeepsItFromShowing) {
        _tracker.Update(&_a, false, false, 0.0);
        _tracker.Update(&_a, false, true, 0.1);
        _tracker.Update(&_a, false, true, 3.0);
        EXPECT_FALSE(_tracker.IsShown(&_a));
    }

    TEST_F(UITooltipTrackerTest, ResetForgetsEverything) {
        _tracker.Update(&_a, false, false, 0.0);
        _tracker.Update(&_a, false, false, 1.0);
        ASSERT_TRUE(_tracker.IsShown(&_a));

        _tracker.Reset();
        EXPECT_FALSE(_tracker.IsShown(&_a));
        EXPECT_FALSE(_tracker.IsLongPress());
    }

    TEST_F(UITooltipTrackerTest, ATouchHeldPastTheLongPressShowsItAndIsFlagged) {
        _tracker.Update(&_a, true, true, 0.0);
        EXPECT_FALSE(_tracker.IsShown(&_a));
        _tracker.Update(&_a, true, true, UITooltip::kLongPress - 0.01);
        EXPECT_FALSE(_tracker.IsShown(&_a));
        EXPECT_FALSE(_tracker.IsLongPress());

        _tracker.Update(&_a, true, true, UITooltip::kLongPress);
        EXPECT_TRUE(_tracker.IsShown(&_a));
        EXPECT_TRUE(_tracker.IsLongPress());
    }

    TEST_F(UITooltipTrackerTest, ATouchTooltipLingersAfterTheReleaseAndThenGoes) {
        _tracker.Update(&_a, true, true, 0.0);
        _tracker.Update(&_a, true, true, 1.0);
        _tracker.Update(&_a, true, false, 1.2);
        EXPECT_TRUE(_tracker.IsLongPress()) << "the release still knows it was a long press";
        EXPECT_TRUE(_tracker.IsShown(&_a));

        _tracker.Update(&_a, true, false, 1.2 + UITooltip::kTouchLinger - 0.01);
        EXPECT_TRUE(_tracker.IsShown(&_a));
        _tracker.Update(&_a, true, false, 1.2 + UITooltip::kTouchLinger);
        EXPECT_FALSE(_tracker.IsShown(&_a));
    }

    TEST_F(UITooltipTrackerTest, AReleaseAfterALongWaitWithNoUpdateInBetweenIsStillALongPress) {
        _tracker.Update(&_a, true, true, 0.0);
        _tracker.Update(&_a, true, false, 2.0);

        EXPECT_TRUE(_tracker.IsLongPress());
        EXPECT_TRUE(_tracker.IsShown(&_a));
    }

    TEST_F(UITooltipTrackerTest, AShortTouchIsNeverFlaggedAndNeverShowsAnything) {
        _tracker.Update(&_a, true, true, 0.0);
        _tracker.Update(&_a, true, false, UITooltip::kLongPress - 0.1);
        EXPECT_FALSE(_tracker.IsLongPress());
        EXPECT_FALSE(_tracker.IsShown(&_a));

        // The finger is up and the pointer still rests on the button: a mouse tooltip would come now, a touch one does not
        _tracker.Update(&_a, true, false, 5.0);
        EXPECT_FALSE(_tracker.IsShown(&_a));
        EXPECT_FALSE(_tracker.IsLongPress());
    }

    TEST_F(UITooltipTrackerTest, ATouchThatLeavesTheButtonIsNoLongPress) {
        _tracker.Update(&_a, true, true, 0.0);
        _tracker.Update(&_b, true, false, 0.4);
        _tracker.Update(&_b, true, false, 2.0);

        EXPECT_FALSE(_tracker.IsLongPress());
        EXPECT_FALSE(_tracker.IsShown(&_a));
        EXPECT_FALSE(_tracker.IsShown(&_b));
    }

    TEST_F(UITooltipTrackerTest, ANewTouchPressClearsTheFlagOfTheLastOne) {
        _tracker.Update(&_a, true, true, 0.0);
        _tracker.Update(&_a, true, false, 1.0);
        ASSERT_TRUE(_tracker.IsLongPress());

        _tracker.Update(&_a, true, true, 3.0);
        EXPECT_FALSE(_tracker.IsLongPress());
        EXPECT_FALSE(_tracker.IsShown(&_a));
        _tracker.Update(&_a, true, false, 3.1);
        EXPECT_FALSE(_tracker.IsLongPress()) << "a short touch after a long one";
    }

    TEST_F(UITooltipTrackerTest, AMousePressIsNeverALongPress) {
        _tracker.Update(&_a, false, true, 0.0);
        _tracker.Update(&_a, false, false, 5.0);
        EXPECT_FALSE(_tracker.IsLongPress());
    }

    TEST_F(UITooltipTrackerTest, SwitchingFromTouchToMouseStartsAgain) {
        _tracker.Update(&_a, true, true, 0.0);
        _tracker.Update(&_a, true, true, 1.0);
        ASSERT_TRUE(_tracker.IsShown(&_a));

        _tracker.Update(&_a, false, false, 1.1);
        EXPECT_FALSE(_tracker.IsShown(&_a));
        EXPECT_FALSE(_tracker.IsLongPress());
        _tracker.Update(&_a, false, false, 1.1 + UITooltip::kHoverDelay);
        EXPECT_TRUE(_tracker.IsShown(&_a));
    }

    // ---- MeasureText ---------------------------------------------------------------------------------------------------------

    class UILabelMeasureTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        static IntrusivePtr<UILabel> MakeLabel(const char* text) {
            auto label = New<UILabel>();
            label->text = text;
            label->active.fontSize = UITooltip::kFontSize;
            return label;
        }
    };

    TEST_F(UILabelMeasureTest, AShortTextIsOneLineNarrowerThanTheLimit) {
        const auto label = MakeLabel("Settings");
        const Vec2 size = label->MeasureText(UITooltip::kMaxWidth, 1.0f);

        EXPECT_GT(size.x, 0.0f);
        EXPECT_LT(size.x, UITooltip::kMaxWidth);
        EXPECT_GE(size.y, UITooltip::kFontSize) << "a line is at least as high as the font is";
        EXPECT_LT(size.y, UITooltip::kFontSize * 2.0f) << "one line";
    }

    TEST_F(UILabelMeasureTest, ALongTextWrapsAtTheLimitAndGrowsTaller) {
        const auto shortLabel = MakeLabel("Settings");
        const auto longLabel = MakeLabel("Settings of the game: the sound, the language and the size of everything on the screen");
        const Vec2 shortSize = shortLabel->MeasureText(UITooltip::kMaxWidth, 1.0f);
        const Vec2 longSize = longLabel->MeasureText(UITooltip::kMaxWidth, 1.0f);

        EXPECT_LE(longSize.x, UITooltip::kMaxWidth + kEpsilon);
        EXPECT_GT(longSize.x, shortSize.x);
        EXPECT_GT(longSize.y, shortSize.y * 1.5f) << "several lines";
    }

    TEST_F(UILabelMeasureTest, ANarrowerLimitMakesItTaller) {
        const auto label = MakeLabel("Settings of the game and the sound");
        const Vec2 wide = label->MeasureText(400.0f, 1.0f);
        const Vec2 narrow = label->MeasureText(100.0f, 1.0f);

        EXPECT_LE(narrow.x, 100.0f + kEpsilon);
        EXPECT_GT(narrow.y, wide.y);
    }

    TEST_F(UILabelMeasureTest, TheSizeIsInCanvasUnitsWhateverTheScale) {
        const auto label = MakeLabel("Settings");
        const Vec2 one = label->MeasureText(UITooltip::kMaxWidth, 1.0f);
        const Vec2 two = label->MeasureText(UITooltip::kMaxWidth, 2.0f);

        EXPECT_NEAR(two.x, one.x, one.x * 0.1f) << "twice the pixels, twice the divisor";
        EXPECT_NEAR(two.y, one.y, one.y * 0.1f);
    }

    TEST_F(UILabelMeasureTest, AnEmptyTextOrNoRoomIsZero) {
        const auto empty = MakeLabel("");
        EXPECT_EQ(empty->MeasureText(UITooltip::kMaxWidth, 1.0f), Vec2{});

        const auto label = MakeLabel("Settings");
        EXPECT_EQ(label->MeasureText(0.0f, 1.0f), Vec2{});
        EXPECT_EQ(label->MeasureText(UITooltip::kMaxWidth, 0.0f), Vec2{});
    }

}  // namespace BECore::Tests
