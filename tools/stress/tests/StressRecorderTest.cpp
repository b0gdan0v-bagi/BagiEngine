#include <BECore/Control/ControlParams.h>
#include <Common/JsonDoc.h>
#include <Stress/StressRecorder.h>
#include <gtest/gtest.h>
#include <regex>

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

        constexpr const char* kKeys[] = {"allocBytesPerFrame", "allocTracking", "allocsPerFrame", "atlases",         "batching",   "count",        "cpuMs",       "cpuMsP50",
                                         "cpuMsP99",           "drawCalls",     "drawMs",         "drawMsP50",       "frameMsP50", "frameMsP99",   "frames",      "gpuMs",
                                         "gpuMsP50",           "gpuMsP99",      "interleave",     "manualClock",     "presentMs",  "presentMsP50", "primitives",  "scene",
                                         "spriteSize",         "submitMs",      "submitMsP50",    "textureSwitches", "triangles",  "updateMs",     "updateMsP50", "waitMs",
                                         "waitMsP50",          "warmup",        "workMs",         "workMsP50",       "workMsP99"};

        StressRunContext Context() {
            StressRunContext context;
            context.scene = StressScene::Ui;
            context.count = 300;
            context.warmup = 30;
            context.interleave = true;
            context.atlases = 8;
            context.manualClock = true;
            context.allocTracking = false;
            context.load.spriteSize = 256;
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
        a.primitives = 40;
        a.triangles = 100;
        a.textureSwitches = 4;
        a.allocCount = 6;
        a.allocBytes = 600;
        StressSample b;
        b.cpuMs = 4.0;
        b.workMs = 3.0;
        b.drawCalls = 20;
        b.primitives = 60;
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
        EXPECT_DOUBLE_EQ(summary.primitives, 50.0);
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

    TEST(StressRecorderTest, TheWorkTimeAlsoLeavesOutTheWait) {
        FrameStats stats;
        stats.cpuMs = 20.0;
        stats.stageMs[+FrameStage::Present] = 4.0;
        stats.stageMs[+FrameStage::Wait] = 9.0;
        EXPECT_DOUBLE_EQ(SampleOf(stats).workMs, 7.0);
    }

    TEST(StressRecorderTest, TheStagesAreReadFromTheStatistics) {
        FrameStats stats;
        stats.cpuMs = 40.0;
        stats.stageMs[+FrameStage::Update] = 1.0;
        stats.stageMs[+FrameStage::SceneDraw] = 10.0;
        stats.stageMs[+FrameStage::Submit] = 3.0;
        stats.stageMs[+FrameStage::Present] = 4.0;
        stats.stageMs[+FrameStage::Wait] = 5.0;
        stats.gpuMs = 6.0;
        const StressSample sample = SampleOf(stats);
        EXPECT_DOUBLE_EQ(sample.updateMs, 1.0);
        EXPECT_DOUBLE_EQ(sample.drawMs, 7.0) << "the submissions are inside the SceneDraw stage and are not counted twice";
        EXPECT_DOUBLE_EQ(sample.submitMs, 3.0);
        EXPECT_DOUBLE_EQ(sample.presentMs, 4.0);
        EXPECT_DOUBLE_EQ(sample.waitMs, 5.0);
        EXPECT_DOUBLE_EQ(sample.gpuMs, 6.0);

        stats.stageMs[+FrameStage::Submit] = 12.0;
        EXPECT_EQ(SampleOf(stats).drawMs, 0.0) << "submissions outside the draw pass can be longer than it";
    }

    TEST(StressRecorderTest, EveryStageHasItsMeanAndItsMedian) {
        StressRecorder recorder;
        recorder.Reserve(3);
        for (const double ms : {1.0, 2.0, 9.0}) {
            StressSample sample;
            sample.updateMs = ms;
            sample.drawMs = ms * 2;
            sample.submitMs = ms * 3;
            sample.presentMs = ms * 4;
            sample.waitMs = ms * 5;
            sample.gpuMs = ms * 6;
            recorder.Add(sample);
        }

        const StressSummary summary = recorder.Summarize();
        EXPECT_DOUBLE_EQ(summary.stages.updateMs, 4.0);
        EXPECT_DOUBLE_EQ(summary.stages.drawMs, 8.0);
        EXPECT_DOUBLE_EQ(summary.stages.submitMs, 12.0);
        EXPECT_DOUBLE_EQ(summary.stages.presentMs, 16.0);
        EXPECT_DOUBLE_EQ(summary.stages.waitMs, 20.0);
        EXPECT_DOUBLE_EQ(summary.stages.gpuMs, 24.0);
        EXPECT_DOUBLE_EQ(summary.stagesP50.updateMs, 2.0);
        EXPECT_DOUBLE_EQ(summary.stagesP50.drawMs, 4.0);
        EXPECT_DOUBLE_EQ(summary.stagesP50.submitMs, 6.0);
        EXPECT_DOUBLE_EQ(summary.stagesP50.presentMs, 8.0);
        EXPECT_DOUBLE_EQ(summary.stagesP50.waitMs, 10.0);
        EXPECT_DOUBLE_EQ(summary.stagesP50.gpuMs, 12.0);
        EXPECT_DOUBLE_EQ(summary.gpuMsP99, 54.0);
    }

    TEST(StressRecorderTest, TheGpuStatisticsLeaveOutTheFramesWithoutAGpuTime) {
        StressRecorder recorder;
        recorder.Reserve(6);
        for (const double ms : {0.0, 8.0, 0.0, 0.0, 4.0, 0.0}) {
            StressSample sample;
            sample.gpuMs = ms;
            recorder.Add(sample);
        }

        const StressSummary summary = recorder.Summarize();
        EXPECT_DOUBLE_EQ(summary.stages.gpuMs, 6.0);
        EXPECT_DOUBLE_EQ(summary.stagesP50.gpuMs, 4.0);
        EXPECT_DOUBLE_EQ(summary.gpuMsP99, 8.0);
    }

    TEST(StressRecorderTest, ARunWithoutAnyGpuTimeReportsZero) {
        const StressSummary summary = RecorderOf(3).Summarize();
        EXPECT_EQ(summary.stages.gpuMs, 0.0);
        EXPECT_EQ(summary.stagesP50.gpuMs, 0.0);
        EXPECT_EQ(summary.gpuMsP99, 0.0);
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
        const BECore::Tests::JsonDoc parsed(line);
        ASSERT_TRUE(parsed.IsParsed()) << line.c_str();
        ASSERT_TRUE(parsed.Root().IsObject());
        EXPECT_EQ(parsed.Root().Size(), std::size(kKeys));
        for (const char* key : kKeys) {
            EXPECT_TRUE(parsed.Root().Find(key).has_value()) << key;
        }

        const Vector<String> written = KeysInOrder(line);
        ASSERT_EQ(written.size(), std::size(kKeys));
        for (size_t i = 0; i < written.size(); ++i) {
            EXPECT_EQ(written[i], kKeys[i]) << "the keys are written sorted";
        }
    }

    TEST(StressRecorderTest, EveryStageKeyOfTheLineHoldsItsOwnStatistic) {
        StressSummary summary;
        summary.stages = {.updateMs = 1.0, .drawMs = 2.0, .submitMs = 3.0, .presentMs = 4.0, .waitMs = 5.0, .gpuMs = 6.0};
        summary.stagesP50 = {.updateMs = 11.0, .drawMs = 12.0, .submitMs = 13.0, .presentMs = 14.0, .waitMs = 15.0, .gpuMs = 16.0};
        summary.gpuMsP99 = 26.0;

        const BECore::Tests::JsonDoc parsed(FormatJsonLine(summary, Context()));

        ASSERT_TRUE(parsed.IsParsed());
        constexpr const char* kNames[] = {"updateMs", "drawMs", "submitMs", "presentMs", "waitMs", "gpuMs"};
        for (size_t i = 0; i < std::size(kNames); ++i) {
            EXPECT_DOUBLE_EQ(GetParamNumber(parsed.Root(), kNames[i]).value_or(0.0), i + 1.0) << kNames[i];
            const String median = Format("{}P50", kNames[i]);
            EXPECT_DOUBLE_EQ(GetParamNumber(parsed.Root(), median.c_str()).value_or(0.0), i + 11.0) << median.c_str();
        }
        EXPECT_DOUBLE_EQ(GetParamNumber(parsed.Root(), "gpuMsP99").value_or(0.0), 26.0);
    }

    TEST(StressRecorderTest, TheLineCarriesTheRunAndTheSummary) {
        const BECore::Tests::JsonDoc parsed(FormatJsonLine(RecorderOf(100).Summarize(), Context()));
        ASSERT_TRUE(parsed.IsParsed());
        EXPECT_EQ(GetParamString(parsed.Root(), "scene").value_or(""), "ui");
        EXPECT_EQ(GetParamInt(parsed.Root(), "count").value_or(0), 300);
        EXPECT_EQ(GetParamInt(parsed.Root(), "frames").value_or(0), 100);
        EXPECT_EQ(GetParamInt(parsed.Root(), "warmup").value_or(0), 30);
        EXPECT_EQ(GetParamInt(parsed.Root(), "atlases").value_or(0), 8);
        EXPECT_EQ(GetParamInt(parsed.Root(), "spriteSize").value_or(0), 256);
        EXPECT_EQ(GetParamInt(parsed.Root(), "batching").value_or(-1), 1);
        EXPECT_EQ(GetParamBool(parsed.Root(), "interleave"), Optional(true));
        EXPECT_EQ(GetParamBool(parsed.Root(), "manualClock"), Optional(true));
        EXPECT_EQ(GetParamBool(parsed.Root(), "allocTracking"), Optional(false));
        EXPECT_DOUBLE_EQ(GetParamNumber(parsed.Root(), "workMsP50").value_or(0.0), 50.0);
        EXPECT_DOUBLE_EQ(GetParamNumber(parsed.Root(), "workMsP99").value_or(0.0), 99.0);
        EXPECT_DOUBLE_EQ(GetParamNumber(parsed.Root(), "workMs").value_or(0.0), 50.5);
    }

    TEST(StressRecorderTest, ARunWithoutSamplesStillGivesAValidLine) {
        const BECore::Tests::JsonDoc parsed(FormatJsonLine(StressRecorder().Summarize(), StressRunContext{}));
        ASSERT_TRUE(parsed.IsParsed());
        EXPECT_EQ(GetParamInt(parsed.Root(), "frames").value_or(-1), 0);
        EXPECT_DOUBLE_EQ(GetParamNumber(parsed.Root(), "workMs").value_or(-1.0), 0.0);
    }

}  // namespace BECore::Stress::Tests
