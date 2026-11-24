#include <BECore/Profiler/ProfilerToggle.h>
#include <BECore/Profiler/Ring/RingTraceExport.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Common/TraceDoc.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr ProfileSite kOuterSite{"Outer", "Test", "RingTraceExportTest.cpp", 1, 0};
        constexpr ProfileSite kInnerSite{"Inner", "Test", "RingTraceExportTest.cpp", 2, 0};

        constexpr double kNanosecondsPerTestTick = 1.0e6;  // a test tick is one millisecond
        constexpr double kMicrosecondsPerTestTick = 1000.0;
        constexpr uint64_t kFrameTicks = 16;
        constexpr uint64_t kFrameCount = 10;
        constexpr uint32_t kWorkerThreadId = 2;
        constexpr uint32_t kWorkerSortIndex = 5;

        String Member(JsonView event, const char* key) {
            return StringOf(event, key);
        }

        TraceDoc ParseTrace(const String& trace) {
            return TraceDoc(StringView(trace.c_str(), trace.size()));
        }

        /// Ten frames of 16 ticks (frame k spans 16k..16k+16): every frame has Outer with Inner inside on Main and one zone on Worker
        RingSnapshot TenFrames() {
            RingSnapshot snapshot;
            snapshot.nanosecondsPerTick = kNanosecondsPerTestTick;
            snapshot.threads.resize(2);
            RingThread& main = snapshot.threads[0];
            main.threadId = 1;
            main.name = "Main";
            main.sortIndex = 0;
            RingThread& worker = snapshot.threads[1];
            worker.threadId = kWorkerThreadId;
            worker.name = "Worker";
            worker.sortIndex = kWorkerSortIndex;
            for (uint64_t k = 0; k < kFrameCount; ++k) {
                const uint64_t begin = k * kFrameTicks;
                snapshot.frames.push_back(RingFrame{k, begin, begin + kFrameTicks});
                main.zones.push_back(ZoneRecord{.site = &kOuterSite, .beginTicks = begin + 1, .endTicks = begin + 10, .depth = 0});
                main.zones.push_back(ZoneRecord{.site = &kInnerSite, .beginTicks = begin + 2, .endTicks = begin + 5, .depth = 1});
                worker.zones.push_back(ZoneRecord{.site = &kInnerSite, .beginTicks = begin + 3, .endTicks = begin + 6, .depth = 0});
            }
            return snapshot;
        }

        /// The instant events of @p trace: views into it, valid while it lives
        Vector<JsonView> InstantsOf(const TraceDoc& trace) {
            Vector<JsonView> marks;
            for (const JsonView event : trace) {
                if (Member(event, "ph") == "i") {
                    marks.push_back(event);
                }
            }
            return marks;
        }

    }  // namespace

    TEST(RingTraceExportTest, TheWindowKeepsTheFramesThatEndWithinTheSecondsBeforeTheNewestEnd) {
        const RingSnapshot snapshot = TenFrames();  // the newest frame ends at 160 ms
        const TraceDoc trace = ParseTrace(WriteRingTraceJson(snapshot, 0.05));
        const Vector<JsonView> marks = InstantsOf(trace);  // from 110 ms: frames 6..9 end at 112 and later

        ASSERT_EQ(marks.size(), 4u);
        EXPECT_EQ(IntOf(MemberOf(marks[0], "args"), "frame"), 6);
        EXPECT_EQ(IntOf(MemberOf(marks[3], "args"), "frame"), 9);
        EXPECT_EQ(InstantsOf(ParseTrace(WriteRingTraceJson(snapshot, 1000.0))).size(), kFrameCount);
    }

    TEST(RingTraceExportTest, CountingFramesAgreesWithTheFramesTheTraceHolds) {
        const RingSnapshot snapshot = TenFrames();
        for (const double seconds : {1000.0, 0.05, 0.0, -1.0}) {
            EXPECT_EQ(CountRingTraceFrames(snapshot, seconds), InstantsOf(ParseTrace(WriteRingTraceJson(snapshot, seconds))).size()) << seconds;
        }
        EXPECT_EQ(CountRingTraceFrames(RingSnapshot{}, 10.0), 0u);
    }

    TEST(RingTraceExportTest, TheNewestFrameIsAlwaysInAlsoForNoWindow) {
        const RingSnapshot snapshot = TenFrames();
        for (const double seconds : {0.0, -1.0, 1.0e-9}) {
            const TraceDoc trace = ParseTrace(WriteRingTraceJson(snapshot, seconds));
            const Vector<JsonView> marks = InstantsOf(trace);
            ASSERT_EQ(marks.size(), 1u) << seconds;
            EXPECT_EQ(IntOf(MemberOf(marks[0], "args"), "frame"), static_cast<int64_t>(kFrameCount - 1));
        }
    }

    TEST(RingTraceExportTest, EveryThreadIsABalancedStackWithItsNameAndSortIndexAndTheFramesAreConsecutiveMarks) {
        const TraceDoc trace = ParseTrace(WriteRingTraceJson(TenFrames(), 1000.0));
        Map<int64_t, int> depth;
        Map<int64_t, String> names;
        Map<int64_t, int64_t> sortIndexes;
        size_t begins = 0;
        for (const JsonView event : trace) {
            const String phase = Member(event, "ph");
            const int64_t tid = IntOf(event, "tid").value_or(-1);
            if (phase == "B" || phase == "E") {
                depth[tid] += phase == "B" ? 1 : -1;
                EXPECT_GE(depth[tid], 0);
                begins += phase == "B" ? 1 : 0;
            } else if (phase == "M" && Member(event, "name") == "thread_name") {
                names[tid] = Member(MemberOf(event, "args"), "name");
            } else if (phase == "M" && Member(event, "name") == "thread_sort_index") {
                sortIndexes[tid] = IntOf(MemberOf(event, "args"), "sort_index").value_or(-1);
            }
        }
        EXPECT_EQ(begins, kFrameCount * 3);
        EXPECT_EQ(depth[1], 0);
        EXPECT_EQ(depth[kWorkerThreadId], 0);
        EXPECT_EQ(names[1], "Main");
        EXPECT_EQ(names[kWorkerThreadId], "Worker");
        EXPECT_EQ(sortIndexes[kWorkerThreadId], kWorkerSortIndex);

        const Vector<JsonView> marks = InstantsOf(trace);
        ASSERT_EQ(marks.size(), kFrameCount);
        for (size_t i = 0; i < marks.size(); ++i) {
            EXPECT_EQ(Member(marks[i], "name"), "Frame");
            EXPECT_NEAR(NumberOf(marks[i], "ts"), static_cast<double>(i * kFrameTicks) * kMicrosecondsPerTestTick, 1.0e-6);
        }
    }

    TEST(RingTraceExportTest, OtherDataCountsTheFramesTheDroppedZonesAndNamesTheBuild) {
        RingSnapshot snapshot = TenFrames();
        snapshot.dropped = 7;
        const TraceDoc trace = ParseTrace(WriteRingTraceJson(snapshot, 0.05));
        const JsonView other = MemberOf(trace.Root(), "otherData");
        ASSERT_TRUE(other.IsObject());
        EXPECT_EQ(IntOf(other, "ringFrames"), 4);
        EXPECT_EQ(IntOf(other, "dropped"), 7);
        EXPECT_EQ(Member(other, "build"), kProfilerStatsBuild ? "stats" : "profile");
    }

    TEST(RingTraceExportTest, AnEmptySnapshotIsAValidEmptyTrace) {
        const TraceDoc trace = ParseTrace(WriteRingTraceJson(RingSnapshot{}, 10.0));
        EXPECT_EQ(trace.size(), 0u);
        EXPECT_EQ(IntOf(MemberOf(trace.Root(), "otherData"), "ringFrames"), 0);
    }

    TEST(RingTraceExportTest, TheFileHoldsTheSameTextAndAnUnwritablePathIsAnError) {
        const RingSnapshot snapshot = TenFrames();
        const Path path = TestTempPath("ring_trace").ReplaceExtension(".json");
        const auto written = WriteRingTraceFile(path.View(), snapshot, 0.05);
        ASSERT_TRUE(written.has_value());
        EXPECT_EQ(ReadFile(path), WriteRingTraceJson(snapshot, 0.05));
        RemoveFileIfPresent(path);

        const auto refused = WriteRingTraceFile((path / "no" / "such" / "dir.json").View(), snapshot, 0.05);
        ASSERT_FALSE(refused.has_value());
        EXPECT_NE(refused.error().find("cannot write"), String::npos);
    }

}  // namespace BECore::Tests
