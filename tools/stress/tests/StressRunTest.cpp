#include "StressSceneFixture.h"

#include <BECore/Control/Json.h>
#include <Events/ApplicationEvents.h>
#include <Stress/Components/StressRunComponent.h>
#include <Stress/RampController.h>
#include <gtest/gtest.h>

/**
 * Coverage of StressRunComponent's fixed mode on the shipped scene file with a clock the test moves: which frames are recorded (warm-up and
 * the building frame are not, a change restarts the warm-up), exactly `frames` samples, one line, one quit, and a plain run that measures nothing.
 */
namespace BECore::Stress::Tests {

    namespace {

        constexpr uint64_t kNanosecondsPerMs = 1000000;
        constexpr uint32_t kCount = 10;
        constexpr uint32_t kWarmup = 3;
        constexpr uint32_t kFrames = 5;
        constexpr double kSlowMs = 100.0;  // the cost of a frame that must not be measured
        constexpr double kFastMs = 2.0;

        uint64_t g_fakeNow = 0;
        uint64_t FakeNow() {
            return g_fakeNow;
        }

        /// Counts the QuitEvents the run emits
        class QuitCounter : public SubscriptionHolder {
        public:
            using SubscriptionHolder::Subscribe;

            int count = 0;

            void OnQuit(const ApplicationEvents::QuitEvent&) {
                ++count;
            }
        };

    }  // namespace

    class StressRunTest : public StressSceneFixture {
    protected:
        void SetUp() override {
            g_fakeNow = 0;
            CoreManager::GetFrameStats().SetClock(FakeNow);
            _quit.Subscribe<ApplicationEvents::QuitEvent, &QuitCounter::OnQuit>(&_quit);
        }

        void TearDown() override {
            CoreManager::GetFrameStats().SetClock(nullptr);
            StressSceneFixture::TearDown();
        }

        /// A fixed run of the entities scene: nothing is drawn, so the only cost is the one the test gives the frame
        void StartRun(const Vector<String>& extra = {}) {
            Vector<String> arguments = {"--stress-scene=entities", Format("--stress-count={}", kCount), Format("--stress-frames={}", kFrames), Format("--stress-warmup={}", kWarmup)};
            arguments.insert(arguments.end(), extra.begin(), extra.end());
            StartGame(arguments);
            Run().SetLineSink([this](const String& line) { _lines.push_back(line); });
        }

        StressRunComponent& Run() const {
            for (const auto& node : GetScene("StressEntities").GetRootNode().GetChildren()) {
                if (node->GetName() == "Run"_intern) {
                    return *node->GetComponent<StressRunComponent>();
                }
            }
            ADD_FAILURE() << "no node Run";
            return *Field("StressEntities").GetComponent<StressRunComponent>();
        }

        /// One frame as Application::RunFrame makes it: opened, the scene updated inside it, closed with a present of @p presentMs and a total of @p cpuMs
        void Frame(double cpuMs, double presentMs = 0.0, bool held = false) {
            FrameStatsCollector& stats = CoreManager::GetFrameStats();
            stats.BeginFrame(held);
            Update();
            stats.BeginStage(FrameStage::Present);
            g_fakeNow += static_cast<uint64_t>(presentMs * kNanosecondsPerMs);
            stats.EndStage(FrameStage::Present);
            g_fakeNow += static_cast<uint64_t>((cpuMs - presentMs) * kNanosecondsPerMs);
            stats.EndFrame({}, {});
        }

        /// Frames of a cost that grows with the count on show (@p msPerObject for each object, @p presentMs of it in the present) until the run is done
        uint32_t RampFramesUntilDone(double msPerObject, double presentMs = 0.0, uint32_t limit = 5000) {
            uint32_t frames = 0;
            while (!Run().IsDone() && frames < limit) {
                const double cpuMs = msPerObject * StressGame::GetActive()->GetState().GetCount() + presentMs;
                Frame(cpuMs, presentMs);
                ++frames;
            }
            return frames;
        }

