#include <BECore/Control/Json.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/Ring/FrozenTrace.h>
#include <BECore/Profiler/Ring/SpikeCapture.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <gtest/gtest.h>
#include <map>

namespace BECore::Tests {

    namespace {

        constexpr ProfileSite kOuterSite{"Outer", "Test", "SpikeCaptureTest.cpp", 1, 0};
        constexpr ProfileSite kInnerSite{"Inner \"quoted\"\\", "Test", "C:\\dir\\SpikeCaptureTest.cpp", 2, 0};

        constexpr double kNanosecondsPerTestTick = 1.0e6;  // a test tick is one millisecond
        constexpr double kTolerance = 1.0e-9;
        constexpr uint64_t kSteadyFrameTicks = 16;

        SpikeSettings AbsoluteSettings(double thresholdMs, size_t keep = SpikeSettings::kDefaultKeep) {
            SpikeSettings settings;
            settings.mode = SpikeMode::AbsoluteMs;
            settings.thresholdMs = thresholdMs;
            settings.keep = keep;
            return settings;
        }

        SpikeSettings MedianSettings(size_t window) {
            SpikeSettings settings;
            settings.mode = SpikeMode::TimesMedian;
            settings.medianFactor = 3.0;
            settings.medianWindow = window;
            return settings;
        }

        /// Appends frame @p index of @p length ticks right after the last frame of the snapshot
        void AddFrame(RingSnapshot& snapshot, uint64_t index, uint64_t length) {
            snapshot.nanosecondsPerTick = kNanosecondsPerTestTick;
            const uint64_t begin = snapshot.frames.empty() ? 0 : snapshot.frames.back().endTicks;
            snapshot.frames.push_back(RingFrame{index, begin, begin + length});
        }

        RingThread& MainThread(RingSnapshot& snapshot) {
            if (snapshot.threads.empty()) {
                RingThread& thread = snapshot.threads.emplace_back();
                thread.threadId = 1;
                thread.name = "Main";
                thread.sortIndex = 0;
            }
            return snapshot.threads[0];
        }

        void AddZone(RingSnapshot& snapshot, const ProfileSite& site, uint64_t begin, uint64_t end, uint32_t depth = 0) {
            MainThread(snapshot).zones.push_back(ZoneRecord{.site = &site, .beginTicks = begin, .endTicks = end, .allocCount = 2, .allocBytes = 64, .depth = depth});
        }

        /// @p count steady frames numbered from @p first
        RingSnapshot SteadyFrames(uint64_t first, uint64_t count) {
            RingSnapshot snapshot;
            for (uint64_t i = 0; i < count; ++i) {
                AddFrame(snapshot, first + i, kSteadyFrameTicks);
            }
            return snapshot;
        }

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

        /// A snapshot with a spike of @p length ticks as frame 3 after three steady frames; the spike holds Outer with Inner inside
        RingSnapshot WithASpike(uint64_t length) {
            RingSnapshot snapshot = SteadyFrames(0, 3);
            AddFrame(snapshot, 3, length);
            const uint64_t begin = snapshot.frames.back().beginTicks;
            AddZone(snapshot, kOuterSite, 1, 4);  // a zone of an earlier frame: not part of the spike
            AddZone(snapshot, kOuterSite, begin + 1, begin + length - 1);
            AddZone(snapshot, kInnerSite, begin + 2, begin + 5, 1);
            return snapshot;
        }

    }  // namespace

    TEST(SpikeCaptureTest, AFrameAboveTheAbsoluteThresholdIsFrozenWithExactlyItsZones) {
        SpikeCapture capture;
        capture.SetSettings(AbsoluteSettings(50.0));
        capture.Update(WithASpike(80));

        ASSERT_EQ(capture.GetSpikes().size(), 1u);
        const FrozenFrame& spike = capture.GetSpikes()[0];
        EXPECT_EQ(spike.frameIndex, 3u);
        EXPECT_NEAR(spike.frameMs, 80.0, kTolerance);
        EXPECT_EQ(spike.beginTicks, 3 * kSteadyFrameTicks);
        ASSERT_EQ(spike.threads.size(), 1u);
        EXPECT_EQ(spike.threads[0].name, "Main");
        ASSERT_EQ(spike.threads[0].zones.size(), 2u);
        EXPECT_EQ(spike.threads[0].zones[0].site, &kOuterSite);
        EXPECT_EQ(spike.threads[0].zones[1].site, &kInnerSite);
        EXPECT_EQ(spike.truncatedZones, 0u);
    }

