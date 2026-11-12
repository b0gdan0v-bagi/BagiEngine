#include <Widgets/Viewport/GizmoGeometry.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace Gizmo;

    namespace {

        constexpr float kTolerance = 1e-3f;

        void ExpectVec(Vec2 expected, Vec2 actual) {
            EXPECT_NEAR(expected.x, actual.x, kTolerance);
            EXPECT_NEAR(expected.y, actual.y, kTolerance);
        }

        Item MakeItem(float x, float y, float width, float height, float rotation = 0.0f, float pivotX = 0.5f, float pivotY = 0.5f) {
            Item item;
            item.x = x;
            item.y = y;
            item.width = width;
            item.height = height;
            item.rotation = rotation;
            item.pivotX = pivotX;
            item.pivotY = pivotY;
            return item;
        }

        Frame FrameOf(const Item& item, Space space = Space::World) {
            return MakeFrame(eastl::span<const Item>(&item, 1), space);
        }

        // A 100 x 50 item at the origin: its centre is (50, 25)
        const Item kFlat = MakeItem(0.0f, 0.0f, 100.0f, 50.0f);
        constexpr Vec2 kFlatCenter{50.0f, 25.0f};

        // A 40 x 20 item turned a quarter about its centre (120, 110): it stands 20 wide and 40 tall, corners (130, 90) (130, 130) (110, 130) (110, 90)
        const Item kQuarter = MakeItem(100.0f, 100.0f, 40.0f, 20.0f, 90.0f);

    }  // namespace

    TEST(GizmoGeometryTest, SnapValueRoundsToTheNearestStepAndHalvesGoAwayFromZero) {
        EXPECT_FLOAT_EQ(SnapValue(13.0f, 8.0f), 16.0f);
        EXPECT_FLOAT_EQ(SnapValue(11.0f, 8.0f), 8.0f);
        EXPECT_FLOAT_EQ(SnapValue(12.0f, 8.0f), 16.0f);
        EXPECT_FLOAT_EQ(SnapValue(-13.0f, 8.0f), -16.0f);
        EXPECT_FLOAT_EQ(SnapValue(-12.0f, 8.0f), -16.0f);
        EXPECT_FLOAT_EQ(SnapValue(0.0f, 8.0f), 0.0f);
        EXPECT_NEAR(SnapValue(0.37f, 0.1f), 0.4f, kTolerance);
    }

    TEST(GizmoGeometryTest, ZeroOrNegativeStepSwitchesSnappingOff) {
        EXPECT_FLOAT_EQ(SnapValue(13.3f, 0.0f), 13.3f);
        EXPECT_FLOAT_EQ(SnapValue(13.3f, -8.0f), 13.3f);
        EXPECT_FLOAT_EQ(SnapAngle(13.0f, 0.0f), 13.0f);
    }

    TEST(GizmoGeometryTest, SnapAngleSnapsAndWrapsIntoHalfOpenRange) {
        EXPECT_FLOAT_EQ(SnapAngle(100.0f, 15.0f), 105.0f);
        EXPECT_FLOAT_EQ(SnapAngle(7.0f, 15.0f), 0.0f);
        EXPECT_FLOAT_EQ(SnapAngle(180.0f, 15.0f), 180.0f);
        EXPECT_FLOAT_EQ(SnapAngle(-180.0f, 15.0f), 180.0f);
        EXPECT_FLOAT_EQ(SnapAngle(190.0f, 0.0f), -170.0f);
        EXPECT_FLOAT_EQ(SnapAngle(-190.0f, 0.0f), 170.0f);
        EXPECT_NEAR(NormalizeAngle(725.0f), 5.0f, kTolerance);
        EXPECT_NEAR(NormalizeAngle(-725.0f), -5.0f, kTolerance);
    }

    TEST(GizmoGeometryTest, SnapModifierFlipsTheThreeSwitchesAndKeepsTheSteps) {
        SnapSettings snap;
        snap.position = true;
        snap.positionStep = 4.0f;
        const SnapSettings same = WithSnapModifier(snap, false);
        EXPECT_TRUE(same.position);
        EXPECT_FALSE(same.angle);
        const SnapSettings flipped = WithSnapModifier(snap, true);
        EXPECT_FALSE(flipped.position);
        EXPECT_TRUE(flipped.angle);
        EXPECT_TRUE(flipped.scale);
        EXPECT_FLOAT_EQ(flipped.positionStep, 4.0f);
        EXPECT_FLOAT_EQ(flipped.angleStep, kDefaultAngleStep);
    }

    TEST(GizmoGeometryTest, DefaultSnapStepsAreTheDocumentedOnes) {
        const SnapSettings snap;
        EXPECT_FLOAT_EQ(snap.positionStep, 8.0f);
        EXPECT_FLOAT_EQ(snap.angleStep, 15.0f);
        EXPECT_FLOAT_EQ(snap.scaleStep, 0.1f);
    }

    TEST(GizmoGeometryTest, PivotPointOfAnItemFollowsItsPivot) {
        ExpectVec({50.0f, 25.0f}, PivotPoint(kFlat));
        ExpectVec({10.0f, 40.0f}, PivotPoint(MakeItem(10.0f, 20.0f, 40.0f, 20.0f, 0.0f, 0.0f, 1.0f)));
    }

    TEST(GizmoGeometryTest, TurnedQuadTurnsClockwiseAboutThePivot) {
        const Quad quad = TurnedQuad(kQuarter);
        ExpectVec({130.0f, 90.0f}, quad[0]);
        ExpectVec({130.0f, 130.0f}, quad[1]);
        ExpectVec({110.0f, 130.0f}, quad[2]);
        ExpectVec({110.0f, 90.0f}, quad[3]);
    }

    TEST(GizmoGeometryTest, BoundsOfOneTurnedItemAreThoseOfItsQuad) {
        const Item items[] = {kQuarter};
        const Rect bounds = Bounds(items);
        EXPECT_NEAR(bounds.x, 110.0f, kTolerance);
        EXPECT_NEAR(bounds.y, 90.0f, kTolerance);
        EXPECT_NEAR(bounds.w, 20.0f, kTolerance);
        EXPECT_NEAR(bounds.h, 40.0f, kTolerance);
        ExpectVec({120.0f, 110.0f}, Pivot(items));
    }

    TEST(GizmoGeometryTest, PivotOfOneItemIsItsOwnPivotEvenOffCentre) {
        const Item items[] = {MakeItem(100.0f, 100.0f, 40.0f, 20.0f, 90.0f, 0.0f, 0.0f)};
        ExpectVec({100.0f, 100.0f}, Pivot(items));
    }

    TEST(GizmoGeometryTest, PivotOfAGroupIsTheCentreOfItsBounds) {
        const Item items[] = {MakeItem(0.0f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f, 0.0f), MakeItem(30.0f, 20.0f, 10.0f, 10.0f)};
        const Rect bounds = Bounds(items);
        EXPECT_FLOAT_EQ(bounds.x, 0.0f);
        EXPECT_FLOAT_EQ(bounds.y, 0.0f);
        EXPECT_FLOAT_EQ(bounds.w, 40.0f);
        EXPECT_FLOAT_EQ(bounds.h, 30.0f);
        ExpectVec({20.0f, 15.0f}, Pivot(items));
    }

    TEST(GizmoGeometryTest, BoundsOfAGroupCountTheTurnedQuads) {
        const Item items[] = {kQuarter, MakeItem(200.0f, 0.0f, 10.0f, 10.0f)};
        const Rect bounds = Bounds(items);
        EXPECT_NEAR(bounds.x, 110.0f, kTolerance);
        EXPECT_NEAR(bounds.y, 0.0f, kTolerance);
        EXPECT_NEAR(bounds.w, 100.0f, kTolerance);
        EXPECT_NEAR(bounds.h, 130.0f, kTolerance);
    }

    TEST(GizmoGeometryTest, NoItemsGiveAnEmptyRectAndTheOrigin) {
        EXPECT_TRUE(Bounds({}).IsEmpty());
        ExpectVec({0.0f, 0.0f}, Pivot({}));
        ExpectVec({0.0f, 0.0f}, MakeFrame({}, Space::Local).origin);
    }

    TEST(GizmoGeometryTest, FrameAnglePerSpace) {
        const Item turned = MakeItem(0.0f, 0.0f, 10.0f, 10.0f, 30.0f);
        EXPECT_FLOAT_EQ(FrameOf(turned, Space::World).angle, 0.0f);
        EXPECT_FLOAT_EQ(FrameOf(turned, Space::Local).angle, 30.0f);
        const Item group[] = {turned, MakeItem(20.0f, 0.0f, 10.0f, 10.0f)};
        const Frame groupFrame = MakeFrame(group, Space::Local);
        EXPECT_FLOAT_EQ(groupFrame.angle, 0.0f);
        EXPECT_TRUE(groupFrame.isGroup);
        EXPECT_FALSE(FrameOf(turned).isGroup);
    }

    TEST(GizmoGeometryTest, ArrowsOfAnUnturnedFrameGoRightAndDown) {
        const Frame frame = FrameOf(kFlat);
        ExpectVec(kFlatCenter, frame.origin);
        ExpectVec({1.0f, 0.0f}, AxisX(frame));
        ExpectVec({0.0f, 1.0f}, AxisY(frame));
        ExpectVec({50.0f + kArrowLength, 25.0f}, HandlePosition(frame, Handle::AxisX, 1.0f));
        ExpectVec({50.0f, 25.0f + kArrowLength}, HandlePosition(frame, Handle::AxisY, 1.0f));
        ExpectVec(kFlatCenter, HandlePosition(frame, Handle::Free, 1.0f));
        ExpectVec({50.0f + kRingRadius, 25.0f}, HandlePosition(frame, Handle::Ring, 1.0f));
    }

    TEST(GizmoGeometryTest, WorldFrameKeepsScreenAxesForATurnedItem) {
        const Frame frame = FrameOf(kQuarter, Space::World);
        ExpectVec({120.0f + kArrowLength, 110.0f}, HandlePosition(frame, Handle::AxisX, 1.0f));
        ExpectVec({120.0f, 110.0f + kArrowLength}, HandlePosition(frame, Handle::AxisY, 1.0f));
    }

    TEST(GizmoGeometryTest, LocalFrameTurnsTheArrowsWithAQuarterTurn) {
        const Frame frame = FrameOf(kQuarter, Space::Local);
        // Clockwise on a screen whose y points down: the X axis points down, the Y axis to the left
        ExpectVec({120.0f, 110.0f + kArrowLength}, HandlePosition(frame, Handle::AxisX, 1.0f));
        ExpectVec({120.0f - kArrowLength, 110.0f}, HandlePosition(frame, Handle::AxisY, 1.0f));
    }

    TEST(GizmoGeometryTest, LocalFrameTurnsTheArrowsByThirtyDegrees) {
        const Item turned = MakeItem(0.0f, 0.0f, 20.0f, 20.0f, 30.0f);
        const Frame frame = FrameOf(turned, Space::Local);
        const float cosine = 0.8660254f;  // cos 30 degrees
        const float sine = 0.5f;          // sin 30 degrees
        ExpectVec({10.0f + kArrowLength * cosine, 10.0f + kArrowLength * sine}, HandlePosition(frame, Handle::AxisX, 1.0f));
        ExpectVec({10.0f - kArrowLength * sine, 10.0f + kArrowLength * cosine}, HandlePosition(frame, Handle::AxisY, 1.0f));
    }

    TEST(GizmoGeometryTest, HandleSizeFollowsUnitsPerScreenPixel) {
        const Frame frame = FrameOf(kFlat);
        ExpectVec({50.0f + 2.0f * kArrowLength, 25.0f}, HandlePosition(frame, Handle::AxisX, 2.0f));
        ExpectVec({50.0f + 0.5f * kRingRadius, 25.0f}, HandlePosition(frame, Handle::Ring, 0.5f));
        EXPECT_FLOAT_EQ(RingRadius(2.0f), 2.0f * kRingRadius);
        // A scale that is zero or negative falls back to one unit per pixel
        EXPECT_FLOAT_EQ(RingRadius(0.0f), kRingRadius);
        EXPECT_FLOAT_EQ(RingRadius(-3.0f), kRingRadius);
    }

    TEST(GizmoGeometryTest, ScaleHandlesSitOnTheTurnedRect) {
        const Frame frame = FrameOf(kQuarter);
        ExpectVec({130.0f, 90.0f}, HandlePosition(frame, Handle::Corner0, 1.0f));
        ExpectVec({130.0f, 130.0f}, HandlePosition(frame, Handle::Corner1, 1.0f));
        ExpectVec({110.0f, 130.0f}, HandlePosition(frame, Handle::Corner2, 1.0f));
        ExpectVec({110.0f, 90.0f}, HandlePosition(frame, Handle::Corner3, 1.0f));
        ExpectVec({130.0f, 110.0f}, HandlePosition(frame, Handle::Edge0, 1.0f));
        ExpectVec({120.0f, 130.0f}, HandlePosition(frame, Handle::Edge1, 1.0f));
        ExpectVec({110.0f, 110.0f}, HandlePosition(frame, Handle::Edge2, 1.0f));
        ExpectVec({120.0f, 90.0f}, HandlePosition(frame, Handle::Edge3, 1.0f));
    }

    TEST(GizmoGeometryTest, ScaleHandlesOfAGroupSitOnItsBounds) {
        const Item group[] = {MakeItem(0.0f, 0.0f, 10.0f, 10.0f), MakeItem(30.0f, 20.0f, 10.0f, 10.0f)};
        const Frame frame = MakeFrame(group, Space::World);
        ExpectVec({0.0f, 0.0f}, HandlePosition(frame, Handle::Corner0, 1.0f));
        ExpectVec({40.0f, 0.0f}, HandlePosition(frame, Handle::Corner1, 1.0f));
        ExpectVec({40.0f, 30.0f}, HandlePosition(frame, Handle::Corner2, 1.0f));
        ExpectVec({0.0f, 30.0f}, HandlePosition(frame, Handle::Corner3, 1.0f));
    }

    TEST(GizmoGeometryTest, HandleAnchorsAreThePlacesInTheRect) {
        ExpectVec({0.0f, 0.0f}, HandleAnchor(Handle::Corner0));
        ExpectVec({1.0f, 1.0f}, HandleAnchor(Handle::Corner2));
        ExpectVec({1.0f, 0.5f}, HandleAnchor(Handle::Edge1));
        ExpectVec({0.5f, 0.0f}, HandleAnchor(Handle::Edge0));
        EXPECT_TRUE(IsCorner(Handle::Corner3));
        EXPECT_FALSE(IsCorner(Handle::Edge0));
        EXPECT_TRUE(IsEdge(Handle::Edge3));
        EXPECT_FALSE(IsEdge(Handle::Ring));
    }

    TEST(GizmoGeometryTest, TranslateHitsTheCentreSquareAndTheArrows) {
        const Frame frame = FrameOf(kFlat);
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter, 1.0f), Handle::Free);
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter + Vec2{kCenterHalfSize, -kCenterHalfSize}, 1.0f), Handle::Free);
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter + Vec2{30.0f, 0.0f}, 1.0f), Handle::AxisX);
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter + Vec2{0.0f, 30.0f}, 1.0f), Handle::AxisY);
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter + Vec2{30.0f, kHandleHitRadius - 0.5f}, 1.0f), Handle::AxisX);
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter + Vec2{30.0f, kHandleHitRadius + 0.5f}, 1.0f), Handle::None);
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter + Vec2{40.0f, 40.0f}, 1.0f), Handle::None);
        // Behind the centre there is no arrow; past the tip only the hit radius counts
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter + Vec2{-30.0f, 0.0f}, 1.0f), Handle::None);
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter + Vec2{kArrowLength + kHandleHitRadius - 1.0f, 0.0f}, 1.0f), Handle::AxisX);
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter + Vec2{kArrowLength + kHandleHitRadius + 1.0f, 0.0f}, 1.0f), Handle::None);
    }

    TEST(GizmoGeometryTest, TranslateCentreSquareWinsOverTheArrowsAndTheNearerArrowWins) {
        const Frame frame = FrameOf(kFlat);
        // On both the square and the X arrow
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter + Vec2{kCenterHalfSize - 1.0f, 1.0f}, 1.0f), Handle::Free);
        // Outside the square, within reach of both arrows: the nearer one
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter + Vec2{kCenterHalfSize + 3.0f, 5.0f}, 1.0f), Handle::AxisX);
        EXPECT_EQ(HitTest(frame, Tool::Translate, kFlatCenter + Vec2{5.0f, kCenterHalfSize + 3.0f}, 1.0f), Handle::AxisY);
    }

    TEST(GizmoGeometryTest, TranslateHitsTheTurnedArrowsInLocalSpaceOnly) {
        const Frame local = FrameOf(kQuarter, Space::Local);
        const Vec2 center{120.0f, 110.0f};
        EXPECT_EQ(HitTest(local, Tool::Translate, center + Vec2{0.0f, 30.0f}, 1.0f), Handle::AxisX);
        EXPECT_EQ(HitTest(local, Tool::Translate, center + Vec2{-30.0f, 0.0f}, 1.0f), Handle::AxisY);
        EXPECT_EQ(HitTest(local, Tool::Translate, center + Vec2{30.0f, 0.0f}, 1.0f), Handle::None);
        const Frame world = FrameOf(kQuarter, Space::World);
        EXPECT_EQ(HitTest(world, Tool::Translate, center + Vec2{30.0f, 0.0f}, 1.0f), Handle::AxisX);
        EXPECT_EQ(HitTest(world, Tool::Translate, center + Vec2{0.0f, 30.0f}, 1.0f), Handle::AxisY);
    }

    TEST(GizmoGeometryTest, TranslateHitsAThirtyDegreeArrowAlongItsOwnDirection) {
        const Item turned = MakeItem(0.0f, 0.0f, 20.0f, 20.0f, 30.0f);
        const Frame frame = FrameOf(turned, Space::Local);
        const Vec2 along = AxisX(frame);
        EXPECT_EQ(HitTest(frame, Tool::Translate, frame.origin + along * 40.0f, 1.0f), Handle::AxisX);
        EXPECT_EQ(HitTest(frame, Tool::Translate, frame.origin + Vec2{40.0f, 0.0f}, 1.0f), Handle::None);
    }

    TEST(GizmoGeometryTest, RotateHitsTheRingOnly) {
        const Frame frame = FrameOf(kFlat);
        EXPECT_EQ(HitTest(frame, Tool::Rotate, kFlatCenter + Vec2{kRingRadius, 0.0f}, 1.0f), Handle::Ring);
        EXPECT_EQ(HitTest(frame, Tool::Rotate, kFlatCenter + Vec2{0.0f, -kRingRadius}, 1.0f), Handle::Ring);
        EXPECT_EQ(HitTest(frame, Tool::Rotate, kFlatCenter + Vec2{kRingRadius + kRingHitHalfWidth - 0.5f, 0.0f}, 1.0f), Handle::Ring);
        EXPECT_EQ(HitTest(frame, Tool::Rotate, kFlatCenter + Vec2{kRingRadius + kRingHitHalfWidth + 0.5f, 0.0f}, 1.0f), Handle::None);
        EXPECT_EQ(HitTest(frame, Tool::Rotate, kFlatCenter + Vec2{kRingRadius - kRingHitHalfWidth - 0.5f, 0.0f}, 1.0f), Handle::None);
        EXPECT_EQ(HitTest(frame, Tool::Rotate, kFlatCenter, 1.0f), Handle::None);
    }

    TEST(GizmoGeometryTest, ScaleHitsCornersAndEdgeMidpoints) {
        const Frame frame = FrameOf(kFlat);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {3.0f, 3.0f}, 1.0f), Handle::Corner0);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {103.0f, -2.0f}, 1.0f), Handle::Corner1);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {104.0f, 50.0f}, 1.0f), Handle::Corner2);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {0.0f, 52.0f}, 1.0f), Handle::Corner3);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {50.0f, 5.0f}, 1.0f), Handle::Edge0);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {100.0f, 25.0f}, 1.0f), Handle::Edge1);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {50.0f, 50.0f}, 1.0f), Handle::Edge2);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {-4.0f, 25.0f}, 1.0f), Handle::Edge3);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {25.0f, 0.0f}, 1.0f), Handle::None);
        EXPECT_EQ(HitTest(frame, Tool::Scale, kFlatCenter, 1.0f), Handle::None);
    }

    TEST(GizmoGeometryTest, ScaleHitsTheHandlesOfATurnedRect) {
        const Frame frame = FrameOf(kQuarter);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {130.0f, 90.0f}, 1.0f), Handle::Corner0);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {110.0f, 130.0f}, 1.0f), Handle::Corner2);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {120.0f, 130.0f}, 1.0f), Handle::Edge1);
    }

    TEST(GizmoGeometryTest, ScaleNearerHandleWinsAndATieKeepsTheCorner) {
        // On a 10 x 10 rect the corner (0, 0) and the top edge middle (5, 0) are within reach of one another
        const Item tiny = MakeItem(0.0f, 0.0f, 10.0f, 10.0f);
        const Frame frame = FrameOf(tiny);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {1.0f, 0.0f}, 1.0f), Handle::Corner0);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {4.0f, 0.0f}, 1.0f), Handle::Edge0);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {2.5f, 0.0f}, 1.0f), Handle::Corner0);
    }

    TEST(GizmoGeometryTest, ScaleOfAGroupOffersTheCornersOnly) {
        const Item group[] = {MakeItem(0.0f, 0.0f, 10.0f, 10.0f), MakeItem(30.0f, 20.0f, 10.0f, 10.0f)};
        const Frame frame = MakeFrame(group, Space::World);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {40.0f, 30.0f}, 1.0f), Handle::Corner2);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {20.0f, 0.0f}, 1.0f), Handle::None);
    }

    TEST(GizmoGeometryTest, HitReachFollowsUnitsPerScreenPixel) {
        const Frame frame = FrameOf(kFlat);
        const Vec2 point = kFlatCenter + Vec2{10.0f, 0.0f};
        EXPECT_EQ(HitTest(frame, Tool::Translate, point, 1.0f), Handle::AxisX);
        EXPECT_EQ(HitTest(frame, Tool::Translate, point, 2.0f), Handle::Free);
        // A hundred units out is past the arrow at one unit per pixel and on it at two
        const Vec2 far = kFlatCenter + Vec2{100.0f, 0.0f};
        EXPECT_EQ(HitTest(frame, Tool::Translate, far, 1.0f), Handle::None);
        EXPECT_EQ(HitTest(frame, Tool::Translate, far, 2.0f), Handle::AxisX);
        EXPECT_EQ(HitTest(frame, Tool::Rotate, kFlatCenter + Vec2{2.0f * kRingRadius, 0.0f}, 2.0f), Handle::Ring);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {3.0f, 3.0f}, 0.1f), Handle::None);
        EXPECT_EQ(HitTest(frame, Tool::Scale, {12.0f, 12.0f}, 4.0f), Handle::Corner0);
        // A scale that is zero or negative falls back to one
        EXPECT_EQ(HitTest(frame, Tool::Translate, point, 0.0f), HitTest(frame, Tool::Translate, point, 1.0f));
    }

}  // namespace BECore::Tests