        /// A ramp run of the entities scene with @p extra flags
        void StartRamp(const Vector<String>& extra = {}) {
            Vector<String> arguments = {"--stress-scene=entities", "--stress-ramp"};
            arguments.insert(arguments.end(), extra.begin(), extra.end());
            StartGame(arguments);
            Run().SetLineSink([this](const String& line) { _lines.push_back(line); });
        }

        /// Frames of the same cost until the run is done or @p limit frames went
        uint32_t FramesUntilDone(double cpuMs, uint32_t limit = 100) {
            uint32_t frames = 0;
            while (!Run().IsDone() && frames < limit) {
                Frame(cpuMs);
                ++frames;
            }
            return frames;
        }

        Vector<String> _lines;
        QuitCounter _quit;
    };

    TEST_F(StressRunTest, TheWarmUpAndTheBuildingFrameAreNotRecorded) {
        StartRun();

        Frame(kSlowMs);  // frame 1 builds the nodes
        for (uint32_t i = 0; i < kWarmup; ++i) {
            Frame(kSlowMs);
        }
        EXPECT_EQ(Run().GetRecordedCount(), 0u);
        for (uint32_t i = 0; i < kFrames; ++i) {
            Frame(kFastMs);  // the sample of a frame is taken in the update of the next one
        }
        Frame(kFastMs);

        ASSERT_EQ(_lines.size(), 1u);
        const auto parsed = ParseJson(_lines[0]);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(GetJsonInt(*parsed, "frames").value_or(0), kFrames);
        EXPECT_DOUBLE_EQ(GetJsonNumber(*parsed, "workMsP99").value_or(0.0), kFastMs) << "no slow frame is among the samples";
        EXPECT_DOUBLE_EQ(GetJsonNumber(*parsed, "workMs").value_or(0.0), kFastMs);
    }

    TEST_F(StressRunTest, ExactlyTheAskedFramesAreRecordedThenOneLineAndOneQuit) {
        StartRun();

        const uint32_t frames = FramesUntilDone(kFastMs);

        EXPECT_EQ(frames, 1 + kWarmup + kFrames + 1) << "the building frame, the warm-up, the measured frames and the update that reads the last of them";
        ASSERT_EQ(_lines.size(), 1u);
        EXPECT_EQ(_quit.count, 1);
        EXPECT_TRUE(Run().IsDone());
        EXPECT_EQ(Run().GetRecordedCount(), size_t{kFrames});

        // The game quits at the end of the frame, but a frame or two more may still run: nothing more is printed or recorded
        Frame(kFastMs);
        Frame(kFastMs);
        EXPECT_EQ(_lines.size(), 1u);
        EXPECT_EQ(_quit.count, 1);
        EXPECT_EQ(Run().GetRecordedCount(), size_t{kFrames});
    }

    TEST_F(StressRunTest, TheLineIsTheRunsAndStartsWithABrace) {
        StartRun({"--stress-interleave", "--stress-atlases=2"});
        FramesUntilDone(kFastMs);

        ASSERT_EQ(_lines.size(), 1u);
        EXPECT_EQ(_lines[0].front(), '{');
        const auto parsed = ParseJson(_lines[0]);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(GetJsonString(*parsed, "scene").value_or(""), "entities");
        EXPECT_EQ(GetJsonInt(*parsed, "count").value_or(0), kCount);
        EXPECT_EQ(GetJsonInt(*parsed, "warmup").value_or(0), kWarmup);
        EXPECT_EQ(GetJsonBool(*parsed, "interleave"), Optional(true));
        EXPECT_EQ(GetJsonInt(*parsed, "atlases").value_or(0), 2);
        EXPECT_EQ(GetJsonBool(*parsed, "manualClock"), Optional(false));
    }

    TEST_F(StressRunTest, TheWorkTimeLeavesOutThePresent) {
        StartRun();

        for (uint32_t i = 0; i < 1 + kWarmup + kFrames + 1; ++i) {
            Frame(10.0, 7.0);
        }

        ASSERT_EQ(_lines.size(), 1u);
        const auto parsed = ParseJson(_lines[0]);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_DOUBLE_EQ(GetJsonNumber(*parsed, "workMs").value_or(0.0), 3.0);
        EXPECT_DOUBLE_EQ(GetJsonNumber(*parsed, "cpuMs").value_or(0.0), 10.0);
    }