    TEST(SpikeCaptureTest, AFrameBelowTheThresholdIsNotFrozen) {
        SpikeCapture capture;
        capture.SetSettings(AbsoluteSettings(50.0));
        capture.Update(WithASpike(50));  // equal to the threshold is not above it
        EXPECT_TRUE(capture.GetSpikes().empty());
    }

    TEST(SpikeCaptureTest, TimesMedianNeedsEnoughPreviousFramesAndFiresAtThreeTimesTheMedian) {
        SpikeCapture capture;
        capture.SetSettings(MedianSettings(8));  // the minimum is the window when the window is smaller than kMinMedianFrames

        RingSnapshot early = SteadyFrames(0, 7);
        AddFrame(early, 7, 100);  // seven steady frames before it: not enough
        capture.Update(early);
        EXPECT_TRUE(capture.GetSpikes().empty());

        SpikeCapture second;
        second.SetSettings(MedianSettings(8));
        RingSnapshot snapshot = SteadyFrames(0, 8);
        AddFrame(snapshot, 8, 47);  // 47 ms: under 3 x 16 ms
        AddFrame(snapshot, 9, 49);  // 49 ms: over 3 x 16 ms
        second.Update(snapshot);
        ASSERT_EQ(second.GetSpikes().size(), 1u);
        EXPECT_EQ(second.GetSpikes()[0].frameIndex, 9u);
        EXPECT_NEAR(second.GetSpikes()[0].medianMs, 16.0, kTolerance);
    }

    TEST(SpikeCaptureTest, TimesMedianWithTheDefaultWindowWaitsForTheMinimum) {
        SpikeCapture capture;
        SpikeSettings settings = MedianSettings(SpikeSettings::kDefaultMedianWindow);
        capture.SetSettings(settings);
        RingSnapshot snapshot = SteadyFrames(0, SpikeCapture::kMinMedianFrames - 1);
        AddFrame(snapshot, 100, 200);
        AddFrame(snapshot, 101, 16);
        AddFrame(snapshot, 102, 200);  // now there are enough previous frames
        capture.Update(snapshot);
        ASSERT_EQ(capture.GetSpikes().size(), 1u);
        EXPECT_EQ(capture.GetSpikes()[0].frameIndex, 102u);
    }

    TEST(SpikeCaptureTest, TheListKeepsTheLastK) {
        SpikeCapture capture;
        capture.SetSettings(AbsoluteSettings(20.0, 2));
        RingSnapshot snapshot = SteadyFrames(0, 1);
        for (uint64_t index = 1; index <= 4; ++index) {
            AddFrame(snapshot, index, 30);
        }
        capture.Update(snapshot);
        ASSERT_EQ(capture.GetSpikes().size(), 2u);
        EXPECT_EQ(capture.GetSpikes()[0].frameIndex, 3u);
        EXPECT_EQ(capture.GetSpikes()[1].frameIndex, 4u);

        SpikeSettings smaller = capture.GetSettings();
        smaller.keep = 1;
        capture.SetSettings(smaller);
        ASSERT_EQ(capture.GetSpikes().size(), 1u);
        EXPECT_EQ(capture.GetSpikes()[0].frameIndex, 4u);
    }

    TEST(SpikeCaptureTest, AFrameIsJudgedOnceAcrossOverlappingSnapshots) {
        SpikeCapture capture;
        capture.SetSettings(AbsoluteSettings(50.0));
        RingSnapshot first = SteadyFrames(0, 2);
        AddFrame(first, 2, 80);
        capture.Update(first);
        ASSERT_EQ(capture.GetSpikes().size(), 1u);

        RingSnapshot overlapping = first;  // the same frames again, and one more
        AddFrame(overlapping, 3, 90);
        capture.Update(overlapping);
        capture.Update(overlapping);
        ASSERT_EQ(capture.GetSpikes().size(), 2u);
        EXPECT_EQ(capture.GetSpikes()[0].frameIndex, 2u);
        EXPECT_EQ(capture.GetSpikes()[1].frameIndex, 3u);
    }

    TEST(SpikeCaptureTest, AFrameTheRingNoLongerHasIsSkipped) {
        SpikeCapture capture;
        capture.SetSettings(AbsoluteSettings(50.0));
        RingSnapshot first = SteadyFrames(0, 2);
        capture.Update(first);
        RingSnapshot later = SteadyFrames(10, 1);  // frames 2..9 were overwritten before the next Update
        AddFrame(later, 11, 80);
        capture.Update(later);
        ASSERT_EQ(capture.GetSpikes().size(), 1u);
        EXPECT_EQ(capture.GetSpikes()[0].frameIndex, 11u);
    }

