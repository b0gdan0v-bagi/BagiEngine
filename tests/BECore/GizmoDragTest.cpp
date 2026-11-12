#include <Widgets/Viewport/GizmoDrag.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace Gizmo;

    namespace {

        constexpr float kTolerance = 1e-3f;
        constexpr float kThirtyDegreesCos = 0.8660254f;
        constexpr float kThirtyDegreesSin = 0.5f;

        void ExpectVec(Vec2 expected, Vec2 actual) {
            EXPECT_NEAR(expected.x, actual.x, kTolerance);
            EXPECT_NEAR(expected.y, actual.y, kTolerance);
        }

        void ExpectItem(const Item& expected, const Item& actual) {
            EXPECT_NEAR(expected.x, actual.x, kTolerance);
            EXPECT_NEAR(expected.y, actual.y, kTolerance);
            EXPECT_NEAR(expected.width, actual.width, kTolerance);
            EXPECT_NEAR(expected.height, actual.height, kTolerance);
            EXPECT_NEAR(expected.rotation, actual.rotation, kTolerance);
            EXPECT_NEAR(expected.pivotX, actual.pivotX, kTolerance);
            EXPECT_NEAR(expected.pivotY, actual.pivotY, kTolerance);
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

        /// Where the point (u, v) of the item's rect is on the scene, rotation and pivot applied
        Vec2 WorldPointOf(const Item& item, float u, float v) {
            return TurnAbout({item.x + u * item.width, item.y + v * item.height}, PivotPoint(item), item.rotation);
        }

        SnapSettings SnapAll() {
            SnapSettings snap;
            snap.position = true;
            snap.angle = true;
            snap.scale = true;
            return snap;
        }

        /// One whole drag in one update: press at @p from, pointer at @p to; the result is a copy
        eastl::vector<Item> Drag(Tool tool, Space space, Handle handle, eastl::span<const Item> items, Vec2 from, Vec2 to, const SnapSettings& snap = {}, bool snapModifier = false,
                                 bool keepAspect = false) {
            GizmoDrag drag;
            drag.Begin(tool, space, handle, items, from, snap);
            const eastl::span<const Item> result = drag.Update(to, snapModifier, keepAspect);
            return {result.begin(), result.end()};
        }

        eastl::vector<Item> DragOne(Tool tool, Handle handle, const Item& item, Vec2 from, Vec2 to, const SnapSettings& snap = {}, bool snapModifier = false, bool keepAspect = false,
                                    Space space = Space::World) {
            const Item items[] = {item};
            return Drag(tool, space, handle, items, from, to, snap, snapModifier, keepAspect);
        }

        const Item kFlat = MakeItem(0.0f, 0.0f, 100.0f, 50.0f);
        constexpr Vec2 kFlatCenter{50.0f, 25.0f};

        /// A point @p radius from @p center at @p degrees (clockwise from the right on a screen with y down)
        Vec2 OnCircle(Vec2 center, float radius, float degrees) {
            return TurnAbout(center + Vec2{radius, 0.0f}, center, degrees);
        }

        constexpr Handle kScaleHandles[] = {Handle::Corner0, Handle::Corner1, Handle::Corner2, Handle::Corner3, Handle::Edge0, Handle::Edge1, Handle::Edge2, Handle::Edge3};

        // The sides a handle moves, -1 / 0 / +1 on each axis of the rect
        Vec2 SidesOf(Handle handle) {
            const Vec2 anchor = HandleAnchor(handle);
            const auto side = [](float value) { return value < 0.5f ? -1.0f : (value > 0.5f ? 1.0f : 0.0f); };
            return {side(anchor.x), side(anchor.y)};
        }

    }  // namespace

    TEST(GizmoDragTest, NothingStartsWithoutItemsOrAHandle) {
        GizmoDrag drag;
        const Item items[] = {kFlat};
        drag.Begin(Tool::Translate, Space::World, Handle::None, items, {0.0f, 0.0f}, {});
        EXPECT_FALSE(drag.IsActive());
        EXPECT_TRUE(drag.Update({10.0f, 10.0f}, false).empty());
        drag.Begin(Tool::Translate, Space::World, Handle::Free, {}, {0.0f, 0.0f}, {});
        EXPECT_FALSE(drag.IsActive());
        EXPECT_TRUE(drag.Update({10.0f, 10.0f}, false).empty());
        drag.Begin(Tool::Translate, Space::World, Handle::Free, items, {0.0f, 0.0f}, {});
        EXPECT_TRUE(drag.IsActive());
    }

    TEST(GizmoDragTest, TranslateFreeMovesByThePointersOffset) {
        const auto result = DragOne(Tool::Translate, Handle::Free, MakeItem(10.0f, 20.0f, 40.0f, 30.0f), {100.0f, 100.0f}, {130.0f, 90.0f});
        ASSERT_EQ(result.size(), 1u);
        ExpectItem(MakeItem(40.0f, 10.0f, 40.0f, 30.0f), result[0]);
    }

    TEST(GizmoDragTest, TranslateAlongAWorldAxisKeepsTheOtherCoordinate) {
        const Item item = MakeItem(10.0f, 20.0f, 40.0f, 30.0f);
        ExpectItem(MakeItem(40.0f, 20.0f, 40.0f, 30.0f), DragOne(Tool::Translate, Handle::AxisX, item, {100.0f, 100.0f}, {130.0f, 90.0f})[0]);
        ExpectItem(MakeItem(10.0f, 10.0f, 40.0f, 30.0f), DragOne(Tool::Translate, Handle::AxisY, item, {100.0f, 100.0f}, {130.0f, 90.0f})[0]);
    }

    TEST(GizmoDragTest, TranslateAlongALocalAxisOfAQuarterTurnedItem) {
        const Item item = MakeItem(10.0f, 20.0f, 40.0f, 30.0f, 90.0f);
        // Turned a quarter clockwise, the item's X axis points down the screen and its Y axis to the left
        const auto alongX = DragOne(Tool::Translate, Handle::AxisX, item, {100.0f, 100.0f}, {130.0f, 140.0f}, {}, false, false, Space::Local);
        ExpectItem(MakeItem(10.0f, 60.0f, 40.0f, 30.0f, 90.0f), alongX[0]);
        const auto alongY = DragOne(Tool::Translate, Handle::AxisY, item, {100.0f, 100.0f}, {130.0f, 140.0f}, {}, false, false, Space::Local);
        ExpectItem(MakeItem(40.0f, 20.0f, 40.0f, 30.0f, 90.0f), alongY[0]);
        // The same drag in World space follows the screen's X axis
        const auto world = DragOne(Tool::Translate, Handle::AxisX, item, {100.0f, 100.0f}, {130.0f, 140.0f});
        ExpectItem(MakeItem(40.0f, 20.0f, 40.0f, 30.0f, 90.0f), world[0]);
    }

    TEST(GizmoDragTest, TranslateMovesAGroupAsOne) {
        const Item items[] = {MakeItem(0.0f, 0.0f, 10.0f, 10.0f), MakeItem(30.0f, 20.0f, 10.0f, 10.0f, 45.0f)};
        const auto result = Drag(Tool::Translate, Space::World, Handle::Free, items, {5.0f, 5.0f}, {15.0f, -5.0f});
        ASSERT_EQ(result.size(), 2u);
        ExpectItem(MakeItem(10.0f, -10.0f, 10.0f, 10.0f), result[0]);
        ExpectItem(MakeItem(40.0f, 10.0f, 10.0f, 10.0f, 45.0f), result[1]);
    }

    TEST(GizmoDragTest, TranslateSnapRoundsTheFirstItemAndMovesTheOthersByTheSameOffset) {
        SnapSettings snap;
        snap.position = true;
        const Item items[] = {MakeItem(3.0f, 5.0f, 10.0f, 10.0f), MakeItem(100.0f, 101.0f, 10.0f, 10.0f)};
        const auto result = Drag(Tool::Translate, Space::World, Handle::Free, items, {0.0f, 0.0f}, {10.0f, 10.0f}, snap);
        ExpectItem(MakeItem(16.0f, 16.0f, 10.0f, 10.0f), result[0]);
        // The same offset (13, 11) as the first item got, so the group keeps its shape
        ExpectItem(MakeItem(113.0f, 112.0f, 10.0f, 10.0f), result[1]);
    }

    TEST(GizmoDragTest, TranslateSnapIsOffByDefaultAndTheModifierInvertsIt) {
        const Item item = MakeItem(3.0f, 5.0f, 10.0f, 10.0f);
        ExpectItem(MakeItem(13.0f, 15.0f, 10.0f, 10.0f), DragOne(Tool::Translate, Handle::Free, item, {0.0f, 0.0f}, {10.0f, 10.0f})[0]);
        ExpectItem(MakeItem(16.0f, 16.0f, 10.0f, 10.0f), DragOne(Tool::Translate, Handle::Free, item, {0.0f, 0.0f}, {10.0f, 10.0f}, {}, true)[0]);
        SnapSettings on;
        on.position = true;
        ExpectItem(MakeItem(16.0f, 16.0f, 10.0f, 10.0f), DragOne(Tool::Translate, Handle::Free, item, {0.0f, 0.0f}, {10.0f, 10.0f}, on)[0]);
        ExpectItem(MakeItem(13.0f, 15.0f, 10.0f, 10.0f), DragOne(Tool::Translate, Handle::Free, item, {0.0f, 0.0f}, {10.0f, 10.0f}, on, true)[0]);
    }

    TEST(GizmoDragTest, TranslateSnapAlongAScreenAxisRoundsOnlyThatCoordinate) {
        SnapSettings snap;
        snap.position = true;
        const Item item = MakeItem(3.0f, 5.0f, 10.0f, 10.0f);
        const auto result = DragOne(Tool::Translate, Handle::AxisX, item, {0.0f, 0.0f}, {10.0f, 7.0f}, snap);
        EXPECT_FLOAT_EQ(result[0].x, 16.0f);
        EXPECT_FLOAT_EQ(result[0].y, 5.0f);
        const auto vertical = DragOne(Tool::Translate, Handle::AxisY, item, {0.0f, 0.0f}, {10.0f, 7.0f}, snap);
        EXPECT_FLOAT_EQ(vertical[0].x, 3.0f);
        EXPECT_FLOAT_EQ(vertical[0].y, 16.0f);
    }

    TEST(GizmoDragTest, TranslateSnapAlongATurnedAxisRoundsTheDistanceTravelled) {
        SnapSettings snap;
        snap.position = true;
        const Item item = MakeItem(0.0f, 0.0f, 10.0f, 10.0f, 30.0f);
        const Vec2 axis{kThirtyDegreesCos, kThirtyDegreesSin};
        const auto result = DragOne(Tool::Translate, Handle::AxisX, item, {0.0f, 0.0f}, axis * 10.0f, snap, false, false, Space::Local);
        EXPECT_NEAR(result[0].x, axis.x * 8.0f, kTolerance);
        EXPECT_NEAR(result[0].y, axis.y * 8.0f, kTolerance);
    }

    TEST(GizmoDragTest, ComingBackToThePressGivesTheStartValues) {
        const Item items[] = {MakeItem(1.0f, 2.0f, 30.0f, 40.0f, 25.0f), MakeItem(50.0f, 60.0f, 30.0f, 40.0f)};
        GizmoDrag drag;
        drag.Begin(Tool::Translate, Space::World, Handle::Free, items, {7.0f, 9.0f}, {});
        [[maybe_unused]] const auto away = drag.Update({70.0f, -90.0f}, false);
        const auto back = drag.Update({7.0f, 9.0f}, false);
        ExpectItem(items[0], back[0]);
        ExpectItem(items[1], back[1]);
    }

    TEST(GizmoDragTest, RotateOneItemChangesOnlyItsRotation) {
        const auto result = DragOne(Tool::Rotate, Handle::Ring, kFlat, {150.0f, 25.0f}, {50.0f, 125.0f});
        EXPECT_EQ(result[0].x, kFlat.x);
        EXPECT_EQ(result[0].y, kFlat.y);
        EXPECT_EQ(result[0].width, kFlat.width);
        EXPECT_NEAR(result[0].rotation, 90.0f, kTolerance);
    }

    TEST(GizmoDragTest, RotateIsMeasuredFromThePressSoThereIsNoJump) {
        Item item = kFlat;
        item.rotation = 10.0f;
        // Pressed at 45 degrees off the item's own angle: no movement yet, no change
        const Vec2 press = OnCircle(kFlatCenter, 60.0f, 45.0f);
        EXPECT_EQ(DragOne(Tool::Rotate, Handle::Ring, item, press, press)[0].rotation, 10.0f);
        const auto swept = DragOne(Tool::Rotate, Handle::Ring, item, press, OnCircle(kFlatCenter, 90.0f, 75.0f));
        EXPECT_NEAR(swept[0].rotation, 40.0f, kTolerance);
        const auto back = DragOne(Tool::Rotate, Handle::Ring, item, press, OnCircle(kFlatCenter, 90.0f, 15.0f));
        EXPECT_NEAR(back[0].rotation, -20.0f, kTolerance);
    }

    TEST(GizmoDragTest, RotateTurnsAboutTheOwnPivotOfAnItem) {
        const Item item = MakeItem(0.0f, 0.0f, 100.0f, 50.0f, 0.0f, 0.0f, 0.0f);
        // The pivot is the top left corner (0, 0): a sweep is measured around it
        const auto result = DragOne(Tool::Rotate, Handle::Ring, item, {60.0f, 0.0f}, {0.0f, 60.0f});
        EXPECT_NEAR(result[0].rotation, 90.0f, kTolerance);
        EXPECT_EQ(result[0].x, 0.0f);
        EXPECT_EQ(result[0].y, 0.0f);
    }

    TEST(GizmoDragTest, RotateWrapsPastAFullTurn) {
        Item item = kFlat;
        item.rotation = 350.0f;
        const auto result = DragOne(Tool::Rotate, Handle::Ring, item, OnCircle(kFlatCenter, 60.0f, 0.0f), OnCircle(kFlatCenter, 60.0f, 20.0f));
        EXPECT_NEAR(result[0].rotation, 10.0f, kTolerance);
        Item back = kFlat;
        back.rotation = 10.0f;
        const auto negative = DragOne(Tool::Rotate, Handle::Ring, back, OnCircle(kFlatCenter, 60.0f, 0.0f), OnCircle(kFlatCenter, 60.0f, -20.0f));
        EXPECT_NEAR(negative[0].rotation, -10.0f, kTolerance);
    }

    TEST(GizmoDragTest, RotateTakesTheShortWayAcrossTheBranchCut) {
        // From 170 to -170 degrees on the circle is a sweep of +20, not -340
        const auto result = DragOne(Tool::Rotate, Handle::Ring, kFlat, OnCircle(kFlatCenter, 60.0f, 170.0f), OnCircle(kFlatCenter, 60.0f, 190.0f));
        EXPECT_NEAR(result[0].rotation, 20.0f, kTolerance);
    }

    TEST(GizmoDragTest, RotateSnapRoundsTheSweepToTheStep) {
        SnapSettings snap;
        snap.angle = true;
        const Vec2 press = OnCircle(kFlatCenter, 60.0f, 0.0f);
        EXPECT_NEAR(DragOne(Tool::Rotate, Handle::Ring, kFlat, press, OnCircle(kFlatCenter, 60.0f, 22.0f), snap)[0].rotation, 15.0f, kTolerance);
        EXPECT_NEAR(DragOne(Tool::Rotate, Handle::Ring, kFlat, press, OnCircle(kFlatCenter, 60.0f, 23.0f), snap)[0].rotation, 30.0f, kTolerance);
        EXPECT_NEAR(DragOne(Tool::Rotate, Handle::Ring, kFlat, press, OnCircle(kFlatCenter, 60.0f, -22.0f), snap)[0].rotation, -15.0f, kTolerance);
        // Off, and the modifier flips it
        EXPECT_NEAR(DragOne(Tool::Rotate, Handle::Ring, kFlat, press, OnCircle(kFlatCenter, 60.0f, 22.0f))[0].rotation, 22.0f, kTolerance);
        EXPECT_NEAR(DragOne(Tool::Rotate, Handle::Ring, kFlat, press, OnCircle(kFlatCenter, 60.0f, 22.0f), {}, true)[0].rotation, 15.0f, kTolerance);
        EXPECT_NEAR(DragOne(Tool::Rotate, Handle::Ring, kFlat, press, OnCircle(kFlatCenter, 60.0f, 22.0f), snap, true)[0].rotation, 22.0f, kTolerance);
        // A snapped sweep leaves the item that starts off the step off it too
        Item odd = kFlat;
        odd.rotation = 7.0f;
        EXPECT_NEAR(DragOne(Tool::Rotate, Handle::Ring, odd, press, OnCircle(kFlatCenter, 60.0f, 22.0f), snap)[0].rotation, 22.0f, kTolerance);
    }

    TEST(GizmoDragTest, RotateAGroupTurnsEveryItemAboutTheCentreOfTheirBounds) {
        // Bounds are (0, 0, 40, 10), the centre (20, 5); the items sit 15 to its left and right
        const Item items[] = {MakeItem(0.0f, 0.0f, 10.0f, 10.0f), MakeItem(30.0f, 0.0f, 10.0f, 10.0f)};
        const Vec2 centre{20.0f, 5.0f};
        const auto result = Drag(Tool::Rotate, Space::World, Handle::Ring, items, centre + Vec2{50.0f, 0.0f}, centre + Vec2{0.0f, 50.0f});
        ASSERT_EQ(result.size(), 2u);
        // A quarter turn clockwise sends the left item to the top and the right item to the bottom
        ExpectItem(MakeItem(15.0f, -15.0f, 10.0f, 10.0f, 90.0f), result[0]);
        ExpectItem(MakeItem(15.0f, 15.0f, 10.0f, 10.0f, 90.0f), result[1]);
    }

    TEST(GizmoDragTest, RotateAGroupKeepsEachItemsPivotOnItsCircle) {
        const Item items[] = {MakeItem(0.0f, 0.0f, 10.0f, 20.0f, 20.0f, 0.25f, 0.75f), MakeItem(60.0f, 30.0f, 30.0f, 10.0f, -5.0f, 0.0f, 0.5f)};
        const Vec2 centre = Pivot(items);
        const Vec2 press = centre + Vec2{80.0f, 0.0f};
        const auto result = Drag(Tool::Rotate, Space::World, Handle::Ring, items, press, OnCircle(centre, 80.0f, 33.0f));
        for (size_t i = 0; i < 2; ++i) {
            const Vec2 expected = TurnAbout(PivotPoint(items[i]), centre, 33.0f);
            ExpectVec(expected, PivotPoint(result[i]));
            EXPECT_NEAR(result[i].rotation, items[i].rotation + 33.0f, kTolerance);
            EXPECT_FLOAT_EQ(result[i].width, items[i].width);
        }
    }

    TEST(GizmoDragTest, ScaleEachHandleOfAnUnturnedItemMovesItsSidesAndKeepsTheOppositeOnes) {
        const Vec2 pull{10.0f, 6.0f};
        const auto drag = [&](Handle handle) { return DragOne(Tool::Scale, handle, kFlat, {0.0f, 0.0f}, pull)[0]; };
        ExpectItem(MakeItem(0.0f, 0.0f, 110.0f, 56.0f), drag(Handle::Corner2));
        ExpectItem(MakeItem(10.0f, 6.0f, 90.0f, 44.0f), drag(Handle::Corner0));
        ExpectItem(MakeItem(0.0f, 6.0f, 110.0f, 44.0f), drag(Handle::Corner1));
        ExpectItem(MakeItem(10.0f, 0.0f, 90.0f, 56.0f), drag(Handle::Corner3));
        ExpectItem(MakeItem(0.0f, 6.0f, 100.0f, 44.0f), drag(Handle::Edge0));
        ExpectItem(MakeItem(0.0f, 0.0f, 110.0f, 50.0f), drag(Handle::Edge1));
        ExpectItem(MakeItem(0.0f, 0.0f, 100.0f, 56.0f), drag(Handle::Edge2));
        ExpectItem(MakeItem(10.0f, 0.0f, 90.0f, 50.0f), drag(Handle::Edge3));
    }

    TEST(GizmoDragTest, ScaleKeepsTheFixedPointOfATurnedItemWithAnOffCentrePivot) {
        const Item items[] = {kFlat, MakeItem(30.0f, 40.0f, 80.0f, 60.0f, 30.0f), MakeItem(-20.0f, 10.0f, 80.0f, 60.0f, 90.0f, 0.25f, 0.75f), MakeItem(5.0f, 5.0f, 40.0f, 70.0f, -50.0f, 1.0f, 0.0f)};
        const Vec2 pull{13.0f, -9.0f};
        for (const Item& item : items) {
            for (const Handle handle : kScaleHandles) {
                const Vec2 sides = SidesOf(handle);
                const Vec2 anchor = HandleAnchor(handle);
                const Vec2 press = WorldPointOf(item, anchor.x, anchor.y);
                const auto result = DragOne(Tool::Scale, handle, item, press, press + pull);
                const Item& scaled = result[0];
                // The point opposite the handle (the middle of an axis the handle does not move) is where it was
                const float fixedU = sides.x == 0.0f ? 0.5f : (sides.x > 0.0f ? 0.0f : 1.0f);
                const float fixedV = sides.y == 0.0f ? 0.5f : (sides.y > 0.0f ? 0.0f : 1.0f);
                ExpectVec(WorldPointOf(item, fixedU, fixedV), WorldPointOf(scaled, fixedU, fixedV));
                // The handle itself follows the pointer's travel along the item's own axes
                const Vec2 local = TurnAbout(pull, {}, -item.rotation);
                EXPECT_NEAR(scaled.width, item.width + sides.x * local.x, kTolerance) << "handle " << static_cast<int>(handle);
                EXPECT_NEAR(scaled.height, item.height + sides.y * local.y, kTolerance) << "handle " << static_cast<int>(handle);
                EXPECT_FLOAT_EQ(scaled.rotation, item.rotation);
                EXPECT_FLOAT_EQ(scaled.pivotX, item.pivotX);
                EXPECT_FLOAT_EQ(scaled.pivotY, item.pivotY);
            }
        }
    }

    TEST(GizmoDragTest, ScaleNeverGoesBelowTheMinimumOrFlips) {
        const auto result = DragOne(Tool::Scale, Handle::Corner2, kFlat, {100.0f, 50.0f}, {-1000.0f, -1000.0f});
        EXPECT_FLOAT_EQ(result[0].width, kMinItemSize);
        EXPECT_FLOAT_EQ(result[0].height, kMinItemSize);
        // The corner that was opposite stays at (0, 0)
        ExpectVec({0.0f, 0.0f}, WorldPointOf(result[0], 0.0f, 0.0f));
        const auto edge = DragOne(Tool::Scale, Handle::Edge3, kFlat, {0.0f, 25.0f}, {500.0f, 25.0f});
        EXPECT_FLOAT_EQ(edge[0].width, kMinItemSize);
        ExpectVec({100.0f, 0.0f}, WorldPointOf(edge[0], 1.0f, 0.0f));
    }

    TEST(GizmoDragTest, ScaleAnEdgeLeavesTheUnmovedAxisAloneEvenUnderTheMinimum) {
        const float thin = kMinItemSize * 0.5f;
        const auto result = DragOne(Tool::Scale, Handle::Edge3, MakeItem(0.0f, 0.0f, 100.0f, thin), {0.0f, thin * 0.5f}, {-50.0f, thin * 0.5f});
        EXPECT_NEAR(result[0].width, 150.0f, kTolerance);
        EXPECT_FLOAT_EQ(result[0].height, thin);
    }

    TEST(GizmoDragTest, ScaleKeepAspectMakesTheChangeUniform) {
        const Item item = kFlat;
        // The corner moves 20 right and not at all down: the wider change decides, 120 x 60
        auto result = DragOne(Tool::Scale, Handle::Corner2, item, {100.0f, 50.0f}, {120.0f, 50.0f}, {}, false, true);
        ExpectItem(MakeItem(0.0f, 0.0f, 120.0f, 60.0f), result[0]);
        // Pulled up by half the height: the change in y decides, 50 x 25
        result = DragOne(Tool::Scale, Handle::Corner2, item, {100.0f, 50.0f}, {100.0f, 25.0f}, {}, false, true);
        ExpectItem(MakeItem(0.0f, 0.0f, 50.0f, 25.0f), result[0]);
        // An edge scales the other axis about its middle
        result = DragOne(Tool::Scale, Handle::Edge1, item, {100.0f, 25.0f}, {120.0f, 25.0f}, {}, false, true);
        ExpectItem(MakeItem(0.0f, -5.0f, 120.0f, 60.0f), result[0]);
        result = DragOne(Tool::Scale, Handle::Edge2, item, {50.0f, 50.0f}, {50.0f, 75.0f}, {}, false, true);
        ExpectItem(MakeItem(-25.0f, 0.0f, 150.0f, 75.0f), result[0]);
    }

    TEST(GizmoDragTest, ScaleKeepAspectOfATurnedItemKeepsTheOppositeCorner) {
        const Item item = MakeItem(30.0f, 40.0f, 80.0f, 60.0f, 30.0f);
        const Vec2 press = WorldPointOf(item, 1.0f, 1.0f);
        const auto result = DragOne(Tool::Scale, Handle::Corner2, item, press, press + Vec2{20.0f, 20.0f}, {}, false, true);
        EXPECT_NEAR(result[0].width / result[0].height, item.width / item.height, kTolerance);
        ExpectVec(WorldPointOf(item, 0.0f, 0.0f), WorldPointOf(result[0], 0.0f, 0.0f));
    }

    TEST(GizmoDragTest, ScaleKeepAspectHoldsTheMinimumOnBothSides) {
        const auto result = DragOne(Tool::Scale, Handle::Corner2, kFlat, {100.0f, 50.0f}, {-1000.0f, -1000.0f}, {}, false, true);
        EXPECT_GE(result[0].width, kMinItemSize);
        EXPECT_GE(result[0].height, kMinItemSize);
        EXPECT_NEAR(result[0].width / result[0].height, 2.0f, kTolerance);
    }

    TEST(GizmoDragTest, ScaleSnapRoundsTheFactorOnTheStartSize) {
        SnapSettings snap;
        snap.scale = true;
        // 123.4 / 100 is 1.234, which rounds to 1.2
        auto result = DragOne(Tool::Scale, Handle::Edge1, kFlat, {100.0f, 25.0f}, {123.4f, 25.0f}, snap);
        EXPECT_NEAR(result[0].width, 120.0f, kTolerance);
        EXPECT_NEAR(result[0].height, 50.0f, kTolerance);
        // A corner snaps each axis: x by 1.234 to 1.2, y by 1.07 to 1.1
        result = DragOne(Tool::Scale, Handle::Corner2, kFlat, {100.0f, 50.0f}, {123.4f, 53.5f}, snap);
        EXPECT_NEAR(result[0].width, 120.0f, kTolerance);
        EXPECT_NEAR(result[0].height, 55.0f, kTolerance);
        // Off by the modifier, on by it when the switch is off
        result = DragOne(Tool::Scale, Handle::Edge1, kFlat, {100.0f, 25.0f}, {123.4f, 25.0f}, snap, true);
        EXPECT_NEAR(result[0].width, 123.4f, kTolerance);
        result = DragOne(Tool::Scale, Handle::Edge1, kFlat, {100.0f, 25.0f}, {123.4f, 25.0f}, {}, true);
        EXPECT_NEAR(result[0].width, 120.0f, kTolerance);
        // With the aspect locked the one factor snaps and both sides follow
        result = DragOne(Tool::Scale, Handle::Corner2, kFlat, {100.0f, 50.0f}, {123.4f, 50.0f}, snap, false, true);
        EXPECT_NEAR(result[0].width, 120.0f, kTolerance);
        EXPECT_NEAR(result[0].height, 60.0f, kTolerance);
    }

    TEST(GizmoDragTest, ScaleSnapNeverSnapsBelowTheMinimum) {
        SnapSettings snap;
        snap.scale = true;
        const auto result = DragOne(Tool::Scale, Handle::Edge1, kFlat, {100.0f, 25.0f}, {1.0f, 25.0f}, snap);
        EXPECT_FLOAT_EQ(result[0].width, kMinItemSize);
    }

    TEST(GizmoDragTest, ScaleAGroupIsUniformAboutTheCentreOfItsBounds) {
        // Bounds (0, 0, 40, 10), the centre (20, 5); pressed 50 to the right of it, pulled to 100
        const Item items[] = {MakeItem(0.0f, 0.0f, 10.0f, 10.0f), MakeItem(30.0f, 0.0f, 10.0f, 10.0f)};
        const Vec2 centre{20.0f, 5.0f};
        const auto result = Drag(Tool::Scale, Space::World, Handle::Corner2, items, centre + Vec2{50.0f, 0.0f}, centre + Vec2{0.0f, 100.0f});
        ASSERT_EQ(result.size(), 2u);
        // Factor 2: each item's pivot is twice as far from the centre, and each side is twice as long
        ExpectItem(MakeItem(-20.0f, -5.0f, 20.0f, 20.0f), result[0]);
        ExpectItem(MakeItem(40.0f, -5.0f, 20.0f, 20.0f), result[1]);
        ExpectVec(centre, Pivot(result));
    }

    TEST(GizmoDragTest, ScaleAGroupKeepsRotationsAndOffCentrePivots) {
        const Item items[] = {MakeItem(0.0f, 0.0f, 10.0f, 20.0f, 20.0f, 0.25f, 0.75f), MakeItem(60.0f, 30.0f, 30.0f, 10.0f, -5.0f, 0.0f, 0.5f)};
        const Vec2 centre = Pivot(items);
        const auto result = Drag(Tool::Scale, Space::World, Handle::Corner1, items, centre + Vec2{40.0f, 0.0f}, centre + Vec2{0.0f, 60.0f});
        for (size_t i = 0; i < 2; ++i) {
            ExpectVec(centre + (PivotPoint(items[i]) - centre) * 1.5f, PivotPoint(result[i]));
            EXPECT_NEAR(result[i].width, items[i].width * 1.5f, kTolerance);
            EXPECT_NEAR(result[i].height, items[i].height * 1.5f, kTolerance);
            EXPECT_FLOAT_EQ(result[i].rotation, items[i].rotation);
        }
    }

    TEST(GizmoDragTest, ScaleAGroupSnapsAndHoldsTheMinimum) {
        const Item items[] = {MakeItem(0.0f, 0.0f, 10.0f, 10.0f), MakeItem(30.0f, 0.0f, 10.0f, 10.0f)};
        const Vec2 centre{20.0f, 5.0f};
        SnapSettings snap;
        snap.scale = true;
        // 1.234 snaps to 1.2
        const auto snapped = Drag(Tool::Scale, Space::World, Handle::Corner2, items, centre + Vec2{50.0f, 0.0f}, centre + Vec2{61.7f, 0.0f}, snap);
        EXPECT_NEAR(snapped[0].width, 12.0f, kTolerance);
        // Pulled onto the centre: the smallest item stays at the minimum size
        const auto squeezed = Drag(Tool::Scale, Space::World, Handle::Corner2, items, centre + Vec2{50.0f, 0.0f}, centre);
        EXPECT_NEAR(squeezed[0].width, kMinItemSize, kTolerance);
        EXPECT_NEAR(squeezed[1].height, kMinItemSize, kTolerance);
    }

    TEST(GizmoDragTest, ScaleEdgeHandlesOfAGroupActAsCorners) {
        const Item items[] = {MakeItem(0.0f, 0.0f, 10.0f, 10.0f), MakeItem(30.0f, 0.0f, 10.0f, 10.0f)};
        const Vec2 centre{20.0f, 5.0f};
        const auto result = Drag(Tool::Scale, Space::World, Handle::Edge1, items, centre + Vec2{20.0f, 0.0f}, centre + Vec2{40.0f, 0.0f});
        EXPECT_NEAR(result[0].width, 20.0f, kTolerance);
        EXPECT_NEAR(result[0].height, 20.0f, kTolerance);
    }

    TEST(GizmoDragTest, ACancelGivesBackTheSnapshotExactlyAndEndsTheDrag) {
        const Item items[] = {MakeItem(1.1f, 2.2f, 33.3f, 44.4f, 12.5f, 0.3f, 0.7f), MakeItem(5.5f, 6.6f, 7.7f, 8.8f)};
        GizmoDrag drag;
        drag.Begin(Tool::Scale, Space::World, Handle::Corner2, items, {0.0f, 0.0f}, SnapAll());
        [[maybe_unused]] const auto moved = drag.Update({17.0f, 31.0f}, false);
        EXPECT_NE(drag.Current()[0], items[0]);
        const auto snapshot = drag.Cancel();
        ASSERT_EQ(snapshot.size(), 2u);
        EXPECT_EQ(snapshot[0], items[0]);
        EXPECT_EQ(snapshot[1], items[1]);
        EXPECT_FALSE(drag.IsActive());
        EXPECT_TRUE(drag.Update({50.0f, 50.0f}, false).empty());
        EXPECT_EQ(drag.Current()[0], items[0]);
    }

    TEST(GizmoDragTest, EndKeepsTheLastResultAndEndsTheDrag) {
        const Item items[] = {kFlat};
        GizmoDrag drag;
        drag.Begin(Tool::Translate, Space::World, Handle::Free, items, {0.0f, 0.0f}, {});
        [[maybe_unused]] const auto moved = drag.Update({10.0f, 20.0f}, false);
        drag.End();
        EXPECT_FALSE(drag.IsActive());
        EXPECT_FLOAT_EQ(drag.Current()[0].x, 10.0f);
        EXPECT_FLOAT_EQ(drag.Start()[0].x, 0.0f);
    }

    TEST(GizmoDragTest, ASteadyDragKeepsItsStorage) {
        const Item items[] = {kFlat, MakeItem(10.0f, 10.0f, 5.0f, 5.0f), MakeItem(20.0f, 20.0f, 5.0f, 5.0f)};
        GizmoDrag drag;
        drag.Begin(Tool::Translate, Space::World, Handle::Free, items, {0.0f, 0.0f}, {});
        const Item* first = drag.Update({1.0f, 1.0f}, false).data();
        for (int i = 2; i < 20; ++i) {
            EXPECT_EQ(drag.Update({static_cast<float>(i), 1.0f}, false).data(), first);
        }
    }

    TEST(GizmoDragTest, AHandleOfAnotherToolChangesNothing) {
        const auto result = DragOne(Tool::Scale, Handle::Ring, kFlat, {0.0f, 0.0f}, {10.0f, 10.0f});
        EXPECT_EQ(result[0], kFlat);
        const auto rotate = DragOne(Tool::Rotate, Handle::AxisX, kFlat, {0.0f, 0.0f}, {10.0f, 10.0f});
        EXPECT_EQ(rotate[0], kFlat);
        const auto translate = DragOne(Tool::Translate, Handle::Corner0, kFlat, {0.0f, 0.0f}, {10.0f, 10.0f});
        EXPECT_EQ(translate[0], kFlat);
    }

}  // namespace BECore::Tests