    TEST_F(StressRunTest, ACountChangeRestartsTheWarmUp) {
        StartRun();
        Frame(kSlowMs);
        for (uint32_t i = 0; i < kWarmup + 2; ++i) {
            Frame(kFastMs);
        }
        const size_t before = Run().GetRecordedCount();
        EXPECT_GT(before, 0u) << "the run was measuring";

        SetCount(kCount * 2);
        Frame(kSlowMs);  // the frame that notices the change builds the nodes
        for (uint32_t i = 0; i < kWarmup; ++i) {
            Frame(kSlowMs);
        }
        EXPECT_EQ(Run().GetRecordedCount(), 0u) << "the samples of the old count are gone and the warm-up runs again";
        EXPECT_TRUE(_lines.empty());

        FramesUntilDone(kFastMs);
        ASSERT_EQ(_lines.size(), 1u);
        const auto parsed = ParseJson(_lines[0]);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(GetJsonInt(*parsed, "count").value_or(0), kCount * 2);
        EXPECT_EQ(GetJsonInt(*parsed, "frames").value_or(0), kFrames);
        EXPECT_LT(GetJsonNumber(*parsed, "workMsP99").value_or(1e9), kSlowMs) << "no frame of the warm-up is among the samples";
    }

    TEST_F(StressRunTest, AHeldFrameIsNotASample) {
        StartRun();
        Frame(kFastMs);
        for (uint32_t i = 0; i < kWarmup + 1; ++i) {
            Frame(kFastMs);
        }
        Frame(kSlowMs, 0.0, true);  // the game stood still
        Frame(kFastMs);
        Frame(kFastMs);

        EXPECT_EQ(Run().GetRecordedCount(), 2u) << "the held frame was left out";
        FramesUntilDone(kFastMs);
        const auto parsed = ParseJson(_lines[0]);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(GetJsonInt(*parsed, "frames").value_or(0), kFrames);
        EXPECT_DOUBLE_EQ(GetJsonNumber(*parsed, "workMsP99").value_or(0.0), kFastMs);
    }

    TEST_F(StressRunTest, AnUpdateWithoutANewFrameTakesNoSecondSample) {
        StartRun();
        Frame(kFastMs);
        for (uint32_t i = 0; i < kWarmup + 2; ++i) {
            Frame(kFastMs);
        }
        Update();  // reads the frame that was just closed
        const size_t recorded = Run().GetRecordedCount();
        ASSERT_GT(recorded, 0u);

        Update();  // the scene updates again with no frame closed in between
        Update();

        EXPECT_EQ(Run().GetRecordedCount(), recorded);
    }

    TEST_F(StressRunTest, APlainRunMeasuresNothing) {
        StartGame({"--stress-scene=entities", Format("--stress-count={}", kCount)});
        StressRunComponent& run = Run();
        run.SetLineSink([this](const String& line) { _lines.push_back(line); });

        for (uint32_t i = 0; i < 20; ++i) {
            Frame(kFastMs);
        }

        EXPECT_EQ(run.GetRecordedCount(), 0u);
        EXPECT_FALSE(run.IsDone());
        EXPECT_TRUE(_lines.empty());
        EXPECT_EQ(_quit.count, 0);
    }

    TEST_F(StressRunTest, TheCounterNamesTheObjectCountEveryFrame) {
        StartGame({"--stress-scene=entities", Format("--stress-count={}", kCount)});
        Frame(kFastMs);
        Frame(kFastMs);
        const auto objects = CoreManager::GetFrameStats().GetLast()->FindCounter("Stress objects"_intern);
        ASSERT_TRUE(objects.has_value());
        EXPECT_DOUBLE_EQ(*objects, kCount);

        SetCount(kCount * 3);
        Frame(kFastMs);
        EXPECT_DOUBLE_EQ(*CoreManager::GetFrameStats().GetLast()->FindCounter("Stress objects"_intern), kCount * 3);
    }

    TEST_F(StressRunTest, AGameThatIsNotActiveRecordsNothing) {
        StartRun();
        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(StressGame::GetActive(), nullptr);

        for (uint32_t i = 0; i < 20; ++i) {
            Frame(kFastMs);
        }

        EXPECT_EQ(Run().GetRecordedCount(), 0u);
        EXPECT_TRUE(_lines.empty());
    }