    TEST(SpikeCaptureTest, ClearForgetsTheSpikesButNotTheFramesAlreadyJudged) {
        SpikeCapture capture;
        capture.SetSettings(AbsoluteSettings(50.0));
        const RingSnapshot snapshot = WithASpike(80);
        capture.Update(snapshot);
        ASSERT_EQ(capture.GetSpikes().size(), 1u);
        capture.Clear();
        EXPECT_TRUE(capture.GetSpikes().empty());
        capture.Update(snapshot);
        EXPECT_TRUE(capture.GetSpikes().empty());
    }

    TEST(SpikeCaptureTest, ChangingTheSettingsDoesNotJudgeSeenFramesAgain) {
        SpikeCapture capture;
        capture.SetSettings(AbsoluteSettings(500.0));
        const RingSnapshot snapshot = WithASpike(80);
        capture.Update(snapshot);
        EXPECT_TRUE(capture.GetSpikes().empty());
        capture.SetSettings(AbsoluteSettings(10.0));
        capture.Update(snapshot);
        EXPECT_TRUE(capture.GetSpikes().empty());
    }

    TEST(SpikeCaptureTest, TheZoneCapSetsTruncatedZones) {
        SpikeCapture capture;
        capture.SetSettings(AbsoluteSettings(50.0));
        RingSnapshot snapshot = SteadyFrames(0, 1);
        AddFrame(snapshot, 1, 80);
        const uint64_t begin = snapshot.frames.back().beginTicks;
        constexpr size_t kExtra = 7;
        for (size_t i = 0; i < SpikeCapture::kMaxFrozenZones + kExtra; ++i) {
            AddZone(snapshot, kOuterSite, begin + 1, begin + 2);
        }
        capture.Update(snapshot);
        ASSERT_EQ(capture.GetSpikes().size(), 1u);
        EXPECT_EQ(capture.GetSpikes()[0].threads[0].zones.size(), SpikeCapture::kMaxFrozenZones);
        EXPECT_EQ(capture.GetSpikes()[0].truncatedZones, kExtra);
    }

    TEST(SpikeCaptureTest, ToSnapshotIsOneFrameWithTheSameZones) {
        SpikeCapture capture;
        capture.SetSettings(AbsoluteSettings(50.0));
        capture.Update(WithASpike(80));
        const FrozenFrame& spike = capture.GetSpikes()[0];
        const RingSnapshot snapshot = ToSnapshot(spike);
        ASSERT_EQ(snapshot.frames.size(), 1u);
        EXPECT_EQ(snapshot.frames[0].index, 3u);
        EXPECT_EQ(snapshot.frames[0].beginTicks, spike.beginTicks);
        EXPECT_EQ(snapshot.frames[0].endTicks, spike.endTicks);
        EXPECT_NEAR(snapshot.nanosecondsPerTick, kNanosecondsPerTestTick, kTolerance);
        ASSERT_EQ(snapshot.threads.size(), 1u);
        ASSERT_EQ(snapshot.threads[0].zones.size(), 2u);
        EXPECT_EQ(snapshot.threads[0].zones[1].depth, 1u);
    }

    TEST(SpikeCaptureTest, FreezeFrameKeepsAFrameThatIsNotASpike) {
        const RingSnapshot snapshot = WithASpike(80);
        const FrozenFrame steady = FreezeFrame(snapshot, snapshot.frames[1], 0.0);
        EXPECT_EQ(steady.frameIndex, 1u);
        EXPECT_NEAR(steady.frameMs, static_cast<double>(kSteadyFrameTicks), kTolerance);
        EXPECT_NEAR(steady.nanosecondsPerTick, kNanosecondsPerTestTick, kTolerance);
        EXPECT_TRUE(steady.threads.empty());  // the zones of WithASpike are in its spike frame

        const FrozenFrame spike = FreezeFrame(snapshot, snapshot.frames[3], 16.0);
        EXPECT_EQ(spike.frameIndex, 3u);
        EXPECT_NEAR(spike.medianMs, 16.0, kTolerance);
        ASSERT_EQ(spike.threads.size(), 1u);
        EXPECT_EQ(spike.threads[0].zones.size(), 2u);
        EXPECT_EQ(spike.truncatedZones, 0u);
    }

