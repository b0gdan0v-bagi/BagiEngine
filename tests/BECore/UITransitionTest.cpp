#include <BECore/UI/UIGeometryBounds.h>
#include <BECore/UI/UITransition.h>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

        // Times are powers of two so the sums are exact; QuadOut gives 1 - (1 - t)^2: 0.4375 at 1/4, 0.75 at 1/2, 0.9375 at 3/4
        UITransitionSpec Spec(float duration, float hideDuration = -1.0f) {
            UITransitionSpec spec;
            spec.duration = duration;
            spec.hideDuration = hideDuration;
            return spec;
        }

        Vec2 Apply(const ViewTransform& view, Vec2 point) {
            return {point.x * view.scale + view.offsetX, point.y * view.scale + view.offsetY};
        }

    }  // namespace

    TEST(UITransitionSpecTest, DefaultIsInactiveAndFades) {
        const UITransitionSpec spec;
        EXPECT_FALSE(spec.IsActive());
        EXPECT_TRUE(spec.fade);
        EXPECT_EQ(spec.ease, EaseType::QuadOut);
        EXPECT_FALSE(spec.inputDuringTransition);
    }

    TEST(UITransitionSpecTest, ActiveWhenAnyDirectionHasATime) {
        EXPECT_TRUE(Spec(0.25f).IsActive());
        EXPECT_TRUE(Spec(0.0f, 0.25f).IsActive());
        EXPECT_FALSE(Spec(0.0f, 0.0f).IsActive());
        EXPECT_FALSE(Spec(-1.0f).IsActive());
    }

    TEST(UITransitionSpecTest, HideFollowsTheShowWhenNegativeOrNaN) {
        EXPECT_FLOAT_EQ(Spec(0.5f).HideSeconds(), 0.5f);
        EXPECT_FLOAT_EQ(Spec(0.5f, kNaN).HideSeconds(), 0.5f);
        EXPECT_FLOAT_EQ(Spec(0.5f, 0.25f).HideSeconds(), 0.25f);
        EXPECT_FLOAT_EQ(Spec(0.5f, 0.0f).HideSeconds(), 0.0f);
    }

    TEST(UITransitionSpecTest, NumbersOfAFileAreBounded) {
        UITransitionSpec spec;
        spec.duration = kNaN;
        spec.hideDuration = 1.0e9f;
        spec.scaleFrom = kNaN;
        spec.slideX = 1.0e30f;
        spec.slideY = kNaN;
        const UITransitionSpec bounded = spec.Bounded();
        EXPECT_EQ(bounded.duration, 0.0f);
        EXPECT_EQ(bounded.hideDuration, kMaxTransitionSeconds);
        EXPECT_EQ(bounded.scaleFrom, 1.0f);
        EXPECT_EQ(bounded.slideX, UIBounds::kMaxExtent);
        EXPECT_EQ(bounded.slideY, 0.0f);

        spec.duration = -3.0f;
        spec.scaleFrom = -2.0f;
        spec.hideDuration = kNaN;
        const UITransitionSpec second = spec.Bounded();
        EXPECT_EQ(second.duration, 0.0f);
        EXPECT_EQ(second.hideDuration, -1.0f);
        EXPECT_EQ(second.scaleFrom, 0.0f);
    }

    TEST(UITransitionEvaluateTest, FadeScaleAndSlideFollowTheProgress) {
        UITransitionSpec spec;
        spec.scaleFrom = 0.5f;
        spec.slideX = 8.0f;
        spec.slideY = -16.0f;

        const auto hidden = UITransition::Evaluate(spec, 0.0f);
        EXPECT_EQ(hidden.opacity, 0.0f);
        EXPECT_EQ(hidden.scale, 0.5f);
        EXPECT_EQ(hidden.offsetX, 8.0f);
        EXPECT_EQ(hidden.offsetY, -16.0f);

        const auto half = UITransition::Evaluate(spec, 0.5f);
        EXPECT_EQ(half.opacity, 0.5f);
        EXPECT_EQ(half.scale, 0.75f);
        EXPECT_EQ(half.offsetX, 4.0f);
        EXPECT_EQ(half.offsetY, -8.0f);

        const auto shown = UITransition::Evaluate(spec, 1.0f);
        EXPECT_EQ(shown.opacity, 1.0f);
        EXPECT_EQ(shown.scale, 1.0f);
        EXPECT_EQ(shown.offsetX, 0.0f);
        EXPECT_EQ(shown.offsetY, 0.0f);
    }

    TEST(UITransitionEvaluateTest, WithoutFadeTheOpacityStaysOne) {
        UITransitionSpec spec;
        spec.fade = false;
        EXPECT_EQ(UITransition::Evaluate(spec, 0.0f).opacity, 1.0f);
        EXPECT_EQ(UITransition::Evaluate(spec, 0.5f).opacity, 1.0f);
    }

    TEST(UITransitionEvaluateTest, DefaultSpecChangesNothingButTheOpacity) {
        const auto evaluated = UITransition::Evaluate(UITransitionSpec{}, 0.25f);
        EXPECT_EQ(evaluated.opacity, 0.25f);
        EXPECT_EQ(evaluated.scale, 1.0f);
        EXPECT_EQ(evaluated.offsetX, 0.0f);
        EXPECT_EQ(evaluated.offsetY, 0.0f);
    }

    TEST(UITransitionEvaluateTest, OvershootKeepsTheOpacityAndTheScaleInRange) {
        UITransitionSpec spec;
        spec.scaleFrom = 0.0f;
        const auto below = UITransition::Evaluate(spec, -0.5f);
        EXPECT_EQ(below.opacity, 0.0f);
        EXPECT_EQ(below.scale, 0.0f);
        const auto above = UITransition::Evaluate(spec, 1.5f);
        EXPECT_EQ(above.opacity, 1.0f);
        EXPECT_EQ(above.scale, 1.5f);
    }

    TEST(UITransitionEvaluateTest, NaNProgressIsShown) {
        UITransitionSpec spec;
        spec.scaleFrom = 0.5f;
        spec.slideX = 8.0f;
        const auto evaluated = UITransition::Evaluate(spec, kNaN);
        EXPECT_EQ(evaluated.opacity, 1.0f);
        EXPECT_EQ(evaluated.scale, 1.0f);
        EXPECT_EQ(evaluated.offsetX, 0.0f);
    }

    TEST(UITransitionComposeTest, IdentityKeepsTheView) {
        const ViewTransform view{2.0f, 10.0f, 20.0f};
        const ViewTransform composed = UITransition::Compose(view, {50.0f, 40.0f}, UITransition::Evaluated{});
        EXPECT_EQ(composed.scale, 2.0f);
        EXPECT_EQ(composed.offsetX, 10.0f);
        EXPECT_EQ(composed.offsetY, 20.0f);
    }

    TEST(UITransitionComposeTest, APointAtThePivotDoesNotMoveUnderScale) {
        const ViewTransform view{2.0f, 10.0f, 20.0f};
        const Vec2 pivot{50.0f, 40.0f};
        UITransition::Evaluated evaluated;
        evaluated.scale = 0.5f;
        const ViewTransform composed = UITransition::Compose(view, pivot, evaluated);
        EXPECT_EQ(composed.scale, 1.0f);
        EXPECT_EQ(Apply(composed, pivot), Apply(view, pivot));
        // another point comes half way to the pivot
        EXPECT_EQ(Apply(composed, {70.0f, 40.0f}), Apply(view, {60.0f, 40.0f}));
    }

    TEST(UITransitionComposeTest, SlideMovesByTheOffsetTimesTheViewScale) {
        const ViewTransform view{2.0f, 10.0f, 20.0f};
        UITransition::Evaluated evaluated;
        evaluated.offsetX = 4.0f;
        evaluated.offsetY = -6.0f;
        const ViewTransform composed = UITransition::Compose(view, {50.0f, 40.0f}, evaluated);
        EXPECT_EQ(composed.scale, 2.0f);
        EXPECT_EQ(composed.offsetX, 18.0f);
        EXPECT_EQ(composed.offsetY, 8.0f);
    }

    TEST(UITransitionComposeTest, ScaleAndSlideTogether) {
        const ViewTransform view{2.0f, 10.0f, 20.0f};
        UITransition::Evaluated evaluated;
        evaluated.scale = 0.5f;
        evaluated.offsetX = 4.0f;
        evaluated.offsetY = 6.0f;
        const ViewTransform composed = UITransition::Compose(view, {50.0f, 40.0f}, evaluated);
        EXPECT_EQ(composed.scale, 1.0f);
        EXPECT_EQ(composed.offsetX, 68.0f);
        EXPECT_EQ(composed.offsetY, 72.0f);
        // the pivot only moved by the slide, in pixels
        const Vec2 pivot{50.0f, 40.0f};
        EXPECT_EQ(Apply(composed, pivot), (Vec2{Apply(view, pivot).x + 8.0f, Apply(view, pivot).y + 12.0f}));
    }

    TEST(UITransitionRunTest, FirstSyncSnapsWithoutAnimation) {
        UITransitionRun shown;
        shown.Sync(true, 5.0, Spec(1.0f));
        EXPECT_EQ(shown.Progress(5.0), 1.0f);
        EXPECT_FALSE(shown.IsTransitioning(5.0));
        EXPECT_FALSE(shown.IsLeaving(5.0));
        EXPECT_FALSE(shown.TakeFinished(100.0));

        UITransitionRun hidden;
        hidden.Sync(false, 5.0, Spec(1.0f));
        EXPECT_EQ(hidden.Progress(5.0), 0.0f);
        EXPECT_FALSE(hidden.IsLeaving(5.0));
        EXPECT_FALSE(hidden.IsShowing(5.0));
    }

    TEST(UITransitionRunTest, AnUnsyncedRunIsHiddenAndQuiet) {
        const UITransitionRun run;
        EXPECT_EQ(run.Progress(1.0), 0.0f);
        EXPECT_FALSE(run.IsTransitioning(1.0));
        EXPECT_FALSE(run.IsLeaving(1.0));
        EXPECT_FALSE(run.TargetVisible());
    }

    TEST(UITransitionRunTest, ShowRisesEasedAndEndsExactly) {
        UITransitionRun run;
        run.Sync(false, 0.0, Spec(1.0f));
        run.Sync(true, 10.0, Spec(1.0f));
        EXPECT_TRUE(run.IsShowing(10.0));
        EXPECT_FALSE(run.IsLeaving(10.0));
        EXPECT_EQ(run.Progress(10.0), 0.0f);
        EXPECT_EQ(run.Progress(10.25), 0.4375f);
        EXPECT_EQ(run.Progress(10.5), 0.75f);
        EXPECT_EQ(run.Progress(10.75), 0.9375f);
        EXPECT_EQ(run.Progress(11.0), 1.0f);
        EXPECT_EQ(run.Progress(50.0), 1.0f);
        EXPECT_TRUE(run.IsTransitioning(10.5));
        EXPECT_FALSE(run.IsTransitioning(11.0));
        EXPECT_FALSE(run.IsShowing(11.0));
    }

    TEST(UITransitionRunTest, HideFallsAndKeepsLeavingUntilTheEnd) {
        UITransitionRun run;
        run.Sync(true, 0.0, Spec(1.0f));
        run.Sync(false, 10.0, Spec(1.0f));
        EXPECT_TRUE(run.IsLeaving(10.0));
        EXPECT_EQ(run.Progress(10.0), 1.0f);
        EXPECT_EQ(run.Progress(10.5), 0.25f);
        EXPECT_TRUE(run.IsLeaving(10.5));
        EXPECT_TRUE(run.IsLeaving(10.9375));
        EXPECT_EQ(run.Progress(11.0), 0.0f);
        EXPECT_FALSE(run.IsLeaving(11.0));
        EXPECT_FALSE(run.IsLeaving(12.0));
    }

    TEST(UITransitionRunTest, HideUsesItsOwnDuration) {
        UITransitionRun run;
        run.Sync(true, 0.0, Spec(1.0f, 2.0f));
        run.Sync(false, 10.0, Spec(1.0f, 2.0f));
        EXPECT_EQ(run.Progress(11.0), 0.25f);
        EXPECT_TRUE(run.IsLeaving(11.0));
        EXPECT_EQ(run.Progress(12.0), 0.0f);
    }

    TEST(UITransitionRunTest, TheSameTargetChangesNothing) {
        UITransitionRun run;
        run.Sync(false, 0.0, Spec(1.0f));
        run.Sync(true, 10.0, Spec(1.0f));
        run.Sync(true, 10.5, Spec(1.0f));
        EXPECT_EQ(run.Progress(10.5), 0.75f);
        EXPECT_EQ(run.Progress(11.0), 1.0f);
    }

    TEST(UITransitionRunTest, AnswerDependsOnTheTimeAskedNotOnTheCalls) {
        UITransitionRun once;
        once.Sync(false, 0.0, Spec(1.0f));
        once.Sync(true, 10.0, Spec(1.0f));
        UITransitionRun often = once;
        for (int i = 0; i < 100; ++i) {
            (void)often.Progress(10.25);
            (void)often.IsTransitioning(10.25);
        }
        EXPECT_EQ(once.Progress(10.5), often.Progress(10.5));
        // a held frame asks for the same time again and gets the same answer
        EXPECT_EQ(often.Progress(10.5), often.Progress(10.5));
    }

    TEST(UITransitionRunTest, ReversalMidwayContinuesFromTheShownProgress) {
        UITransitionRun run;
        run.Sync(true, 0.0, Spec(1.0f));
        run.Sync(false, 10.0, Spec(1.0f));
        ASSERT_EQ(run.Progress(10.5), 0.25f);

        run.Sync(true, 10.5, Spec(1.0f));
        EXPECT_EQ(run.Progress(10.5), 0.25f);  // no jump
        EXPECT_TRUE(run.IsShowing(10.5));
        // 0.75 of the way left takes 0.75 of the show duration: half of it is 0.375 s
        EXPECT_EQ(run.Progress(10.875), 0.8125f);
        EXPECT_EQ(run.Progress(11.25), 1.0f);
        EXPECT_TRUE(run.IsTransitioning(11.0));
        EXPECT_FALSE(run.IsTransitioning(11.25));
    }

    TEST(UITransitionRunTest, TakeFinishedAnswersOnceForARunThatEnded) {
        UITransitionRun run;
        run.Sync(true, 0.0, Spec(1.0f));
        run.Sync(false, 10.0, Spec(1.0f));
        EXPECT_FALSE(run.TakeFinished(10.5));
        EXPECT_FALSE(run.TakeFinished(10.9375));
        EXPECT_TRUE(run.TakeFinished(11.0));
        EXPECT_FALSE(run.TakeFinished(11.0));
        EXPECT_FALSE(run.TakeFinished(20.0));
        EXPECT_EQ(run.Progress(20.0), 0.0f);
        EXPECT_FALSE(run.IsLeaving(20.0));
    }

    TEST(UITransitionRunTest, ASyncThatCutsARunIsNotReportedAsFinished) {
        UITransitionRun run;
        run.Sync(true, 0.0, Spec(1.0f));
        run.Sync(false, 10.0, Spec(1.0f));
        run.Sync(true, 10.5, Spec(1.0f));
        EXPECT_FALSE(run.TakeFinished(10.5));
        EXPECT_TRUE(run.TakeFinished(11.25));
    }

    TEST(UITransitionRunTest, ANewRunAfterAFinishedOneStartsFromItsEnd) {
        UITransitionRun run;
        run.Sync(true, 0.0, Spec(1.0f));
        run.Sync(false, 10.0, Spec(1.0f));
        ASSERT_TRUE(run.TakeFinished(11.0));
        run.Sync(true, 20.0, Spec(1.0f));
        EXPECT_EQ(run.Progress(20.0), 0.0f);
        EXPECT_EQ(run.Progress(20.5), 0.75f);
    }

    TEST(UITransitionRunTest, NoDurationSnapsAtOnce) {
        for (const float duration : {0.0f, -1.0f, kNaN}) {
            UITransitionRun run;
            run.Sync(true, 0.0, Spec(duration));
            run.Sync(false, 10.0, Spec(duration));
            EXPECT_EQ(run.Progress(10.0), 0.0f) << duration;
            EXPECT_FALSE(run.IsTransitioning(10.0));
            EXPECT_FALSE(run.IsLeaving(10.0));
            EXPECT_FALSE(run.TakeFinished(11.0));
            run.Sync(true, 12.0, Spec(duration));
            EXPECT_EQ(run.Progress(12.0), 1.0f) << duration;
            EXPECT_FALSE(run.IsShowing(12.0));
        }
    }

    TEST(UITransitionRunTest, AHideWithNoHideDurationSnapsWhileTheShowAnimates) {
        UITransitionRun run;
        run.Sync(false, 0.0, Spec(1.0f, 0.0f));
        run.Sync(true, 10.0, Spec(1.0f, 0.0f));
        EXPECT_TRUE(run.IsShowing(10.25));
        run.Sync(false, 10.25, Spec(1.0f, 0.0f));
        EXPECT_EQ(run.Progress(10.25), 0.0f);
        EXPECT_FALSE(run.IsLeaving(10.25));
    }

    TEST(UITransitionRunTest, ReversalToTheSameProgressSnapsWithoutARun) {
        UITransitionRun run;
        run.Sync(true, 0.0, Spec(1.0f));
        run.Sync(false, 10.0, Spec(1.0f));
        run.Sync(true, 10.0, Spec(1.0f));  // reversed in the same instant: nothing to travel
        EXPECT_EQ(run.Progress(10.0), 1.0f);
        EXPECT_FALSE(run.IsTransitioning(10.0));
    }

    TEST(UITransitionRunTest, EaseComesFromTheSpecOfTheChange) {
        UITransitionSpec spec = Spec(1.0f);
        spec.ease = EaseType::Linear;
        UITransitionRun run;
        run.Sync(false, 0.0, spec);
        run.Sync(true, 10.0, spec);
        EXPECT_EQ(run.Progress(10.25), 0.25f);
        EXPECT_EQ(run.Progress(10.5), 0.5f);
    }

}  // namespace BECore::Tests
