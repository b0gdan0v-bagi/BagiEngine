#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/Ring/TimelineView.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr ProfileSite kOuterSite{"Outer", "Test", "TimelineViewTest.cpp", 1, 0};
        constexpr ProfileSite kInnerSite{"Inner", "Test", "TimelineViewTest.cpp", 2, 0};

        constexpr double kNanosecondsPerTestTick = 1000.0;  // a test tick is one microsecond
        constexpr uint64_t kExtentEnd = 10000;
        constexpr float kWidth = 1000.0f;
        constexpr float kRowHeight = 20.0f;
        constexpr double kTolerance = 1.0e-6;

        TimelineView MakeView() {
            TimelineView view;
            view.SetExtent(0, kExtentEnd, kNanosecondsPerTestTick);
            view.FitTo(0, kExtentEnd);
            return view;
        }

        RingThread& AddThread(RingSnapshot& snapshot, uint32_t threadId, uint32_t sortIndex) {
            RingThread& thread = snapshot.threads.emplace_back();
            thread.threadId = threadId;
            thread.sortIndex = sortIndex;
            return thread;
        }

        ZoneRecord Zone(const ProfileSite& site, uint64_t begin, uint64_t end, uint32_t depth) {
            return ZoneRecord{.site = &site, .beginTicks = begin, .endTicks = end, .depth = depth};
        }

        std::vector<uint32_t> ThreadIds(const RingSnapshot& snapshot, const eastl::vector<TimelineLane>& lanes) {
            std::vector<uint32_t> ids;
            for (const TimelineLane& lane : lanes) {
                ids.push_back(snapshot.threads[lane.threadIndex].threadId);
            }
            return ids;
        }

    }  // namespace

    TEST(TimelineViewTest, ZoomKeepsTheAnchorTickAtTheSameX) {
        TimelineView view = MakeView();
        view.FitTo(2000, 6000);
        const double anchor = 3000.0;
        const float before = view.TickToX(anchor, kWidth);
        view.ZoomAt(anchor, 0.5);
        EXPECT_NEAR(view.TickToX(anchor, kWidth), before, 1.0e-3f);
        EXPECT_NEAR(view.SpanTicks(), 2000.0, kTolerance);
        view.ZoomAt(anchor, 2.0);
        EXPECT_NEAR(view.TickToX(anchor, kWidth), before, 1.0e-3f);
        EXPECT_NEAR(view.SpanTicks(), 4000.0, kTolerance);
    }

    TEST(TimelineViewTest, ZoomStopsAtTheMinimumSpanAndAtTheExtent) {
        TimelineView view = MakeView();
        view.ZoomAt(5000.0, 1.0e-9);
        EXPECT_NEAR(view.SpanTicks(), 5.0, kTolerance);  // 5 microseconds at one tick a microsecond
        view.ZoomAt(5000.0, 1.0e9);
        EXPECT_NEAR(view.SpanTicks(), static_cast<double>(kExtentEnd), kTolerance);
        EXPECT_NEAR(view.BeginTicks(), 0.0, kTolerance);
    }

    TEST(TimelineViewTest, PanClampsAtBothEnds) {
        TimelineView view = MakeView();
        view.FitTo(1000, 3000);
        view.PanByTicks(-5000.0);
        EXPECT_NEAR(view.BeginTicks(), 0.0, kTolerance);
        EXPECT_NEAR(view.SpanTicks(), 2000.0, kTolerance);
        view.PanByTicks(1.0e7);
        EXPECT_NEAR(view.EndTicks(), static_cast<double>(kExtentEnd), kTolerance);
        view.PanByTicks(-500.0);
        EXPECT_NEAR(view.BeginTicks(), 7500.0, kTolerance);
    }

    TEST(TimelineViewTest, ZoomOutNearTheEdgeShiftsTheRangeBackInsideTheExtent) {
        TimelineView view = MakeView();
        view.FitTo(8000, 10000);
        view.ZoomAt(9900.0, 3.0);
        EXPECT_LE(view.EndTicks(), static_cast<double>(kExtentEnd) + kTolerance);
        EXPECT_NEAR(view.SpanTicks(), 6000.0, kTolerance);
    }

    TEST(TimelineViewTest, TickAndXRoundTrip) {
        TimelineView view = MakeView();
        view.FitTo(2000, 4000);
        for (const double tick : {2000.0, 2500.5, 3999.0}) {
            EXPECT_NEAR(view.XToTick(view.TickToX(tick, kWidth), kWidth), tick, 1.0e-2);
        }
        EXPECT_NEAR(view.TickToX(2000.0, kWidth), 0.0f, 1.0e-4f);
        EXPECT_NEAR(view.TickToX(4000.0, kWidth), kWidth, 1.0e-3f);
        EXPECT_NEAR(view.PixelsPerTick(kWidth), 0.5, kTolerance);
    }

    TEST(TimelineViewTest, FitToShowsExactlyTheRangeAskedFor) {
        TimelineView view = MakeView();
        view.FitTo(100, 900);
        EXPECT_NEAR(view.BeginTicks(), 100.0, kTolerance);
        EXPECT_NEAR(view.EndTicks(), 900.0, kTolerance);
        view.FitTo(500, 500);
        EXPECT_NEAR(view.SpanTicks(), 5.0, kTolerance);  // never narrower than the minimum span
    }

    TEST(TimelineViewTest, LanesFollowTheSortIndexThenTheThreadId) {
        RingSnapshot snapshot;
        AddThread(snapshot, 7, Profiler::kNoSortIndex).zones = {Zone(kOuterSite, 0, 10, 0)};
        AddThread(snapshot, 3, Profiler::kNoSortIndex).zones = {Zone(kOuterSite, 0, 10, 0)};
        AddThread(snapshot, 9, 100).zones = {Zone(kOuterSite, 0, 10, 0)};
        AddThread(snapshot, 4, 0).zones = {Zone(kOuterSite, 0, 10, 0)};
        const auto lanes = LayoutLanes(snapshot, 0.0, 100.0);
        EXPECT_EQ(ThreadIds(snapshot, lanes), (std::vector<uint32_t>{4, 9, 3, 7}));
    }

    TEST(TimelineViewTest, LanesWithoutZonesInTheRangeAreHiddenAndRowsFollowTheDepth) {
        RingSnapshot snapshot;
        AddThread(snapshot, 1, 0).zones = {Zone(kOuterSite, 0, 50, 0), Zone(kInnerSite, 10, 20, 1), Zone(kInnerSite, 12, 15, 2)};
        AddThread(snapshot, 2, 1).zones = {Zone(kOuterSite, 500, 600, 0)};
        AddThread(snapshot, 3, 2);  // no zones at all

        auto lanes = LayoutLanes(snapshot, 0.0, 100.0);
        ASSERT_EQ(lanes.size(), 1u);
        EXPECT_EQ(lanes[0].threadIndex, 0u);
        EXPECT_EQ(lanes[0].rows, 3u);

        lanes = LayoutLanes(snapshot, 0.0, 100.0 + 1000.0);
        EXPECT_EQ(lanes.size(), 2u);

        lanes = LayoutLanes(snapshot, 16.0, 18.0);  // the parent and the first child reach into it; the depth-2 zone ended at 15
        ASSERT_EQ(lanes.size(), 1u);
        EXPECT_EQ(lanes[0].rows, 2u);

        EXPECT_TRUE(LayoutLanes(snapshot, 200.0, 300.0).empty());
    }

    TEST(TimelineViewTest, HitTestFindsTheChildAtItsRowAndTheParentAtRowZero) {
        RingSnapshot snapshot;
        AddThread(snapshot, 1, 0).zones = {Zone(kOuterSite, 1000, 5000, 0), Zone(kInnerSite, 2000, 3000, 1)};
        AddThread(snapshot, 2, 1).zones = {Zone(kOuterSite, 0, 10000, 0)};
        const TimelineView view = MakeView();  // 10 ticks a pixel
        const auto lanes = LayoutLanes(snapshot, view.BeginTicks(), view.EndTicks());
        ASSERT_EQ(lanes.size(), 2u);
        constexpr float kGap = 4.0f;

        const auto child = HitTest(lanes, snapshot, view, 250.0f, kRowHeight * 1.5f, kRowHeight, kWidth, kGap);
        ASSERT_TRUE(child.has_value());
        EXPECT_EQ(child->threadIndex, 0u);
        EXPECT_EQ(child->zoneIndex, 1u);

        const auto parent = HitTest(lanes, snapshot, view, 250.0f, kRowHeight * 0.5f, kRowHeight, kWidth, kGap);
        ASSERT_TRUE(parent.has_value());
        EXPECT_EQ(parent->zoneIndex, 0u);

        const auto second = HitTest(lanes, snapshot, view, 500.0f, LaneTop(lanes, 1, kRowHeight, kGap) + 3.0f, kRowHeight, kWidth, kGap);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(second->threadIndex, 1u);
    }

    TEST(TimelineViewTest, HitTestFindsNothingOnEmptySpace) {
        RingSnapshot snapshot;
        AddThread(snapshot, 1, 0).zones = {Zone(kOuterSite, 1000, 5000, 0), Zone(kInnerSite, 2000, 3000, 1)};
        AddThread(snapshot, 2, 1).zones = {Zone(kOuterSite, 0, 100, 0)};
        const TimelineView view = MakeView();
        const auto lanes = LayoutLanes(snapshot, view.BeginTicks(), view.EndTicks());
        constexpr float kGap = 4.0f;

        EXPECT_FALSE(HitTest(lanes, snapshot, view, 800.0f, 5.0f, kRowHeight, kWidth, kGap));                                       // right of the parent
        EXPECT_FALSE(HitTest(lanes, snapshot, view, 150.0f, kRowHeight * 1.5f, kRowHeight, kWidth, kGap));                          // row 1 left of the child
        EXPECT_FALSE(HitTest(lanes, snapshot, view, 250.0f, kRowHeight * 2.0f + 1.0f, kRowHeight, kWidth, kGap));                   // the gap between the lanes
        EXPECT_FALSE(HitTest(lanes, snapshot, view, 250.0f, 10000.0f, kRowHeight, kWidth, kGap));                                   // below the last lane
        EXPECT_FALSE(HitTest(lanes, snapshot, view, 250.0f, -3.0f, kRowHeight, kWidth, kGap));                                      // above the first
        EXPECT_FALSE(HitTest(lanes, snapshot, view, 20.0f, LaneTop(lanes, 1, kRowHeight, kGap) + 2.0f, kRowHeight, kWidth, kGap));  // the zone is 100 ticks, 10 pixels wide
    }

    TEST(TimelineViewTest, ANarrowZoneIsVisibleOnlyFromOnePixel) {
        const ZoneRecord zone = Zone(kOuterSite, 100, 110, 0);  // 10 ticks
        EXPECT_TRUE(IsVisible(zone, 0.1));                      // exactly one pixel
        EXPECT_FALSE(IsVisible(zone, 0.09));
        EXPECT_TRUE(IsVisible(zone, 5.0));
    }

    TEST(TimelineViewTest, FindWorstCallGivesTheLongestCallOnAnyThread) {
        RingSnapshot snapshot;
        AddThread(snapshot, 1, 0).zones = {Zone(kOuterSite, 0, 10, 0), Zone(kInnerSite, 20, 90, 0), Zone(kOuterSite, 100, 130, 0)};
        AddThread(snapshot, 2, 1).zones = {Zone(kOuterSite, 0, 50, 0), Zone(kOuterSite, 60, 110, 0)};
        const auto worst = FindWorstCall(snapshot, &kOuterSite);
        ASSERT_TRUE(worst.has_value());
        EXPECT_EQ(worst->threadIndex, 1u);
        EXPECT_EQ(worst->zoneIndex, 0u);  // 50 ticks, first of two equal
        EXPECT_FALSE(FindWorstCall(snapshot, nullptr));
        constexpr ProfileSite kUnknown{"Unknown", "Test", "x.cpp", 1, 0};
        EXPECT_FALSE(FindWorstCall(snapshot, &kUnknown));
    }

    TEST(TimelineViewTest, FrameAtGivesTheFrameThatHoldsTheTick) {
        RingSnapshot snapshot;
        snapshot.frames = {RingFrame{4, 0, 100}, RingFrame{5, 100, 250}};
        ASSERT_NE(FrameAt(snapshot, 0), nullptr);
        EXPECT_EQ(FrameAt(snapshot, 0)->index, 4u);
        EXPECT_EQ(FrameAt(snapshot, 99)->index, 4u);
        EXPECT_EQ(FrameAt(snapshot, 100)->index, 5u);  // a frame ends where the next begins
        EXPECT_EQ(FrameAt(snapshot, 250), nullptr);
    }

    TEST(TimelineViewTest, TheExtentOfASnapshotIsItsFramesElseItsZones) {
        RingSnapshot snapshot;
        AddThread(snapshot, 1, 0).zones = {Zone(kOuterSite, 30, 50, 0), Zone(kOuterSite, 20, 90, 0)};
        EXPECT_EQ(SnapshotExtent(snapshot).begin, 20u);
        EXPECT_EQ(SnapshotExtent(snapshot).end, 90u);
        snapshot.frames = {RingFrame{0, 10, 60}, RingFrame{1, 60, 120}};
        EXPECT_EQ(SnapshotExtent(snapshot).begin, 10u);
        EXPECT_EQ(SnapshotExtent(snapshot).end, 120u);
        EXPECT_EQ(SnapshotExtent(RingSnapshot{}).end, 0u);
    }

}  // namespace BECore::Tests
