#include <BECore/Control/Json.h>
#include <Stress/StressRecorder.h>
#include <gtest/gtest.h>
#include <regex>

/**
 * Coverage of the recorder: the nearest-rank percentiles by hand, the means, a buffer that never grows, and the one JSON line (it parses,
 * has every key, in sorted order, with plain decimals).
 */
namespace BECore::Stress::Tests {

    namespace {

        /// The values 1, 2, ..., n as work times, the other fields 0
        StressRecorder RecorderOf(uint32_t n) {
            StressRecorder recorder;
            recorder.Reserve(n);
            for (uint32_t i = 1; i <= n; ++i) {
                StressSample sample;
                sample.workMs = i;
                sample.cpuMs = i;
                sample.frameMs = i;
                EXPECT_TRUE(recorder.Add(sample));
            }
            return recorder;
        }

        constexpr const char* kKeys[] = {"allocBytesPerFrame", "allocTracking", "allocsPerFrame", "atlases",   "count",      "cpuMs",       "cpuMsP50", "cpuMsP99",
                                         "drawCalls",          "frameMsP50",    "frameMsP99",     "frames",    "interleave", "manualClock", "scene",    "textureSwitches",
                                         "triangles",          "warmup",        "workMs",         "workMsP50", "workMsP99"};

        StressRunContext Context() {
            StressRunContext context;
            context.scene = StressScene::Ui;
            context.count = 300;
            context.warmup = 30;
            context.interleave = true;
            context.atlases = 8;
            context.manualClock = true;
            context.allocTracking = false;
            return context;
        }

        /// The keys of a one-level JSON line in the order they are written
        Vector<String> KeysInOrder(const String& line) {
            Vector<String> keys;
            const std::regex key("\"([A-Za-z0-9]+)\":");
            for (auto it = std::cregex_iterator(line.data(), line.data() + line.size(), key); it != std::cregex_iterator(); ++it) {
                keys.emplace_back((*it)[1].first, (*it)[1].second);
            }
            return keys;
        }

    }  // namespace

    TEST(StressRecorderTest, NearestRankOfOneValueIsEveryPercentile) {
        const double values[] = {7.5};
        EXPECT_EQ(NearestRank(values, 0.0), 7.5);
        EXPECT_EQ(NearestRank(values, 0.5), 7.5);
        EXPECT_EQ(NearestRank(values, 0.99), 7.5);
        EXPECT_EQ(NearestRank(values, 1.0), 7.5);
    }

    TEST(StressRecorderTest, NearestRankOfTwoValues) {
        const double values[] = {1.0, 2.0};
        EXPECT_EQ(NearestRank(values, 0.5), 1.0) << "ceil(0.5 * 2) = 1: the first value";
        EXPECT_EQ(NearestRank(values, 0.51), 2.0);
        EXPECT_EQ(NearestRank(values, 0.99), 2.0);
    }

    TEST(StressRecorderTest, NearestRankOfAHundredAndOneValues) {
        const StressSummary hundred = RecorderOf(100).Summarize();
        EXPECT_EQ(hundred.workMsP50, 50.0) << "ceil(50) = 50: the 50th value, not the 51st";
        EXPECT_EQ(hundred.workMsP99, 99.0);
        const StressSummary hundredAndOne = RecorderOf(101).Summarize();
        EXPECT_EQ(hundredAndOne.workMsP50, 51.0) << "ceil(50.5) = 51";
        EXPECT_EQ(hundredAndOne.workMsP99, 100.0) << "ceil(99.99) = 100";
    }

    TEST(StressRecorderTest, NearestRankOfNothingIsZero) {
        EXPECT_EQ(NearestRank({}, 0.5), 0.0);
        EXPECT_EQ(StressRecorder().Summarize().frames, 0u);
    }

    TEST(StressRecorderTest, AnUnsortedRunIsSortedForThePercentiles) {
        StressRecorder recorder;
        recorder.Reserve(5);
        for (const double ms : {5.0, 1.0, 4.0, 2.0, 3.0}) {
            StressSample sample;
            sample.workMs = ms;
            sample.cpuMs = ms;
            sample.frameMs = ms;
            recorder.Add(sample);
        }
        const StressSummary summary = recorder.Summarize();
        EXPECT_EQ(summary.workMsP50, 3.0);
        EXPECT_EQ(summary.workMsP99, 5.0);
        EXPECT_EQ(summary.cpuMsP50, 3.0);
        EXPECT_EQ(summary.frameMsP99, 5.0);
    }

    TEST(StressRecorderTest, TheMeansAreTheMeansOfTheSamples) {
        StressRecorder recorder;
        recorder.Reserve(2);
        StressSample a;
        a.cpuMs = 2.0;
        a.workMs = 1.0;
        a.drawCalls = 10;
        a.triangles = 100;
        a.textureSwitches = 4;
        a.allocCount = 6;
        a.allocBytes = 600;
        StressSample b;
        b.cpuMs = 4.0;
        b.workMs = 3.0;
        b.drawCalls = 20;
        b.triangles = 300;
        b.textureSwitches = 0;
        b.allocCount = 0;
        b.allocBytes = 0;
        recorder.Add(a);
        recorder.Add(b);

        const StressSummary summary = recorder.Summarize();
        EXPECT_EQ(summary.frames, 2u);
        EXPECT_DOUBLE_EQ(summary.cpuMs, 3.0);
        EXPECT_DOUBLE_EQ(summary.workMs, 2.0);
        EXPECT_DOUBLE_EQ(summary.drawCalls, 15.0);
        EXPECT_DOUBLE_EQ(summary.triangles, 200.0);
        EXPECT_DOUBLE_EQ(summary.textureSwitches, 2.0);
        EXPECT_DOUBLE_EQ(summary.allocsPerFrame, 3.0);
        EXPECT_DOUBLE_EQ(summary.allocBytesPerFrame, 300.0);
    }

