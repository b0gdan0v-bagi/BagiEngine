#include <BECore/Control/Json.h>
#include <BECore/Profiler/Ring/RingTraceExport.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
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

        String Member(const Json& event, const char* key) {
            const auto value = GetJsonString(event, key);
            return value ? *value : String();
        }

        Json ParseTrace(const String& trace) {
            auto parsed = ParseJson(trace);
            EXPECT_TRUE(parsed.has_value()) << trace;
            return parsed ? *parsed : Json::object();
        }

        Json EventsOf(const Json& trace) {
            const Json* events = FindJsonMember(trace, "traceEvents");
            EXPECT_NE(events, nullptr);
            return events != nullptr ? *events : Json::array();
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

        Vector<Json> InstantsOf(const Json& trace) {
            Vector<Json> marks;
            for (const Json& event : EventsOf(trace)) {
                if (Member(event, "ph") == "i") {
                    marks.push_back(event);
                }
            }
            return marks;
        }

    }  // namespace

    TEST(RingTraceExportTest, TheWindowKeepsTheFramesThatEndWithinTheSecondsBeforeTheNewestEnd) {
        const RingSnapshot snapshot = TenFrames();                                              // the newest frame ends at 160 ms
        const Vector<Json> marks = InstantsOf(ParseTrace(WriteRingTraceJson(snapshot, 0.05)));  // from 110 ms: frames 6..9 end at 112 and later

        ASSERT_EQ(marks.size(), 4u);
        EXPECT_EQ(GetJsonInt(*FindJsonMember(marks[0], "args"), "frame"), 6);
        EXPECT_EQ(GetJsonInt(*FindJsonMember(marks[3], "args"), "frame"), 9);
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
            const Vector<Json> marks = InstantsOf(ParseTrace(WriteRingTraceJson(snapshot, seconds)));
            ASSERT_EQ(marks.size(), 1u) << seconds;
            EXPECT_EQ(GetJsonInt(*FindJsonMember(marks[0], "args"), "frame"), static_cast<int64_t>(kFrameCount - 1));
        }
    }

    TEST(RingTraceExportTest, EveryThreadIsABalancedStackWithItsNameAndSortIndexAndTheFramesAreConsecutiveMarks) {
        const Json trace = ParseTrace(WriteRingTraceJson(TenFrames(), 1000.0));
        Map<int64_t, int> depth;
        Map<int64_t, String> names;
        Map<int64_t, int64_t> sortIndexes;
        size_t begins = 0;
        for (const Json& event : EventsOf(trace)) {
            const String phase = Member(event, "ph");
            const int64_t tid = GetJsonInt(event, "tid").value_or(-1);
            if (phase == "B" || phase == "E") {
                depth[tid] += phase == "B" ? 1 : -1;
                EXPECT_GE(depth[tid], 0);
                begins += phase == "B" ? 1 : 0;
            } else if (phase == "M" && Member(event, "name") == "thread_name") {
                names[tid] = Member(*FindJsonMember(event, "args"), "name");
            } else if (phase == "M" && Member(event, "name") == "thread_sort_index") {
                sortIndexes[tid] = GetJsonInt(*FindJsonMember(event, "args"), "sort_index").value_or(-1);
            }
        }
        EXPECT_EQ(begins, kFrameCount * 3);
        EXPECT_EQ(depth[1], 0);
        EXPECT_EQ(depth[kWorkerThreadId], 0);
        EXPECT_EQ(names[1], "Main");
        EXPECT_EQ(names[kWorkerThreadId], "Worker");
        EXPECT_EQ(sortIndexes[kWorkerThreadId], kWorkerSortIndex);

        const Vector<Json> marks = InstantsOf(trace);
        ASSERT_EQ(marks.size(), kFrameCount);
        for (size_t i = 0; i < marks.size(); ++i) {
            EXPECT_EQ(Member(marks[i], "name"), "Frame");
            EXPECT_NEAR(GetJsonNumber(marks[i], "ts").value_or(-1.0), static_cast<double>(i * kFrameTicks) * kMicrosecondsPerTestTick, 1.0e-6);
        }
    }

    TEST(RingTraceExportTest, OtherDataCountsTheFramesTheDroppedZonesAndNamesTheBuild) {
        RingSnapshot snapshot = TenFrames();
        snapshot.dropped = 7;
        const Json trace = ParseTrace(WriteRingTraceJson(snapshot, 0.05));
        const Json* other = FindJsonMember(trace, "otherData");
        ASSERT_NE(other, nullptr);
        EXPECT_EQ(GetJsonInt(*other, "ringFrames"), 4);
        EXPECT_EQ(GetJsonInt(*other, "dropped"), 7);
        EXPECT_EQ(Member(*other, "build"), "stats");
    }

    TEST(RingTraceExportTest, AnEmptySnapshotIsAValidEmptyTrace) {
        const Json trace = ParseTrace(WriteRingTraceJson(RingSnapshot{}, 10.0));
        EXPECT_TRUE(EventsOf(trace).empty());
        EXPECT_EQ(GetJsonInt(*FindJsonMember(trace, "otherData"), "ringFrames"), 0);
    }

    TEST(RingTraceExportTest, TheFileHoldsTheSameTextAndAnUnwritablePathIsAnError) {
        const RingSnapshot snapshot = TenFrames();
        const std::filesystem::path path = TestTempPath("ring_trace").replace_extension(".json");
        const auto written = WriteRingTraceFile(StringView(path.string().c_str()), snapshot, 0.05);
        ASSERT_TRUE(written.has_value());
        EXPECT_EQ(ReadFile(path), WriteRingTraceJson(snapshot, 0.05));
        std::filesystem::remove(path);

        const auto refused = WriteRingTraceFile(StringView((path / "no" / "such" / "dir.json").string().c_str()), snapshot, 0.05);
        ASSERT_FALSE(refused.has_value());
        EXPECT_NE(refused.error().find("cannot write"), String::npos);
    }

}  // namespace BECore::Tests
