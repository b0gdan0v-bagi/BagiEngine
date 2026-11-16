#include <BECore/UI/UIAnchorPresets.h>
#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIElementDrag.h>
#include <BECore/UI/UILayout.h>
#include <BECore/UI/UIRectEdit.h>
#include <BECore/UI/UIVariant.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;
        constexpr float kHandle = 10.0f;

        void ExpectRect(const Rect& actual, const Rect& expected) {
            EXPECT_NEAR(actual.x, expected.x, kTolerance);
            EXPECT_NEAR(actual.y, expected.y, kTolerance);
            EXPECT_NEAR(actual.w, expected.w, kTolerance);
            EXPECT_NEAR(actual.h, expected.h, kTolerance);
        }

        UILayoutContext Output(float width, float height) {
            UILayoutContext context;
            context.outputWidth = width;
            context.outputHeight = height;
            return context;
        }

        IntrusivePtr<UIElement> AddBox(UIElement& parent, const char* name, float x, float y, float width, float height) {
            auto box = parent.AddChild(PoolString::Intern(name));
            box->style.rect.x = UILength::Px(x);
            box->style.rect.y = UILength::Px(y);
            box->style.rect.width = UILength::Px(width);
            box->style.rect.height = UILength::Px(height);
            return box;
        }

        /// A canvas of 800 x 600 units (scale 1, so output pixels are canvas units) with a panel and a child in it
        struct DragFixture {
            UICanvasLayout canvas;
            IntrusivePtr<UIElement> panel;
            IntrusivePtr<UIElement> child;

            DragFixture() {
                canvas.params.scaleMode = CanvasScaleMode::ConstantPixelSize;
                panel = AddBox(*canvas.root, "Panel", 100.0f, 50.0f, 400.0f, 300.0f);
                child = AddBox(*panel, "Child", 20.0f, 10.0f, 100.0f, 60.0f);
                canvas.Update(Output(800.0f, 600.0f));
            }

            void Relayout() {
                canvas.Invalidate();
                canvas.Update(Output(800.0f, 600.0f));
            }
        };

        Vec2 CenterOf(const Rect& rect) {
            return {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
        }

    }  // namespace

    // ---- the box ---------------------------------------------------------------------------------------------------------

    TEST(UIElementDragTest, TheBoxOfAChildOfTheRootIsTheContentBounds) {
        DragFixture f;
        const Optional<Rect> box = UIElementDrag::BoxOf(f.canvas, *f.panel);
        ASSERT_TRUE(box.has_value());
        ExpectRect(*box, {0.0f, 0.0f, 800.0f, 600.0f});
    }

    TEST(UIElementDragTest, TheBoxOfAChildOfAPanelIsTheRectOfThePanel) {
        DragFixture f;
        const Optional<Rect> box = UIElementDrag::BoxOf(f.canvas, *f.child);
        ASSERT_TRUE(box.has_value());
        ExpectRect(*box, {100.0f, 50.0f, 400.0f, 300.0f});
    }

    TEST(UIElementDragTest, TheBoxIsTheRegionTheElementNames) {
        DragFixture f;
        f.canvas.regions.Set("table"_intern, {300.0f, 200.0f, 123.0f, 77.0f});
        f.child->style.rect.region = "table"_intern;
        f.Relayout();
        const Optional<Rect> box = UIElementDrag::BoxOf(f.canvas, *f.child);
        ASSERT_TRUE(box.has_value());
        ExpectRect(*box, {300.0f, 200.0f, 123.0f, 77.0f});

        f.child->style.rect.region = "unpublished"_intern;
        f.Relayout();
        ExpectRect(*UIElementDrag::BoxOf(f.canvas, *f.child), {100.0f, 50.0f, 400.0f, 300.0f});  // a region nobody published is no region
    }

    TEST(UIElementDragTest, AnIgnoreSafeAreaChildOfTheRootTakesTheWholeCanvas) {
        UICanvasLayout canvas;
        canvas.params.scaleMode = CanvasScaleMode::ConstantPixelSize;
        canvas.params.useSafeArea = true;
        auto inside = AddBox(*canvas.root, "Inside", 0.0f, 0.0f, 50.0f, 50.0f);
        auto bleeding = AddBox(*canvas.root, "Bleeding", 0.0f, 0.0f, 50.0f, 50.0f);
        bleeding->ignoreSafeArea = true;
        UILayoutContext context = Output(800.0f, 600.0f);
        context.safeArea = {0.0f, 40.0f, 800.0f, 520.0f};
        canvas.Update(context);

        ExpectRect(*UIElementDrag::BoxOf(canvas, *inside), {0.0f, 40.0f, 800.0f, 520.0f});
        ExpectRect(*UIElementDrag::BoxOf(canvas, *bleeding), {0.0f, 0.0f, 800.0f, 600.0f});
    }

    TEST(UIElementDragTest, TheBoxIsNoneForAnElementOutsideTheTreeOrAWithoutLayout) {
        DragFixture f;
        const IntrusivePtr<UIElement> stray = New<UIElement>();
        EXPECT_FALSE(UIElementDrag::BoxOf(f.canvas, *stray).has_value());

        UICanvasLayout fresh;
        auto lone = AddBox(*fresh.root, "Lone", 0.0f, 0.0f, 10.0f, 10.0f);
        EXPECT_FALSE(UIElementDrag::BoxOf(fresh, *lone).has_value());
    }

    TEST(UIElementDragTest, TheBoxOfAKnownParentReadsTheGivenRegionName) {
        DragFixture f;
        f.canvas.regions.Set("table"_intern, {1.0f, 2.0f, 3.0f, 4.0f});
        ExpectRect(UIElementDrag::BoxOf(f.canvas, *f.panel, *f.child, "table"_intern), {1.0f, 2.0f, 3.0f, 4.0f});
        ExpectRect(UIElementDrag::BoxOf(f.canvas, *f.panel, *f.child, PoolString{}), f.panel->rect);
    }

    // ---- who may drag ----------------------------------------------------------------------------------------------------

    TEST(UIElementDragTest, AnAnchoredVisibleElementCanBeDragged) {
        DragFixture f;
        EXPECT_EQ(UIElementDrag::CanDrag(f.canvas, *f.panel), CanDragReason::Ok);
        EXPECT_EQ(UIElementDrag::CanDrag(f.canvas, *f.child), CanDragReason::Ok);
    }

    TEST(UIElementDragTest, NoLayoutIsTheFirstReason) {
        UICanvasLayout fresh;
        auto lone = AddBox(*fresh.root, "Lone", 0.0f, 0.0f, 10.0f, 10.0f);
        EXPECT_EQ(UIElementDrag::CanDrag(fresh, *lone), CanDragReason::NoLayout);
    }

    TEST(UIElementDragTest, AHiddenElementOrOneInAHiddenParentOrWithoutAreaCannotBeDragged) {
        DragFixture f;
        f.child->style.visible = false;
        f.Relayout();
        EXPECT_EQ(UIElementDrag::CanDrag(f.canvas, *f.child), CanDragReason::Hidden);

        f.child->style.visible = true;
        f.panel->style.visible = false;
        f.Relayout();
        EXPECT_EQ(UIElementDrag::CanDrag(f.canvas, *f.child), CanDragReason::Hidden);

        f.panel->style.visible = true;
        f.child->style.rect.width = UILength::Px(0.0f);
        f.Relayout();
        EXPECT_EQ(UIElementDrag::CanDrag(f.canvas, *f.child), CanDragReason::Hidden);
    }

    TEST(UIElementDragTest, AChildOfAStackHasNoBox) {
        DragFixture f;
        f.panel->layout = UILayoutKind::Stack;
        f.Relayout();
        EXPECT_EQ(UIElementDrag::CanDrag(f.canvas, *f.child), CanDragReason::NoBox);
        EXPECT_EQ(UIElementDrag::CanDrag(f.canvas, *f.panel), CanDragReason::Ok);  // the stack itself is a child of the root
    }

    TEST(UIElementDragTest, AnElementOutsideTheTreeHasNoBox) {
        DragFixture f;
        const IntrusivePtr<UIElement> stray = New<UIElement>();
        stray->active.visible = true;
        stray->rect = {0.0f, 0.0f, 10.0f, 10.0f};
        EXPECT_EQ(UIElementDrag::CanDrag(f.canvas, *stray), CanDragReason::NoBox);
    }

    TEST(UIElementDragTest, AVariantThatOverridesTheRectBlocksTheDrag) {
        DragFixture f;
        UIVariant wide;
        UIElementOverride override;
        override.element = "Child"_intern;
        override.width = UILength::Px(300.0f);
        wide.overrides.push_back(override);
        f.canvas.variants.push_back(wide);
        f.Relayout();
        EXPECT_EQ(UIElementDrag::CanDrag(f.canvas, *f.child), CanDragReason::VariantOverrides);
        EXPECT_EQ(UIElementDrag::CanDrag(f.canvas, *f.panel), CanDragReason::Ok);  // a variant of another element does not matter
    }

    TEST(UIElementDragTest, EveryReasonHasAText) {
        for (const CanDragReason reason : {CanDragReason::NoLayout, CanDragReason::Hidden, CanDragReason::NoBox, CanDragReason::VariantOverrides}) {
            EXPECT_NE(String(UIElementDrag::ReasonText(reason)), "");
        }
        EXPECT_EQ(String(UIElementDrag::ReasonText(CanDragReason::Ok)), "");
    }

    // ---- the handles -----------------------------------------------------------------------------------------------------

    TEST(UIElementDragTest, TheHandlesSitOnTheCornersAndTheEdgeMiddles) {
        const Rect rect{100.0f, 100.0f, 200.0f, 100.0f};
        const UIHandleSet set = UIElementDrag::HandleRects(rect, kHandle);
        ASSERT_EQ(set.count, 8u);
        const auto find = [&](UIDragHandle handle) {
            for (const UIHandleRect& item : set.View()) {
                if (item.handle == handle) {
                    return CenterOf(item.rect);
                }
            }
            return Vec2{-1.0f, -1.0f};
        };
        EXPECT_EQ(find(UIDragHandle::TopLeft), (Vec2{100.0f, 100.0f}));
        EXPECT_EQ(find(UIDragHandle::BottomRight), (Vec2{300.0f, 200.0f}));
        EXPECT_EQ(find(UIDragHandle::Top), (Vec2{200.0f, 100.0f}));
        EXPECT_EQ(find(UIDragHandle::Left), (Vec2{100.0f, 150.0f}));
        for (const UIHandleRect& item : set.View()) {
            EXPECT_EQ(item.rect.w, kHandle);
            EXPECT_EQ(item.rect.h, kHandle);
        }
    }

    TEST(UIElementDragTest, HitHandleFindsCornersEdgesAndTheBody) {
        const Rect rect{100.0f, 100.0f, 200.0f, 100.0f};
        const struct {
            Vec2 point;
            UIDragHandle handle;
        } cases[] = {{{100.0f, 100.0f}, UIDragHandle::TopLeft}, {{297.0f, 97.0f}, UIDragHandle::TopRight}, {{300.0f, 200.0f}, UIDragHandle::BottomRight}, {{96.0f, 204.0f}, UIDragHandle::BottomLeft},
                     {{200.0f, 100.0f}, UIDragHandle::Top},     {{303.0f, 150.0f}, UIDragHandle::Right},   {{200.0f, 203.0f}, UIDragHandle::Bottom},      {{98.0f, 150.0f}, UIDragHandle::Left},
                     {{200.0f, 150.0f}, UIDragHandle::Body},    {{110.0f, 110.0f}, UIDragHandle::Body},    {{50.0f, 50.0f}, UIDragHandle::None},          {{200.0f, 300.0f}, UIDragHandle::None}};
        for (const auto& item : cases) {
            EXPECT_EQ(UIElementDrag::HitHandle(rect, item.point, kHandle), item.handle) << item.point.x << ", " << item.point.y;
        }
    }

    TEST(UIElementDragTest, AShortAxisDropsItsEdgeHandlesAndASmallRectKeepsOnlyItsBody) {
        // 25 wide is under three handles: no top or bottom handle; 100 high is enough for the side ones
        const Rect narrow{100.0f, 100.0f, 25.0f, 100.0f};
        const UIHandleSet set = UIElementDrag::HandleRects(narrow, kHandle);
        EXPECT_EQ(set.count, 6u);
        for (const UIHandleRect& item : set.View()) {
            EXPECT_NE(item.handle, UIDragHandle::Top);
            EXPECT_NE(item.handle, UIDragHandle::Bottom);
        }
        EXPECT_EQ(UIElementDrag::HitHandle(narrow, {112.0f, 100.0f}, kHandle), UIDragHandle::Body);  // where the top edge handle would be

        const Rect tiny{100.0f, 100.0f, 15.0f, 40.0f};
        EXPECT_EQ(UIElementDrag::HandleRects(tiny, kHandle).count, 0u);
        EXPECT_EQ(UIElementDrag::HitHandle(tiny, {100.0f, 100.0f}, kHandle), UIDragHandle::Body);
        EXPECT_EQ(UIElementDrag::HitHandle(tiny, {95.0f, 95.0f}, kHandle), UIDragHandle::None);
    }

    // ---- move and resize -------------------------------------------------------------------------------------------------

    TEST(UIElementDragTest, TheBodyMoves) {
        const Rect start{100.0f, 100.0f, 200.0f, 100.0f};
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::Body, {15.0f, -20.0f}), {115.0f, 80.0f, 200.0f, 100.0f});
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::None, {15.0f, -20.0f}), start);
    }

    TEST(UIElementDragTest, LockAxisKeepsTheAxisTheBodyTravelledMore) {
        const Rect start{100.0f, 100.0f, 200.0f, 100.0f};
        const UIResizeOptions lock{.lockAxis = true};
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::Body, {30.0f, 10.0f}, lock), {130.0f, 100.0f, 200.0f, 100.0f});
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::Body, {-5.0f, 40.0f}, lock), {100.0f, 140.0f, 200.0f, 100.0f});
    }

    TEST(UIElementDragTest, EachHandleMovesItsEdgesAndLeavesTheOthers) {
        const Rect start{100.0f, 100.0f, 200.0f, 100.0f};
        const Vec2 delta{10.0f, 20.0f};
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::TopLeft, delta), {110.0f, 120.0f, 190.0f, 80.0f});
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::Top, delta), {100.0f, 120.0f, 200.0f, 80.0f});
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::TopRight, delta), {100.0f, 120.0f, 210.0f, 80.0f});
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::Right, delta), {100.0f, 100.0f, 210.0f, 100.0f});
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::BottomRight, delta), {100.0f, 100.0f, 210.0f, 120.0f});
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::Bottom, delta), {100.0f, 100.0f, 200.0f, 120.0f});
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::BottomLeft, delta), {110.0f, 100.0f, 190.0f, 120.0f});
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::Left, delta), {110.0f, 100.0f, 190.0f, 100.0f});
    }

    TEST(UIElementDragTest, AResizeNeverGoesBelowTheMinimumOrFlips) {
        const Rect start{100.0f, 100.0f, 200.0f, 100.0f};
        const Rect pastRight = UIElementDrag::Resize(start, UIDragHandle::Left, {500.0f, 0.0f});
        ExpectRect(pastRight, {299.0f, 100.0f, UIElementDrag::kMinSize, 100.0f});
        const Rect pastTop = UIElementDrag::Resize(start, UIDragHandle::Bottom, {0.0f, -500.0f});
        ExpectRect(pastTop, {100.0f, 100.0f, 200.0f, UIElementDrag::kMinSize});
        const Rect corner = UIElementDrag::Resize(start, UIDragHandle::BottomRight, {-900.0f, -900.0f});
        ExpectRect(corner, {100.0f, 100.0f, UIElementDrag::kMinSize, UIElementDrag::kMinSize});
    }

    TEST(UIElementDragTest, KeepAspectScalesACornerUniformlyAboutTheOppositeCorner) {
        const Rect start{100.0f, 100.0f, 200.0f, 100.0f};
        const UIResizeOptions keep{.keepAspect = true};
        // The pointer changed X by 50 % and Y by 10 %: the X factor wins
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::BottomRight, {100.0f, 10.0f}, keep), {100.0f, 100.0f, 300.0f, 150.0f});
        // From the top left the opposite corner stays: the bottom right at 300, 200
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::TopLeft, {-100.0f, -10.0f}, keep), {0.0f, 50.0f, 300.0f, 150.0f});
        // Shrinking never passes the minimum size on the short side
        const Rect small = UIElementDrag::Resize(start, UIDragHandle::BottomRight, {-500.0f, -500.0f}, keep);
        EXPECT_NEAR(small.h, UIElementDrag::kMinSize, kTolerance);
        EXPECT_NEAR(small.w / small.h, 2.0f, kTolerance);
        // An edge handle ignores it
        ExpectRect(UIElementDrag::Resize(start, UIDragHandle::Right, {100.0f, 0.0f}, keep), {100.0f, 100.0f, 300.0f, 100.0f});
    }

    TEST(UIElementDragTest, ADragBackToTheStartGivesTheStartRect) {
        const Rect start{100.0f, 100.0f, 200.0f, 100.0f};
        for (const UIDragHandle handle : {UIDragHandle::Body, UIDragHandle::TopLeft, UIDragHandle::Right, UIDragHandle::BottomLeft}) {
            const Rect away = UIElementDrag::Resize(start, handle, {500.0f, -400.0f});
            EXPECT_NE(away.w * away.h + away.x, start.w * start.h + start.x);
            ExpectRect(UIElementDrag::Resize(start, handle, {0.0f, 0.0f}), start);
        }
    }

    // ---- snap ------------------------------------------------------------------------------------------------------------

    TEST(UIElementDragTest, TheSnapLinesAreTheEdgesAndTheCentreOfTheBox) {
        const UISnapLines lines = UIElementDrag::SnapLines({100.0f, 50.0f, 400.0f, 300.0f});
        EXPECT_EQ(lines.x, (Array<float, 3>{100.0f, 300.0f, 500.0f}));
        EXPECT_EQ(lines.y, (Array<float, 3>{50.0f, 200.0f, 350.0f}));
    }

    TEST(UIElementDragTest, TheBodySnapsItsLeftCentreOrRightEdgeToALine) {
        const UISnapLines lines = UIElementDrag::SnapLines({0.0f, 0.0f, 800.0f, 600.0f});  // x: 0, 400, 800; y: 0, 300, 600
        const float threshold = 6.0f;

        UISnapResult left = UIElementDrag::Snap({3.0f, 100.0f, 100.0f, 50.0f}, UIDragHandle::Body, lines, threshold);
        ExpectRect(left.rect, {0.0f, 100.0f, 100.0f, 50.0f});
        EXPECT_EQ(left.guideX, 0.0f);
        EXPECT_FALSE(left.guideY.has_value());

        UISnapResult centre = UIElementDrag::Snap({352.0f, 100.0f, 100.0f, 50.0f}, UIDragHandle::Body, lines, threshold);  // its centre is 402
        EXPECT_NEAR(centre.rect.x, 350.0f, kTolerance);
        EXPECT_EQ(centre.guideX, 400.0f);

        UISnapResult right = UIElementDrag::Snap({702.0f, 100.0f, 100.0f, 50.0f}, UIDragHandle::Body, lines, threshold);  // its right edge is 802
        EXPECT_NEAR(right.rect.x, 700.0f, kTolerance);
        EXPECT_EQ(right.guideX, 800.0f);

        UISnapResult vertical = UIElementDrag::Snap({100.0f, 275.0f, 100.0f, 50.0f}, UIDragHandle::Body, lines, threshold);  // its middle is 300
        EXPECT_NEAR(vertical.rect.y, 275.0f, kTolerance);
        EXPECT_EQ(vertical.guideY, 300.0f);

        UISnapResult bottom = UIElementDrag::Snap({100.0f, 548.0f, 100.0f, 50.0f}, UIDragHandle::Body, lines, threshold);  // its bottom is 598
        EXPECT_NEAR(bottom.rect.y, 550.0f, kTolerance);
        EXPECT_EQ(bottom.guideY, 600.0f);
    }

    TEST(UIElementDragTest, NothingSnapsPastTheThreshold) {
        const UISnapLines lines = UIElementDrag::SnapLines({0.0f, 0.0f, 800.0f, 600.0f});
        const Rect wanted{20.0f, 80.0f, 100.0f, 50.0f};
        for (const UIDragHandle handle : {UIDragHandle::Body, UIDragHandle::TopLeft, UIDragHandle::Right, UIDragHandle::BottomRight}) {
            const UISnapResult result = UIElementDrag::Snap(wanted, handle, lines, 6.0f);
            ExpectRect(result.rect, wanted);
            EXPECT_FALSE(result.guideX.has_value());
            EXPECT_FALSE(result.guideY.has_value());
        }
    }

    TEST(UIElementDragTest, AResizeSnapsTheEdgesItMovesAndKeepsTheOthers) {
        const UISnapLines lines = UIElementDrag::SnapLines({0.0f, 0.0f, 800.0f, 600.0f});

        // The left edge at 4 goes to 0, the right edge (104) stays
        const UISnapResult left = UIElementDrag::Snap({4.0f, 100.0f, 100.0f, 50.0f}, UIDragHandle::Left, lines, 6.0f);
        ExpectRect(left.rect, {0.0f, 100.0f, 104.0f, 50.0f});
        EXPECT_EQ(left.guideX, 0.0f);
        EXPECT_FALSE(left.guideY.has_value());

        // The right edge at 397 goes to 400 and the left edge stays
        const UISnapResult right = UIElementDrag::Snap({297.0f, 100.0f, 100.0f, 50.0f}, UIDragHandle::Right, lines, 6.0f);
        ExpectRect(right.rect, {297.0f, 100.0f, 103.0f, 50.0f});
        EXPECT_EQ(right.guideX, 400.0f);

        // A corner snaps both of its edges: the bottom right to (800, 300)
        const UISnapResult corner = UIElementDrag::Snap({600.0f, 200.0f, 197.0f, 102.0f}, UIDragHandle::BottomRight, lines, 6.0f);
        ExpectRect(corner.rect, {600.0f, 200.0f, 200.0f, 100.0f});
        EXPECT_EQ(corner.guideX, 800.0f);
        EXPECT_EQ(corner.guideY, 300.0f);

        // The top edge does not look at the X lines, and an edge handle snaps one axis only
        const UISnapResult top = UIElementDrag::Snap({5.0f, 4.0f, 100.0f, 50.0f}, UIDragHandle::Top, lines, 6.0f);
        ExpectRect(top.rect, {5.0f, 0.0f, 100.0f, 54.0f});
        EXPECT_FALSE(top.guideX.has_value());
    }

    TEST(UIElementDragTest, ASnapNeverShrinksTheRectBelowTheMinimum) {
        const UISnapLines lines = UIElementDrag::SnapLines({0.0f, 0.0f, 800.0f, 600.0f});
        // The left edge at 396 would go to 400 but the right edge is at 397
        const UISnapResult result = UIElementDrag::Snap({396.0f, 100.0f, 1.0f, 50.0f}, UIDragHandle::Left, lines, 6.0f);
        ExpectRect(result.rect, {396.0f, 100.0f, 1.0f, 50.0f});
        EXPECT_FALSE(result.guideX.has_value());
    }

    // ---- to a spec -------------------------------------------------------------------------------------------------------

    TEST(UIElementDragTest, ADraggedPixelElementStaysInPixelsAndLaysOutAtTheDraggedRect) {
        DragFixture f;
        const Rect box = *UIElementDrag::BoxOf(f.canvas, *f.child);
        const Rect start = f.child->rect;
        for (const UIDragHandle handle : {UIDragHandle::Body, UIDragHandle::BottomRight, UIDragHandle::Left, UIDragHandle::TopLeft}) {
            const Rect wanted = UIElementDrag::Resize(start, handle, {17.0f, -9.0f});
            const UIRectEditResult result = UIRectEdit::SpecForRect(box, f.child->style.rect, wanted);
            EXPECT_TRUE(result.exact);
            EXPECT_EQ(result.spec.x.unit, UIUnit::Pixels);
            EXPECT_EQ(result.spec.width.unit, UIUnit::Pixels);
            ExpectRect(UILayout::ResolveRect(box, result.spec), wanted);
        }
    }

    TEST(UIElementDragTest, ADraggedPercentElementStaysInPercent) {
        DragFixture f;
        f.child->style.rect.x = UILength::Percent(10.0f);
        f.child->style.rect.width = UILength::Percent(25.0f);
        f.Relayout();
        const Rect box = *UIElementDrag::BoxOf(f.canvas, *f.child);
        const Rect wanted = UIElementDrag::Resize(f.child->rect, UIDragHandle::Right, {40.0f, 0.0f});

        const UIRectEditResult result = UIRectEdit::SpecForRect(box, f.child->style.rect, wanted);

        EXPECT_EQ(result.spec.width.unit, UIUnit::Percent);
        EXPECT_EQ(result.spec.x.unit, UIUnit::Percent);
        EXPECT_NEAR(result.spec.width.value, (100.0f + 40.0f) / 400.0f * 100.0f, 1e-2f);
        ExpectRect(UILayout::ResolveRect(box, result.spec), wanted);
    }

    // ---- the anchor widget -----------------------------------------------------------------------------------------------

    namespace {

        AnchorMark MarkOf(const Rect& box, const Vec2& anchorMin, const Vec2& anchorMax) {
            AnchorMark mark;
            mark.box = box;
            mark.anchors = {box.x + anchorMin.x * box.w, box.y + anchorMin.y * box.h, (anchorMax.x - anchorMin.x) * box.w, (anchorMax.y - anchorMin.y) * box.h};
            return mark;
        }

    }  // namespace

    TEST(UIElementDragTest, TheFractionOfAPointIsRelativeToTheBox) {
        const AnchorMark mark = MarkOf({100.0f, 50.0f, 400.0f, 200.0f}, {0.0f, 0.0f}, {1.0f, 1.0f});
        const Optional<Vec2> fraction = UIElementDrag::AnchorFraction(mark, {300.0f, 100.0f});
        ASSERT_TRUE(fraction.has_value());
        EXPECT_NEAR(fraction->x, 0.5f, kTolerance);
        EXPECT_NEAR(fraction->y, 0.25f, kTolerance);
        EXPECT_FALSE(UIElementDrag::AnchorFraction(MarkOf({0.0f, 0.0f, 0.0f, 200.0f}, {0.0f, 0.0f}, {1.0f, 1.0f}), {1.0f, 1.0f}).has_value());
        EXPECT_FALSE(UIElementDrag::AnchorFraction(MarkOf({0.0f, 0.0f, 100.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f}), {1.0f, 1.0f}).has_value());
    }

    TEST(UIElementDragTest, AFourPetalWidgetHitsEachCornerAndMissesBetween) {
        const AnchorMark mark = MarkOf({0.0f, 0.0f, 400.0f, 200.0f}, {0.25f, 0.25f}, {0.75f, 0.75f});  // corners at (100, 50) (300, 50) (100, 150) (300, 150)
        EXPECT_EQ(UIElementDrag::HitAnchorCorner(mark, {102.0f, 52.0f}, 8.0f), AnchorCorner::TopLeft);
        EXPECT_EQ(UIElementDrag::HitAnchorCorner(mark, {298.0f, 52.0f}, 8.0f), AnchorCorner::TopRight);
        EXPECT_EQ(UIElementDrag::HitAnchorCorner(mark, {98.0f, 148.0f}, 8.0f), AnchorCorner::BottomLeft);
        EXPECT_EQ(UIElementDrag::HitAnchorCorner(mark, {305.0f, 150.0f}, 8.0f), AnchorCorner::BottomRight);
        EXPECT_FALSE(UIElementDrag::HitAnchorCorner(mark, {200.0f, 100.0f}, 8.0f).has_value());
        EXPECT_FALSE(UIElementDrag::HitAnchorCorner(mark, {112.0f, 50.0f}, 8.0f).has_value());
    }

    TEST(UIElementDragTest, TheNearestCornerWinsWhereTwoReachThePointer) {
        const AnchorMark mark = MarkOf({0.0f, 0.0f, 400.0f, 200.0f}, {0.5f, 0.5f}, {0.55f, 0.5f});         // two corners 20 apart on a point axis of Y
        EXPECT_EQ(UIElementDrag::HitAnchorCorner(mark, {208.0f, 100.0f}, 30.0f), AnchorCorner::TopLeft);   // closer to x = 200
        EXPECT_EQ(UIElementDrag::HitAnchorCorner(mark, {214.0f, 100.0f}, 30.0f), AnchorCorner::TopRight);  // closer to x = 220
    }

    TEST(UIElementDragTest, ASingleMarkIsHitAndAnyCornerMovesBothAxes) {
        const AnchorMark mark = MarkOf({0.0f, 0.0f, 400.0f, 200.0f}, {0.5f, 0.5f}, {0.5f, 0.5f});
        const Optional<AnchorCorner> hit = UIElementDrag::HitAnchorCorner(mark, {201.0f, 99.0f}, 8.0f);
        ASSERT_TRUE(hit.has_value());
        const UIAnchorDragResult result = UIElementDrag::DragAnchorCorner({0.5f, 0.5f}, {0.5f, 0.5f}, *hit, {0.2f, 0.9f}, false);
        EXPECT_EQ(result.min, (Vec2{0.2f, 0.9f}));
        EXPECT_EQ(result.max, (Vec2{0.2f, 0.9f}));
    }

    TEST(UIElementDragTest, AStretchedAxisMovesOneEdgeAndAPointAxisMovesBoth) {
        // Stretched on X (0.2..0.6), a point on Y (0.5)
        const Vec2 min{0.2f, 0.5f};
        const Vec2 max{0.6f, 0.5f};
        UIAnchorDragResult result = UIElementDrag::DragAnchorCorner(min, max, AnchorCorner::TopLeft, {0.1f, 0.8f}, false);
        EXPECT_EQ(result.min, (Vec2{0.1f, 0.8f}));
        EXPECT_EQ(result.max, (Vec2{0.6f, 0.8f}));

        result = UIElementDrag::DragAnchorCorner(min, max, AnchorCorner::BottomRight, {0.9f, 0.3f}, false);
        EXPECT_EQ(result.min, (Vec2{0.2f, 0.3f}));
        EXPECT_EQ(result.max, (Vec2{0.9f, 0.3f}));
    }

    TEST(UIElementDragTest, TheAnchorsStayInTheBoxAndNeverCross) {
        const Vec2 min{0.2f, 0.2f};
        const Vec2 max{0.6f, 0.6f};
        UIAnchorDragResult result = UIElementDrag::DragAnchorCorner(min, max, AnchorCorner::TopLeft, {-0.5f, 1.5f}, false);
        EXPECT_EQ(result.min.x, 0.0f);
        EXPECT_EQ(result.min.y, 0.6f);  // up to the max, not past it
        EXPECT_EQ(result.max, max);

        result = UIElementDrag::DragAnchorCorner(min, max, AnchorCorner::BottomRight, {0.0f, 2.0f}, false);
        EXPECT_EQ(result.max.x, 0.2f);
        EXPECT_EQ(result.max.y, 1.0f);
        EXPECT_EQ(result.min, min);
    }

    TEST(UIElementDragTest, SnapRoundsTheAnchorsToTheEdgesAndTheMiddleAndOntoTheOtherEdge) {
        const Vec2 min{0.2f, 0.2f};
        const Vec2 max{0.6f, 0.6f};
        UIAnchorDragResult result = UIElementDrag::DragAnchorCorner(min, max, AnchorCorner::TopLeft, {0.43f, 0.04f}, true);
        EXPECT_EQ(result.min, (Vec2{0.5f, 0.0f}));

        // Within the threshold of the other edge the corner goes onto it: the axis becomes a point
        result = UIElementDrag::DragAnchorCorner(min, max, AnchorCorner::TopLeft, {0.6f - 0.01f, 0.5f}, true);
        EXPECT_EQ(result.min.x, 0.6f);
        EXPECT_EQ(result.max.x, 0.6f);

        result = UIElementDrag::DragAnchorCorner(min, max, AnchorCorner::BottomRight, {0.9f, 0.97f}, true);
        EXPECT_EQ(result.max, (Vec2{1.0f, 1.0f}));
    }

    TEST(UIElementDragTest, AWholeAnchorDragThroughSetAnchorsKeepsTheRectOfTheElement) {
        DragFixture f;
        const Rect box = *UIElementDrag::BoxOf(f.canvas, *f.child);
        const Rect before = f.child->rect;
        const UIEffectiveAnchors anchors = UILayout::EffectiveAnchors(f.child->style.rect);
        const AnchorMark mark = *UIPreviewOverlay::AnchorsOf(f.canvas, *f.child);
        ASSERT_TRUE(UIElementDrag::HitAnchorCorner(mark, CenterOf(mark.anchors), 5.0f).has_value());

        // Press on the single mark, drag it to the middle of the box, then stretch it on X
        const Vec2 pointer = {mark.box.x + mark.box.w * 0.5f, mark.box.y + mark.box.h * 0.5f};
        const Optional<Vec2> fraction = UIElementDrag::AnchorFraction(mark, pointer);
        ASSERT_TRUE(fraction.has_value());
        const UIAnchorDragResult dragged = UIElementDrag::DragAnchorCorner(anchors.min, anchors.max, AnchorCorner::TopLeft, *fraction, false);
        EXPECT_EQ(dragged.min, (Vec2{0.5f, 0.5f}));

        const UIRectEditResult result = UIAnchorPresets::SetAnchors(box, f.child->style.rect, dragged.min, dragged.max);
        EXPECT_TRUE(result.exact);
        ExpectRect(UILayout::ResolveRect(box, result.spec), before);

        // A point axis moves as one; from a stretched span a corner moves one edge, and the rect still stays
        const UIRectEditResult spanned = UIAnchorPresets::SetAnchors(box, result.spec, {0.2f, 0.5f}, {0.6f, 0.5f});
        ExpectRect(UILayout::ResolveRect(box, spanned.spec), before);
        const UIAnchorDragResult stretched = UIElementDrag::DragAnchorCorner({0.2f, 0.5f}, {0.6f, 0.5f}, AnchorCorner::TopRight, {0.9f, 0.5f}, false);
        EXPECT_EQ(stretched.min.x, 0.2f);
        EXPECT_EQ(stretched.max.x, 0.9f);
        const UIRectEditResult second = UIAnchorPresets::SetAnchors(box, spanned.spec, stretched.min, stretched.max);
        ExpectRect(UILayout::ResolveRect(box, second.spec), before);
    }

}  // namespace BECore::Tests