    TEST(StressRecorderTest, MoreSamplesThanReservedAreIgnoredNotGrown) {
        StressRecorder recorder;
        recorder.Reserve(3);
        const StressSample sample;
        EXPECT_TRUE(recorder.Add(sample));
        EXPECT_TRUE(recorder.Add(sample));
        EXPECT_TRUE(recorder.Add(sample));
        EXPECT_FALSE(recorder.Add(sample));
        EXPECT_EQ(recorder.Size(), 3u);
        EXPECT_EQ(recorder.Capacity(), 3u);
        EXPECT_EQ(recorder.Summarize().frames, 3u);
    }

    TEST(StressRecorderTest, ReserveStartsAgain) {
        StressRecorder recorder = RecorderOf(10);
        recorder.Reserve(4);
        EXPECT_EQ(recorder.Size(), 0u);
        EXPECT_EQ(recorder.Capacity(), 4u);
    }

    TEST(StressRecorderTest, TheWorkTimeIsTheCpuTimeWithoutThePresent) {
        FrameStats stats;
        stats.cpuMs = 10.0;
        stats.frameMs = 12.0;
        stats.stageMs[+FrameStage::Present] = 7.5;
        stats.draws.drawCalls = 3;
        stats.draws.triangles = 6;
        stats.draws.textureSwitches = 2;
        stats.allocCount = 5;
        stats.allocBytes = 50;
        const StressSample sample = SampleOf(stats);
        EXPECT_DOUBLE_EQ(sample.workMs, 2.5);
        EXPECT_DOUBLE_EQ(sample.cpuMs, 10.0);
        EXPECT_DOUBLE_EQ(sample.frameMs, 12.0);
        EXPECT_EQ(sample.drawCalls, 3u);
        EXPECT_EQ(sample.triangles, 6u);
        EXPECT_EQ(sample.textureSwitches, 2u);
        EXPECT_EQ(sample.allocCount, 5u);
        EXPECT_EQ(sample.allocBytes, 50u);

        stats.stageMs[+FrameStage::Present] = 11.0;
        EXPECT_EQ(SampleOf(stats).workMs, 0.0) << "a present longer than the frame (a clock that stepped) is not a negative work time";
    }

    TEST(StressRecorderTest, NumbersArePlainDecimalsAndNeverNanOrInf) {
        EXPECT_EQ(FormatNumber(12.3456789), "12.3457");
        EXPECT_EQ(FormatNumber(0.00001), "0.0000") << "never `1e-05`";
        EXPECT_EQ(FormatNumber(2.0), "2.0000");
        EXPECT_EQ(FormatNumber(std::numeric_limits<double>::quiet_NaN()), "0");
        EXPECT_EQ(FormatNumber(std::numeric_limits<double>::infinity()), "0");
    }

    TEST(StressRecorderTest, TheLineIsOneJsonObjectWithEveryKeyInSortedOrder) {
        const String line = FormatJsonLine(RecorderOf(100).Summarize(), Context());

        EXPECT_EQ(line.find('\n'), String::npos);
        EXPECT_EQ(line.front(), '{');
        EXPECT_EQ(line.back(), '}');
        const auto parsed = ParseJson(line);
        ASSERT_TRUE(parsed.has_value()) << line.c_str();
        ASSERT_TRUE(parsed->is_object());
        EXPECT_EQ(parsed->size(), std::size(kKeys));
        for (const char* key : kKeys) {
            EXPECT_NE(FindJsonMember(*parsed, key), nullptr) << key;
        }

        const Vector<String> written = KeysInOrder(line);
        ASSERT_EQ(written.size(), std::size(kKeys));
        for (size_t i = 0; i < written.size(); ++i) {
            EXPECT_EQ(written[i], kKeys[i]) << "the keys are written sorted";
        }
    }

    TEST(StressRecorderTest, TheLineCarriesTheRunAndTheSummary) {
        const auto parsed = ParseJson(FormatJsonLine(RecorderOf(100).Summarize(), Context()));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(GetJsonString(*parsed, "scene").value_or(""), "ui");
        EXPECT_EQ(GetJsonInt(*parsed, "count").value_or(0), 300);
        EXPECT_EQ(GetJsonInt(*parsed, "frames").value_or(0), 100);
        EXPECT_EQ(GetJsonInt(*parsed, "warmup").value_or(0), 30);
        EXPECT_EQ(GetJsonInt(*parsed, "atlases").value_or(0), 8);
        EXPECT_EQ(GetJsonBool(*parsed, "interleave"), Optional(true));
        EXPECT_EQ(GetJsonBool(*parsed, "manualClock"), Optional(true));
        EXPECT_EQ(GetJsonBool(*parsed, "allocTracking"), Optional(false));
        EXPECT_DOUBLE_EQ(GetJsonNumber(*parsed, "workMsP50").value_or(0.0), 50.0);
        EXPECT_DOUBLE_EQ(GetJsonNumber(*parsed, "workMsP99").value_or(0.0), 99.0);
        EXPECT_DOUBLE_EQ(GetJsonNumber(*parsed, "workMs").value_or(0.0), 50.5);
    }

    TEST(StressRecorderTest, ARunWithoutSamplesStillGivesAValidLine) {
        const auto parsed = ParseJson(FormatJsonLine(StressRecorder().Summarize(), StressRunContext{}));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(GetJsonInt(*parsed, "frames").value_or(-1), 0);
        EXPECT_DOUBLE_EQ(GetJsonNumber(*parsed, "workMs").value_or(-1.0), 0.0);
    }

}  // namespace BECore::Stress::Tests
