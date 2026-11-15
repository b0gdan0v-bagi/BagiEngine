#include <Widgets/SceneEditor/FieldPresentation.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using namespace FieldPresentation;

        FieldAttributes WithRange(double min, double max) {
            FieldAttributes attributes;
            attributes.hasRange = true;
            attributes.rangeMin = min;
            attributes.rangeMax = max;
            return attributes;
        }

        FieldAttributes WithStep(double step) {
            FieldAttributes attributes;
            attributes.step = step;
            return attributes;
        }

    }  // namespace

    TEST(FieldPresentationTest, NoAttributesGiveADragWithTheSpeedOfTheKind) {
        const Presentation floating = Describe(FieldAttributes{}, NumberKind::Float);
        EXPECT_EQ(floating.widget, NumberWidget::Drag);
        EXPECT_DOUBLE_EQ(floating.speed, kFloatDragSpeed);
        EXPECT_DOUBLE_EQ(floating.snapStep, 0.0);
        EXPECT_FALSE(floating.disabled);
        EXPECT_TRUE(floating.tooltip.empty());

        const Presentation integer = Describe(FieldAttributes{}, NumberKind::Integer);
        EXPECT_EQ(integer.widget, NumberWidget::Drag);
        EXPECT_DOUBLE_EQ(integer.speed, kIntegerDragSpeed);
    }

    TEST(FieldPresentationTest, ARangeGivesASlider) {
        const Presentation presentation = Describe(WithRange(-1.0, 2.5), NumberKind::Float);
        EXPECT_EQ(presentation.widget, NumberWidget::Slider);
        EXPECT_DOUBLE_EQ(presentation.rangeMin, -1.0);
        EXPECT_DOUBLE_EQ(presentation.rangeMax, 2.5);
    }

    TEST(FieldPresentationTest, AStepWithoutARangeGivesADragWithThatSpeed) {
        const Presentation presentation = Describe(WithStep(0.05), NumberKind::Float);
        EXPECT_EQ(presentation.widget, NumberWidget::Drag);
        EXPECT_DOUBLE_EQ(presentation.speed, 0.05);
        EXPECT_DOUBLE_EQ(presentation.snapStep, 0.05);
    }

    TEST(FieldPresentationTest, ABadRangeOrStepCountsAsAbsent) {
        EXPECT_EQ(Describe(WithRange(1.0, 1.0), NumberKind::Float).widget, NumberWidget::Drag);
        EXPECT_EQ(Describe(WithRange(2.0, 1.0), NumberKind::Float).widget, NumberWidget::Drag);

        for (const double step : {0.0, -0.5}) {
            const Presentation presentation = Describe(WithStep(step), NumberKind::Float);
            EXPECT_DOUBLE_EQ(presentation.snapStep, 0.0);
            EXPECT_DOUBLE_EQ(presentation.speed, kFloatDragSpeed);
        }
    }

    TEST(FieldPresentationTest, ReadOnlyAndTooltipPassThrough) {
        FieldAttributes attributes;
        attributes.readOnly = true;
        attributes.tooltip = "How far";
        const Presentation presentation = Describe(attributes, NumberKind::Integer);
        EXPECT_TRUE(presentation.disabled);
        EXPECT_EQ(presentation.tooltip, "How far");
    }

    TEST(FieldPresentationTest, SnapToStepTakesTheNearestGridPointFromMin) {
        EXPECT_DOUBLE_EQ(SnapToStep(0.37, 0.0, 0.25), 0.25);
        EXPECT_DOUBLE_EQ(SnapToStep(0.38, 0.0, 0.25), 0.5);
        EXPECT_DOUBLE_EQ(SnapToStep(1.1, 0.5, 0.5), 1.0);
        EXPECT_DOUBLE_EQ(SnapToStep(-0.3, 0.0, 0.25), -0.25);
        EXPECT_DOUBLE_EQ(SnapToStep(0.3, 0.0, 0.0), 0.3);
        EXPECT_DOUBLE_EQ(SnapToStep(0.3, 0.0, -1.0), 0.3);
    }

    TEST(FieldPresentationTest, ConstrainClampsToTheRangeAndSnapsFromItsStart) {
        FieldAttributes attributes = WithRange(0.1, 1.0);
        attributes.step = 0.25;
        const Presentation presentation = Describe(attributes, NumberKind::Float);

        EXPECT_DOUBLE_EQ(Constrain(presentation, 0.5), 0.6);
        EXPECT_DOUBLE_EQ(Constrain(presentation, -4.0), 0.1);
        // The grid is 0.1, 0.35, 0.6, 0.85, 1.1: the last point is above the range, so the result stops at the end
        EXPECT_DOUBLE_EQ(Constrain(presentation, 5.0), 1.0);
        EXPECT_DOUBLE_EQ(Constrain(presentation, 0.99), 1.0);
    }

    TEST(FieldPresentationTest, ConstrainClampsWithoutAStepAndLeavesADragUnclamped) {
        const Presentation slider = Describe(WithRange(0.0, 1.0), NumberKind::Float);
        EXPECT_DOUBLE_EQ(Constrain(slider, 0.123), 0.123);
        EXPECT_DOUBLE_EQ(Constrain(slider, 1.5), 1.0);

        const Presentation drag = Describe(WithStep(0.5), NumberKind::Float);
        EXPECT_DOUBLE_EQ(Constrain(drag, -7.3), -7.5);

        const Presentation plain = Describe(FieldAttributes{}, NumberKind::Float);
        EXPECT_DOUBLE_EQ(Constrain(plain, 123.456), 123.456);
    }

    TEST(FieldPresentationTest, ConstrainRoundsAnIntegerKindInsideAFractionalRange) {
        const Presentation plain = Describe(FieldAttributes{}, NumberKind::Integer);
        EXPECT_DOUBLE_EQ(Constrain(plain, 2.6), 3.0);

        const Presentation ranged = Describe(WithRange(0.5, 2.5), NumberKind::Integer);
        EXPECT_DOUBLE_EQ(Constrain(ranged, 2.5), 2.0);
        EXPECT_DOUBLE_EQ(Constrain(ranged, 0.5), 1.0);
        EXPECT_DOUBLE_EQ(Constrain(ranged, 9.0), 2.0);
        EXPECT_DOUBLE_EQ(Constrain(ranged, -9.0), 1.0);
    }

    TEST(FieldPresentationTest, FromDoubleRoundsAnIntegerAndStaysInsideItsType) {
        EXPECT_EQ(FromDouble<int32_t>(2.5), 3);
        EXPECT_EQ(FromDouble<int32_t>(-2.4), -2);
        EXPECT_EQ(FromDouble<uint8_t>(300.0), 255);
        EXPECT_EQ(FromDouble<uint8_t>(-3.0), 0);
        EXPECT_EQ(FromDouble<int64_t>(1e30), std::numeric_limits<int64_t>::max());
        EXPECT_EQ(FromDouble<uint64_t>(1e30), std::numeric_limits<uint64_t>::max());
        EXPECT_EQ(FromDouble<int64_t>(-1e30), std::numeric_limits<int64_t>::lowest());
        EXPECT_FLOAT_EQ(FromDouble<float>(0.25), 0.25f);
    }

}  // namespace BECore::Tests