    TEST_F(StressRunTest, TheRampStartsAtItsOwnCountWhateverTheCountFlagSays) {
        StartRamp({"--stress-count=5000"});

        EXPECT_EQ(StressGame::GetActive()->GetState().GetCount(), kRampStart);
        Update();
        EXPECT_EQ(Field("StressEntities").GetChildren().size(), size_t{kRampStart}) << "the big count is never built first";
    }

    TEST_F(StressRunTest, TheRampFindsTheLimitPrintsOneLineAndQuitsOnce) {
        constexpr double kMsPerObject = 0.055;  // 16.6 / 0.055 = 301
        StartRamp();

        const uint32_t frames = RampFramesUntilDone(kMsPerObject);

        EXPECT_LT(frames, 5000u) << "the ramp ended";
        ASSERT_EQ(_lines.size(), 1u);
        EXPECT_EQ(_quit.count, 1);
        const auto parsed = ParseJson(_lines[0]);
        ASSERT_TRUE(parsed.has_value()) << _lines[0].c_str();
        EXPECT_EQ(GetJsonString(*parsed, "mode").value_or(""), "ramp");
        EXPECT_EQ(GetJsonString(*parsed, "scene").value_or(""), "entities");
        EXPECT_EQ(GetJsonBool(*parsed, "capped"), Optional(false));
        EXPECT_EQ(GetJsonBool(*parsed, "aborted"), Optional(false));
        EXPECT_EQ(GetJsonBool(*parsed, "verified"), Optional(true));
        const int64_t found = GetJsonInt(*parsed, "objectsAt60fps").value_or(0);
        EXPECT_LE(found, 301);
        EXPECT_GE(found, 290);

        Frame(kFastMs);
        Frame(kFastMs);
        EXPECT_EQ(_lines.size(), 1u);
        EXPECT_EQ(_quit.count, 1);
    }

    TEST_F(StressRunTest, TheRampHoldsToTheWorkTimeUnlessTheCpuTimeIsAsked) {
        // A frame whose present alone is over the budget: the work time (cpu without the present) is what the ramp judges by default
        StartRamp();
        RampFramesUntilDone(0.001, 50.0);
        ASSERT_EQ(_lines.size(), 1u);
        auto parsed = ParseJson(_lines[0]);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_GT(GetJsonInt(*parsed, "objectsAt60fps").value_or(0), int64_t{kRampStart}) << "the present does not count";
        EXPECT_EQ(GetJsonString(*parsed, "measure").value_or(""), "work");
    }

    TEST_F(StressRunTest, TheCpuMeasureCountsThePresentToo) {
        StartRamp({"--stress-measure=cpu"});
        RampFramesUntilDone(0.001, 50.0);
        ASSERT_EQ(_lines.size(), 1u);
        const auto parsed = ParseJson(_lines[0]);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(GetJsonInt(*parsed, "objectsAt60fps").value_or(-1), 0) << "a present of 50 ms is over the budget at any count";
        EXPECT_EQ(GetJsonString(*parsed, "measure").value_or(""), "cpu");
    }

    TEST_F(StressRunTest, TheRampWinsOverTheFramesFlag) {
        StartRamp({"--stress-frames=5", "--stress-warmup=0"});
        RampFramesUntilDone(0.055);

        ASSERT_EQ(_lines.size(), 1u);
        EXPECT_NE(_lines[0].find("\"mode\":\"ramp\""), String::npos);
        EXPECT_EQ(_quit.count, 1);
    }

    TEST_F(StressRunTest, AHeldFrameDoesNotFeedTheRamp) {
        StartRamp();
        for (uint32_t i = 0; i < 100; ++i) {
            Frame(kSlowMs, 0.0, true);  // the game stood still: nothing of it is a measure
        }
        EXPECT_FALSE(Run().IsDone());
        EXPECT_EQ(StressGame::GetActive()->GetState().GetCount(), kRampStart);
    }

}  // namespace BECore::Stress::Tests
