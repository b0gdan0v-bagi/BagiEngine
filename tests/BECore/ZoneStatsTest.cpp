#include <BECore/Profiler/Ring/ZoneStats.h>
#include <EASTL/sort.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr ProfileSite kParentSite{"Parent", "Test", "Game/Parent.cpp", 10, 0};
        constexpr ProfileSite kChildSite{"Child", "Test", "Game/Child.cpp", 20, 0};
        constexpr ProfileSite kOtherSite{"Other", "Test", "Render/Other.cpp", 30, 0};

        constexpr double kNanosecondsPerTestTick = 1.0e6;  // a test tick is one millisecond
        constexpr double kTolerance = 1.0e-9;

        ZoneRecord Zone(const ProfileSite& site, uint64_t begin, uint64_t end, uint32_t depth = 0, uint64_t allocCount = 0, uint64_t allocBytes = 0) {
            return ZoneRecord{.site = &site, .beginTicks = begin, .endTicks = end, .allocCount = allocCount, .allocBytes = allocBytes, .depth = depth};
        }

        RingThread& AddThread(RingSnapshot& snapshot, uint32_t threadId) {
            RingThread& thread = snapshot.threads.emplace_back();
            thread.threadId = threadId;
            return thread;
        }

        /// @p count frames of @p length ticks each, numbered from @p firstIndex, the first beginning at tick 0
        RingSnapshot MakeSnapshot(uint64_t firstIndex, uint64_t count, uint64_t length) {
            RingSnapshot snapshot;
            snapshot.nanosecondsPerTick = kNanosecondsPerTestTick;
            for (uint64_t i = 0; i < count; ++i) {
                snapshot.frames.push_back(RingFrame{firstIndex + i, i * length, (i + 1) * length});
            }
            return snapshot;
        }

        const ZoneStatRow* FindRow(const eastl::vector<ZoneStatRow>& rows, const ProfileSite& site) {
            for (const ZoneStatRow& row : rows) {
                if (row.site == &site) {
                    return &row;
                }
            }
            return nullptr;
        }

        eastl::string Names(const eastl::vector<ZoneStatRow>& rows) {
            eastl::string out;
            for (const ZoneStatRow& row : rows) {
                out.append(row.site->name);
                out.append(",");
            }
            return out;
        }

        /// Parent 0..10 with children 1..4 (3 ms) and 5..7 (2 ms) on one thread, a leaf Other 20..21 on another, one frame of 30 ticks
        RingSnapshot MakeKnownSnapshot() {
            RingSnapshot snapshot = MakeSnapshot(7, 1, 30);
            RingThread& main = AddThread(snapshot, 1);
            main.zones = {Zone(kParentSite, 0, 10, 0, 9, 900), Zone(kChildSite, 1, 4, 1, 4, 400), Zone(kChildSite, 5, 7, 1, 1, 100)};
            AddThread(snapshot, 2).zones = {Zone(kOtherSite, 20, 21)};
            return snapshot;
        }

    }  // namespace

    TEST(ZoneStatsTest, SelfTimeIsTheDurationWithoutTheChildren) {
        const auto rows = ComputeZoneStats(MakeKnownSnapshot(), 7, 7);
        ASSERT_EQ(rows.size(), 3u);
        const ZoneStatRow* parent = FindRow(rows, kParentSite);
        ASSERT_NE(parent, nullptr);
        EXPECT_NEAR(parent->totalMs, 10.0, kTolerance);
        EXPECT_NEAR(parent->selfMs, 5.0, kTolerance);
        const ZoneStatRow* child = FindRow(rows, kChildSite);
        ASSERT_NE(child, nullptr);
        EXPECT_EQ(child->calls, 2u);
        EXPECT_NEAR(child->totalMs, 5.0, kTolerance);
        EXPECT_NEAR(child->selfMs, 5.0, kTolerance);  // nothing inside them
    }

    TEST(ZoneStatsTest, OnlyDirectChildrenAreSubtracted) {
        RingSnapshot snapshot = MakeSnapshot(0, 1, 100);
        AddThread(snapshot, 1).zones = {Zone(kParentSite, 0, 20, 0), Zone(kChildSite, 2, 12, 1), Zone(kOtherSite, 3, 5, 2)};
        const auto rows = ComputeZoneStats(snapshot, 0, 0);
        EXPECT_NEAR(FindRow(rows, kParentSite)->selfMs, 10.0, kTolerance);
        EXPECT_NEAR(FindRow(rows, kChildSite)->selfMs, 8.0, kTolerance);
        EXPECT_NEAR(FindRow(rows, kOtherSite)->selfMs, 2.0, kTolerance);
    }

    TEST(ZoneStatsTest, ASiblingAfterAZoneIsNotItsChild) {
        RingSnapshot snapshot = MakeSnapshot(0, 1, 100);
        AddThread(snapshot, 1).zones = {Zone(kParentSite, 0, 5), Zone(kChildSite, 6, 9), Zone(kOtherSite, 10, 10)};
        const auto rows = ComputeZoneStats(snapshot, 0, 0);
        EXPECT_NEAR(FindRow(rows, kParentSite)->selfMs, 5.0, kTolerance);
        EXPECT_NEAR(FindRow(rows, kChildSite)->selfMs, 3.0, kTolerance);
    }

    TEST(ZoneStatsTest, ARecursiveZoneCountsOncePerCall) {
        RingSnapshot snapshot = MakeSnapshot(0, 1, 100);
        AddThread(snapshot, 1).zones = {Zone(kParentSite, 0, 10, 0), Zone(kParentSite, 2, 6, 1)};
        const auto rows = ComputeZoneStats(snapshot, 0, 0);
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].calls, 2u);
        EXPECT_NEAR(rows[0].totalMs, 14.0, kTolerance);
        EXPECT_NEAR(rows[0].selfMs, 10.0, kTolerance);  // (10 - 4) + 4
    }

    TEST(ZoneStatsTest, CallsTotalMeanAndMaxOverSeveralFrames) {
        RingSnapshot snapshot = MakeSnapshot(0, 3, 100);
        AddThread(snapshot, 1).zones = {Zone(kChildSite, 10, 12), Zone(kChildSite, 110, 116), Zone(kChildSite, 210, 214)};
        const auto rows = ComputeZoneStats(snapshot, 0, 2);
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].calls, 3u);
        EXPECT_NEAR(rows[0].totalMs, 12.0, kTolerance);
        EXPECT_NEAR(rows[0].meanMs, 4.0, kTolerance);
        EXPECT_NEAR(rows[0].maxMs, 6.0, kTolerance);
    }

    TEST(ZoneStatsTest, TheNearestRankP95OfTwentyKnownCalls) {
        RingSnapshot snapshot = MakeSnapshot(0, 1, 1000);
        RingThread& thread = AddThread(snapshot, 1);
        uint64_t at = 0;
        for (uint64_t duration = 20; duration >= 1; --duration) {  // 20 calls of 1..20 ms, in an order that is not sorted
            thread.zones.push_back(Zone(kChildSite, at, at + duration));
            at += duration + 1;
        }
        eastl::sort(thread.zones.begin(), thread.zones.end(), [](const ZoneRecord& a, const ZoneRecord& b) { return a.beginTicks < b.beginTicks; });
        const auto rows = ComputeZoneStats(snapshot, 0, 0);
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_NEAR(rows[0].p95Ms, 19.0, kTolerance);
        EXPECT_NEAR(rows[0].maxMs, 20.0, kTolerance);
    }

    TEST(ZoneStatsTest, P95OfOneCallIsItsDuration) {
        RingSnapshot snapshot = MakeSnapshot(0, 1, 100);
        AddThread(snapshot, 1).zones = {Zone(kChildSite, 1, 4)};
        EXPECT_NEAR(ComputeZoneStats(snapshot, 0, 0)[0].p95Ms, 3.0, kTolerance);
    }

    TEST(ZoneStatsTest, TheWindowLeavesOutFramesOutsideIt) {
        RingSnapshot snapshot = MakeSnapshot(5, 4, 100);  // frames 5..8
        AddThread(snapshot, 1).zones = {Zone(kChildSite, 10, 11), Zone(kChildSite, 110, 112), Zone(kChildSite, 210, 213), Zone(kChildSite, 310, 314)};
        const auto middle = ComputeZoneStats(snapshot, 6, 7);
        ASSERT_EQ(middle.size(), 1u);
        EXPECT_EQ(middle[0].calls, 2u);
        EXPECT_NEAR(middle[0].totalMs, 5.0, kTolerance);
        EXPECT_TRUE(ComputeZoneStats(snapshot, 20, 30).empty());
        EXPECT_EQ(ComputeZoneStats(snapshot, 0, 100)[0].calls, 4u);  // a window wider than the ring is the ring
    }

    TEST(ZoneStatsTest, AZoneBelongsToTheFrameItBeganIn) {
        RingSnapshot snapshot = MakeSnapshot(0, 2, 100);
        AddThread(snapshot, 1).zones = {Zone(kParentSite, 90, 120, 0), Zone(kChildSite, 95, 110, 1)};
        const auto first = ComputeZoneStats(snapshot, 0, 0);
        ASSERT_EQ(first.size(), 2u);
        EXPECT_NEAR(FindRow(first, kParentSite)->selfMs, 15.0, kTolerance);  // its child runs into the next frame and is still taken off
        EXPECT_TRUE(ComputeZoneStats(snapshot, 1, 1).empty());
    }

    TEST(ZoneStatsTest, TheSameSiteOnTwoThreadsIsOneRow) {
        RingSnapshot snapshot = MakeSnapshot(0, 1, 100);
        AddThread(snapshot, 1).zones = {Zone(kChildSite, 0, 3)};
        AddThread(snapshot, 2).zones = {Zone(kChildSite, 1, 6)};
        const auto rows = ComputeZoneStats(snapshot, 0, 0);
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].calls, 2u);
        EXPECT_NEAR(rows[0].totalMs, 8.0, kTolerance);
    }

    TEST(ZoneStatsTest, AllocationsAreSummedInclusive) {
        const auto rows = ComputeZoneStats(MakeKnownSnapshot(), 7, 7);
        const ZoneStatRow* parent = FindRow(rows, kParentSite);
        EXPECT_EQ(parent->allocCount, 9u);
        EXPECT_EQ(parent->allocBytes, 900u);
        const ZoneStatRow* child = FindRow(rows, kChildSite);
        EXPECT_EQ(child->allocCount, 5u);
        EXPECT_EQ(child->allocBytes, 500u);
    }

    namespace {

        /// Rows with distinct values in every column: Parent is the largest of every number, Other the smallest, Child in between
        eastl::vector<ZoneStatRow> MakeSortableRows() {
            eastl::vector<ZoneStatRow> rows(3);
            rows[0] = {.site = &kChildSite, .calls = 2, .totalMs = 20, .selfMs = 10, .meanMs = 5, .maxMs = 7, .p95Ms = 6, .allocCount = 20, .allocBytes = 0};
            rows[1] = {.site = &kParentSite, .calls = 3, .totalMs = 30, .selfMs = 15, .meanMs = 6, .maxMs = 8, .p95Ms = 7, .allocCount = 30, .allocBytes = 0};
            rows[2] = {.site = &kOtherSite, .calls = 1, .totalMs = 10, .selfMs = 5, .meanMs = 4, .maxMs = 6, .p95Ms = 5, .allocCount = 10, .allocBytes = 0};
            return rows;
        }

    }  // namespace

    TEST(ZoneStatsTest, EveryColumnSortsBothWays) {
        for (const ZoneStatColumn column :
             {ZoneStatColumn::Calls, ZoneStatColumn::Total, ZoneStatColumn::Self, ZoneStatColumn::Mean, ZoneStatColumn::Max, ZoneStatColumn::P95, ZoneStatColumn::Allocations}) {
            auto rows = MakeSortableRows();
            SortZoneStats(rows, column, true);
            EXPECT_EQ(Names(rows), "Other,Child,Parent,") << "ascending, column " << static_cast<int>(column);
            SortZoneStats(rows, column, false);
            EXPECT_EQ(Names(rows), "Parent,Child,Other,") << "descending, column " << static_cast<int>(column);
        }
        auto rows = MakeSortableRows();
        SortZoneStats(rows, ZoneStatColumn::Name, true);
        EXPECT_EQ(Names(rows), "Child,Other,Parent,");
        SortZoneStats(rows, ZoneStatColumn::Name, false);
        EXPECT_EQ(Names(rows), "Parent,Other,Child,");
    }

    TEST(ZoneStatsTest, EqualValuesAreOrderedByNameInBothDirections) {
        auto rows = MakeSortableRows();
        for (ZoneStatRow& row : rows) {
            row.calls = 4;
        }
        SortZoneStats(rows, ZoneStatColumn::Calls, true);
        EXPECT_EQ(Names(rows), "Child,Other,Parent,");
        SortZoneStats(rows, ZoneStatColumn::Calls, false);
        EXPECT_EQ(Names(rows), "Child,Other,Parent,");
    }

    TEST(ZoneStatsTest, FilterByNameAndByFile) {
        const auto rows = MakeSortableRows();
        EXPECT_EQ(Names(FilterZoneStats(rows, "child")), "Child,");  // case does not matter
        EXPECT_EQ(Names(FilterZoneStats(rows, "RENDER/")), "Other,");
        EXPECT_EQ(Names(FilterZoneStats(rows, "Parent.cpp:10")), "Parent,");
        EXPECT_EQ(Names(FilterZoneStats(rows, "Game/")), "Child,Parent,");
    }

    TEST(ZoneStatsTest, FilterWithEmptyTextKeepsEveryRowAndWithNoMatchNone) {
        const auto rows = MakeSortableRows();
        EXPECT_EQ(FilterZoneStats(rows, "").size(), 3u);
        EXPECT_TRUE(FilterZoneStats(rows, "nothing like it").empty());
    }

    TEST(ExclusiveStageMsTest, SegmentsSumWithTheRemainderToCpuMs) {
        FrameStats frame;
        frame.cpuMs = 16.0;
        frame.stageMs[+FrameStage::Update] = 5.0;
        frame.stageMs[+FrameStage::SceneUpdate] = 2.0;
        frame.stageMs[+FrameStage::Render] = 6.0;
        frame.stageMs[+FrameStage::SceneDraw] = 4.0;
        frame.stageMs[+FrameStage::Present] = 3.0;
        const auto segments = ExclusiveStageMs(frame);
        EXPECT_NEAR(segments[+ExclusiveStage::UpdateOnly], 3.0, kTolerance);
        EXPECT_NEAR(segments[+ExclusiveStage::SceneUpdateStage], 2.0, kTolerance);
        EXPECT_NEAR(segments[+ExclusiveStage::RenderOnly], 2.0, kTolerance);
        EXPECT_NEAR(segments[+ExclusiveStage::SceneDrawStage], 4.0, kTolerance);
        EXPECT_NEAR(segments[+ExclusiveStage::PresentStage], 3.0, kTolerance);
        EXPECT_NEAR(segments[+ExclusiveStage::OtherStage], 2.0, kTolerance);
        double sum = 0.0;
        for (const double segment : segments) {
            sum += segment;
        }
        EXPECT_NEAR(sum, frame.cpuMs, kTolerance);
    }

    TEST(ExclusiveStageMsTest, AnInnerStageLongerThanItsParentClampsToZero) {
        FrameStats frame;
        frame.cpuMs = 10.0;
        frame.stageMs[+FrameStage::Update] = 1.0;
        frame.stageMs[+FrameStage::SceneUpdate] = 3.0;
        frame.stageMs[+FrameStage::Render] = 2.0;
        frame.stageMs[+FrameStage::SceneDraw] = 5.0;
        const auto segments = ExclusiveStageMs(frame);
        for (const double segment : segments) {
            EXPECT_GE(segment, 0.0);
        }
        EXPECT_EQ(segments[+ExclusiveStage::UpdateOnly], 0.0);
        EXPECT_EQ(segments[+ExclusiveStage::RenderOnly], 0.0);
        EXPECT_EQ(segments[+ExclusiveStage::OtherStage], 2.0);
    }

}  // namespace BECore::Tests