    TEST(FrozenTraceTest, TheJsonParsesAndEveryBeginHasItsEnd) {
        SpikeCapture capture;
        capture.SetSettings(AbsoluteSettings(50.0));
        RingSnapshot snapshot = WithASpike(80);
        AddFrame(snapshot, 4, 90);
        const uint64_t begin = snapshot.frames.back().beginTicks;
        AddZone(snapshot, kOuterSite, begin + 3, begin + 4);
        capture.Update(snapshot);
        ASSERT_EQ(capture.GetSpikes().size(), 2u);

        const Json trace = ParseTrace(WriteFrozenTraceJson(capture.GetSpikes()));
        const Json events = EventsOf(trace);
        Map<int64_t, int> depth;
        size_t begins = 0;
        size_t ends = 0;
        size_t threadNames = 0;
        size_t frameMarks = 0;
        double lastTs = -1.0;
        for (const Json& event : events) {
            const String phase = Member(event, "ph");
            const int64_t tid = GetJsonInt(event, "tid").value_or(-1);
            if (phase == "B" || phase == "E") {
                const double ts = GetJsonNumber(event, "ts").value_or(-1.0);
                EXPECT_GE(ts, lastTs) << "events of a thread are in time order";
                lastTs = ts;
                depth[tid] += phase == "B" ? 1 : -1;
                EXPECT_GE(depth[tid], 0);
                begins += phase == "B" ? 1 : 0;
                ends += phase == "E" ? 1 : 0;
            } else if (phase == "M" && Member(event, "name") == "thread_name") {
                ++threadNames;
            } else if (phase == "i") {
                ++frameMarks;
            }
        }
        EXPECT_EQ(begins, 3u);
        EXPECT_EQ(ends, begins);
        EXPECT_EQ(depth[1], 0);
        EXPECT_EQ(threadNames, 1u);
        EXPECT_EQ(frameMarks, 2u);
        const Json* other = FindJsonMember(trace, "otherData");
        ASSERT_NE(other, nullptr);
        EXPECT_EQ(GetJsonInt(*other, "frozenFrames"), 2);
    }

    TEST(FrozenTraceTest, TheNamesAreEscapedAndTheTimeStartsAtTheEarliestFrame) {
        SpikeCapture capture;
        capture.SetSettings(AbsoluteSettings(50.0));
        capture.Update(WithASpike(80));
        const Json events = EventsOf(ParseTrace(WriteFrozenTraceJson(capture.GetSpikes())));
        bool foundInner = false;
        double firstTs = -1.0;
        for (const Json& event : events) {
            if (Member(event, "ph") == "B") {
                if (firstTs < 0.0) {
                    firstTs = GetJsonNumber(event, "ts").value_or(-1.0);
                }
                if (Member(event, "name") == "Inner \"quoted\"\\") {
                    foundInner = true;
                }
            }
        }
        EXPECT_TRUE(foundInner);
        EXPECT_NEAR(firstTs, 1000.0, 1.0e-6);  // Outer begins one 1-ms tick after the frame: 1000 microseconds
    }

    TEST(FrozenTraceTest, AnEmptyListIsAValidTrace) {
        const Json trace = ParseTrace(WriteFrozenTraceJson({}));
        EXPECT_TRUE(EventsOf(trace).empty());
        const Json* other = FindJsonMember(trace, "otherData");
        ASSERT_NE(other, nullptr);
        EXPECT_EQ(GetJsonInt(*other, "frozenFrames"), 0);
    }

    TEST(FrozenTraceTest, TheFileHoldsTheSameTextAndAnUnwritablePathIsAnError) {
        SpikeCapture capture;
        capture.SetSettings(AbsoluteSettings(50.0));
        capture.Update(WithASpike(80));

        const std::filesystem::path path = TestTempPath("frozen_trace").replace_extension(".json");
        const auto written = WriteFrozenTraceFile(StringView(path.string().c_str()), capture.GetSpikes());
        ASSERT_TRUE(written.has_value());
        std::ifstream in(path, std::ios::binary);
        std::stringstream content;
        content << in.rdbuf();
        EXPECT_EQ(FromStdString(content.str()), WriteFrozenTraceJson(capture.GetSpikes()));
        in.close();
        std::filesystem::remove(path);

        const auto refused = WriteFrozenTraceFile(StringView((path / "no" / "such" / "dir.json").string().c_str()), capture.GetSpikes());
        ASSERT_FALSE(refused.has_value());
        EXPECT_NE(refused.error().find("cannot write"), String::npos);
    }

}  // namespace BECore::Tests
