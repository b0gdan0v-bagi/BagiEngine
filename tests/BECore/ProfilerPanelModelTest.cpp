#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Reflection/TypedRef.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/ProfilerTestSupport.h>
#include <Common/TempPath.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/Profiler/ProfilerPanelModel.h>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        constexpr ProfileSite kWorkSite{"Work", "Test", "ProfilerPanelModelTest.cpp", 1, 0};
        constexpr double kNanosecondsPerTestTick = 1.0e6;  // a test tick is one millisecond
        constexpr uint64_t kSteadyFrameTicks = 16;
        constexpr uint64_t kSpikeFrameTicks = 80;  // over the default threshold of 50 ms
        constexpr int kSteadyFrames = 8;

        constexpr size_t kProbeBytes = 64;
        char* volatile g_probe = nullptr;

        std::atomic<uint64_t> g_tick{0};
        uint64_t ManualNow() {
            return g_tick.load();
        }

        FrameRingBackend::Options ManualOptions() {
            g_tick = 0;
            return {.now = &ManualNow, .nanosecondsPerTick = kNanosecondsPerTestTick};
        }

        /// Counts the commands the chord sends
        class CommandRecorder : public SubscriptionHolder {
        public:
            CommandRecorder() {
                Subscribe<EditorCommandEvent, &CommandRecorder::OnCommand>(this);
            }

            int CountOf(PoolString id) const {
                int count = 0;
                for (const PoolString& seen : _seen) {
                    count += seen == id ? 1 : 0;
                }
                return count;
            }

        private:
            void OnCommand(const EditorCommandEvent& event) {
                _seen.push_back(event.commandId);
            }

            Vector<PoolString> _seen;
        };

        SDL_Event KeyDown(SDL_Keycode key, SDL_Keymod mod, bool repeat = false) {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = key;
            event.key.mod = mod;
            event.key.repeat = repeat;
            return event;
        }

        constexpr SDL_Keymod kCtrlShift = SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT;

    }  // namespace

    /// The Profiler's model over a ring and a frame collector of its own and no ImGui
    class ProfilerPanelModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _ring = MakeUnique<FrameRingBackend>(ManualOptions());
            _model = New<ProfilerPanelModel>();
            _model->SetFrameStats(&_stats);
            _model->SetFrameRing(_ring.get());
            _model->Initialize();
        }

        /// One more frame of the ring, @p ticks long, and one of the collector
        void AddFrame(uint64_t ticks) {
            g_tick = g_tick + ticks;
            _ring->FrameMark();
            _stats.BeginFrame();
            _stats.EndFrame({}, AllocTotals{});
        }

        /// One Update as the widget makes it
        void Frame() {
            _model->Update();
            _model->EndUpdate();
        }

        bool Run(PoolString id, StringView argument) {
            return _model->RunCommand(id.ToStringView(), argument);
        }

        ProfilerPanelView& View() {
            return const_cast<ProfilerPanelView&>(_model->GetView());
        }

        FrameStatsCollector _stats;
        UniquePtr<FrameRingBackend> _ring;
        IntrusivePtr<ProfilerPanelModel> _model;
    };

    TEST_F(ProfilerPanelModelTest, TheViewStartsWithTheLabelsOfItsCombosAndTheOldDefaults) {
        const ProfilerPanelView& view = _model->GetView();
        EXPECT_EQ(view.budgetLabels.size(), 3u);
        EXPECT_EQ(view.barModeLabels.size(), 2u);
        EXPECT_EQ(view.zoneWindowLabels.size(), 3u);
        EXPECT_EQ(view.spikeModeLabels.size(), 2u);
        EXPECT_EQ(view.tracePath, "trace.json");
        EXPECT_EQ(view.zonesRecorded, BE_PROFILER != 0);
        EXPECT_EQ(view.allocTracked, AllocTracker::kEnabled);
        EXPECT_EQ(view.spikeKeep, static_cast<int32_t>(SpikeSettings::kDefaultKeep));
        EXPECT_TRUE(static_cast<bool>(_model->GetObject()));
    }

    TEST_F(ProfilerPanelModelTest, NumbersAndIndicesAWriterLeftOutOfRangeAreClampedInTheNextUpdate) {
        View().customBudgetMs = 5000.0f;
        View().spikeThresholdMs = 0.0f;
        View().spikeFactor = std::numeric_limits<float>::quiet_NaN();
        View().spikeKeep = 500;
        View().budget = 7;
        View().barMode = -1;
        View().zoneWindow = 3;
        View().spikeMode = 2;
        Frame();

        EXPECT_FLOAT_EQ(View().customBudgetMs, ProfilerPanelLimits::kMaxCustomBudgetMs);
        EXPECT_FLOAT_EQ(View().spikeThresholdMs, ProfilerPanelLimits::kMinThresholdMs);
        EXPECT_FLOAT_EQ(View().spikeFactor, ProfilerPanelLimits::kMinFactor);
        EXPECT_EQ(View().spikeKeep, ProfilerPanelLimits::kMaxKeep);
        EXPECT_EQ(View().budget, 0);
        EXPECT_EQ(View().barMode, 0);
        EXPECT_EQ(View().zoneWindow, 0);
        EXPECT_EQ(View().spikeMode, 0);
    }

    TEST_F(ProfilerPanelModelTest, TheZoneFilterIsCutOnACodePointBoundary) {
        // 62 bytes and then a two byte letter: the cut at 63 bytes would split it
        View().zoneFilter = String(62, 'a') + "\xC3\xA9";
        Frame();
        EXPECT_EQ(View().zoneFilter.size(), 62u);

        View().zoneFilter = String(100, 'b');
        Frame();
        EXPECT_EQ(View().zoneFilter.size(), ProfilerPanelLimits::kMaxFilterBytes);
    }

    TEST_F(ProfilerPanelModelTest, TheSpikeSettingsOfTheViewReachTheCapture) {
        AddFrame(kSteadyFrameTicks);
        AddFrame(30);
        Frame();
        EXPECT_TRUE(_model->GetSpikes().GetSpikes().empty()) << "30 ms is under the default threshold";

        View().spikeThresholdMs = 20.0f;
        View().spikeKeep = 2;
        Frame();
        EXPECT_DOUBLE_EQ(_model->GetSpikes().GetSettings().thresholdMs, 20.0);
        EXPECT_EQ(_model->GetSpikes().GetSettings().keep, 2u);
        AddFrame(30);
        Frame();
        EXPECT_EQ(_model->GetSpikes().GetSpikes().size(), 1u);

        View().spikeMode = static_cast<int32_t>(+SpikeMode::TimesMedian);
        View().spikeFactor = 2.5f;
        Frame();
        EXPECT_EQ(_model->GetSpikes().GetSettings().mode, SpikeMode::TimesMedian);
        EXPECT_DOUBLE_EQ(_model->GetSpikes().GetSettings().medianFactor, 2.5);
    }

    TEST_F(ProfilerPanelModelTest, TheSpikeTextAndTheFlagsFollowTheCapture) {
        AddFrame(kSteadyFrameTicks);
        Frame();
        EXPECT_EQ(View().spikeText, "0 frozen");
        EXPECT_FALSE(View().hasSpikes);
        EXPECT_TRUE(View().hasFrames);

        AddFrame(kSpikeFrameTicks);
        Frame();
        EXPECT_EQ(View().spikeText, "1 frozen");
        EXPECT_TRUE(View().hasSpikes);
    }

    TEST_F(ProfilerPanelModelTest, PauseKeepsTheViewsCopyWhileTheSpikesStillSeeTheFreshFrames) {
        AddFrame(kSteadyFrameTicks);
        Frame();
        const uint64_t version = _model->GetSnapshotVersion();
        const size_t frames = _model->GetSnapshot().frames.size();
        const size_t history = _model->GetHistory().size();

        View().paused = true;
        AddFrame(kSpikeFrameTicks);
        Frame();
        EXPECT_EQ(_model->GetSnapshotVersion(), version);
        EXPECT_EQ(_model->GetSnapshot().frames.size(), frames);
        EXPECT_EQ(_model->GetHistory().size(), history);
        EXPECT_EQ(_model->GetSpikes().GetSpikes().size(), 1u) << "the capture judges the fresh frame while paused";

        View().paused = false;
        Frame();
        EXPECT_GT(_model->GetSnapshotVersion(), version);
        EXPECT_EQ(_model->GetSnapshot().frames.size(), frames + 1);
        EXPECT_EQ(_model->GetHistory().size(), history + 1);
    }

    TEST_F(ProfilerPanelModelTest, ASelectedFrameTheRingHasOverwrittenIsDropped) {
        for (uint64_t i = 0; i < 3; ++i) {
            AddFrame(kSteadyFrameTicks);
        }
        Frame();
        _model->SelectFrame(_model->GetSnapshot().frames.front().index);
        ASSERT_TRUE(_model->GetSelectedFrame().has_value());

        _ring->Start();  // a new epoch: the old frames are gone
        AddFrame(kSteadyFrameTicks);
        AddFrame(kSteadyFrameTicks);
        Frame();
        EXPECT_FALSE(_model->GetSelectedFrame().has_value());
    }

    TEST_F(ProfilerPanelModelTest, OpeningASpikeAndGoingLiveAskTheTimelineToResetOnce) {
        AddFrame(kSteadyFrameTicks);
        AddFrame(kSpikeFrameTicks);
        Frame();
        ASSERT_EQ(_model->GetSpikes().GetSpikes().size(), 1u);
        EXPECT_FALSE(_model->TakeTimelineRequest().reset);

        _model->OpenSpike(1);  // out of range: nothing happens
        EXPECT_FALSE(_model->GetOpenedSpike().has_value());

        _model->OpenSpike(0);
        Frame();
        EXPECT_EQ(_model->GetOpenedSpike(), _model->GetSpikes().GetSpikes()[0].frameIndex);
        EXPECT_TRUE(View().spikeOpened);
        EXPECT_EQ(View().openedText, Format("Frozen frame {} (the Live button of the spikes goes back)", *_model->GetOpenedSpike()));
        EXPECT_FALSE(_model->GetSpikeSnapshot().frames.empty());
        EXPECT_TRUE(_model->TakeTimelineRequest().reset);
        EXPECT_FALSE(_model->TakeTimelineRequest().reset) << "a request is taken once";

        _model->ShowLive();
        EXPECT_FALSE(_model->GetOpenedSpike().has_value());
        EXPECT_TRUE(_model->GetSpikeSnapshot().frames.empty());
        EXPECT_TRUE(_model->TakeTimelineRequest().reset);
        _model->ShowLive();
        EXPECT_FALSE(_model->TakeTimelineRequest().reset) << "already live: nothing to reset";
    }

    TEST_F(ProfilerPanelModelTest, TheWorstCallOfASiteSelectsItsFrameAndMarksTheZone) {
        AddFrame(kSteadyFrameTicks);
        g_tick = g_tick + 1;
        const uint64_t begin = g_tick;
        _ring->RecordZone(kWorkSite, begin, begin + 5);
        AddFrame(kSteadyFrameTicks);
        Frame();

        _model->ShowWorstCall(kWorkSite);
        ASSERT_TRUE(_model->GetSelectedFrame().has_value());
        const ProfilerTimelineRequest request = _model->TakeTimelineRequest();
        ASSERT_TRUE(request.mark.has_value());
        EXPECT_EQ(request.mark->site, &kWorkSite);
        EXPECT_EQ(request.mark->beginTicks, begin);
    }

    TEST_F(ProfilerPanelModelTest, TheTraceCommandWithNothingToWriteSaysSoAndAnUnknownArgumentDoesNothing) {
        const auto run = [&](PoolString id, StringView argument) { return Run(id, argument); };
        EXPECT_TRUE(run(EditorCommandIds::kProfilerTrace, "writeFrames"));
        EXPECT_EQ(View().traceMessage, "No frame in the ring to write");

        View().traceMessage = "kept";
        EXPECT_TRUE(run(EditorCommandIds::kProfilerTrace, "explode"));
        EXPECT_TRUE(run(EditorCommandIds::kProfilerTrace, ""));
        EXPECT_EQ(View().traceMessage, "kept");
        EXPECT_FALSE(View().recording);
        EXPECT_FALSE(_model->RunCommand("profiler.nothing", ""));
    }

    TEST_F(ProfilerPanelModelTest, WriteFramesWritesTheFrameTheViewIsOnToThePathOfTheView) {
        const TestTempFolder folder("profiler_panel_trace");
        AddFrame(kSteadyFrameTicks);
        AddFrame(kSteadyFrameTicks);
        Frame();
        View().tracePath = folder.PathOf("frame.json");

        EXPECT_TRUE(Run(EditorCommandIds::kProfilerTrace, "writeFrames"));
        EXPECT_EQ(View().traceMessage, Format("Written {} (1 frame(s))", View().tracePath));
    }

    TEST_F(ProfilerPanelModelTest, WriteFramesWritesTheFrozenSpikesWhenThereAreSome) {
        const TestTempFolder folder("profiler_panel_spikes");
        AddFrame(kSteadyFrameTicks);
        AddFrame(kSpikeFrameTicks);
        Frame();
        View().tracePath = folder.PathOf("spikes.json");

        EXPECT_TRUE(Run(EditorCommandIds::kProfilerTrace, "writeFrames"));
        EXPECT_EQ(View().traceMessage, Format("Written {} (1 frame(s))", View().tracePath));
    }

    TEST_F(ProfilerPanelModelTest, AFailedWriteShowsTheReason) {
        AddFrame(kSteadyFrameTicks);
        AddFrame(kSteadyFrameTicks);
        Frame();
        const TestTempFolder folder("profiler_panel_fail");
        View().tracePath = Format("{}/no/such/dir.json", folder.PathOf("x"));

        EXPECT_TRUE(Run(EditorCommandIds::kProfilerTrace, "writeFrames"));
        EXPECT_NE(View().traceMessage.find("cannot write"), String::npos);
    }

    TEST_F(ProfilerPanelModelTest, ClearingTheSpikesGoesBackToTheLiveRingWhenOneWasOpen) {
        AddFrame(kSteadyFrameTicks);
        AddFrame(kSpikeFrameTicks);
        Frame();
        _model->OpenSpike(0);
        EXPECT_TRUE(_model->TakeTimelineRequest().reset);

        EXPECT_TRUE(Run(EditorCommandIds::kProfilerSpikeClear, ""));
        EXPECT_TRUE(_model->GetSpikes().GetSpikes().empty());
        EXPECT_FALSE(_model->GetOpenedSpike().has_value());
        EXPECT_TRUE(_model->TakeTimelineRequest().reset);

        _model->OpenSpike(0);  // nothing frozen any more
        EXPECT_FALSE(_model->GetOpenedSpike().has_value());
    }

    TEST_F(ProfilerPanelModelTest, TheLiveCommandLeavesAnOpenedSpike) {
        AddFrame(kSteadyFrameTicks);
        AddFrame(kSpikeFrameTicks);
        Frame();
        _model->OpenSpike(0);
        EXPECT_TRUE(Run(EditorCommandIds::kProfilerSpikeLive, ""));
        EXPECT_FALSE(_model->GetOpenedSpike().has_value());
        EXPECT_EQ(_model->GetSpikes().GetSpikes().size(), 1u) << "Live keeps the frozen frames";
    }

    TEST_F(ProfilerPanelModelTest, TheChordSendsTheViewCommandOnceAndIgnoresARepeatAndOtherKeys) {
        const CommandRecorder recorder;
        SDLEvents::SDLEventWrapper::Emit(KeyDown(SDLK_P, kCtrlShift));
        EXPECT_EQ(recorder.CountOf(EditorCommandIds::kViewProfiler), 1);

        SDLEvents::SDLEventWrapper::Emit(KeyDown(SDLK_P, kCtrlShift, true));
        SDLEvents::SDLEventWrapper::Emit(KeyDown(SDLK_P, SDL_KMOD_LCTRL));
        SDLEvents::SDLEventWrapper::Emit(KeyDown(SDLK_O, kCtrlShift));
        EXPECT_EQ(recorder.CountOf(EditorCommandIds::kViewProfiler), 1);
    }

    TEST_F(ProfilerPanelModelTest, TheRingRunsOnlyWhileThePanelIsOpenAndTheDestructorSwitchesItOff) {
        if constexpr (BE_PROFILER == 0) {
            GTEST_SKIP() << "the ring is switched only in a build that records zones (ENGINE_PROFILER)";
        }
        const ProfilerStateGuard guard;
        EXPECT_FALSE(Profiler::IsRingEnabled());
        _model->OnOpenChanged(true);
        EXPECT_TRUE(Profiler::IsRingEnabled());
        _model->OnOpenChanged(false);
        EXPECT_FALSE(Profiler::IsRingEnabled());

        _model->OnOpenChanged(true);
        _model.Reset();
        EXPECT_FALSE(Profiler::IsRingEnabled());
    }

    TEST_F(ProfilerPanelModelTest, TheAllocationCountsStayPausedFromUpdateToEndUpdateAndAClosingPanelReleasesThem) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const auto counted = [] {
            const uint64_t before = AllocTracker::GetThreadCounters().count;
            char* probe = new char[kProbeBytes];
            g_probe = probe;  // escapes, so the allocation is not optimized away
            delete[] probe;
            g_probe = nullptr;
            return AllocTracker::GetThreadCounters().count - before;
        };
        EXPECT_GT(counted(), 0u);
        _model->Update();
        EXPECT_EQ(counted(), 0u) << "paused between Update and EndUpdate";
        _model->EndUpdate();
        EXPECT_GT(counted(), 0u);

        _model->Update();
        _model->OnOpenChanged(false);  // the close button: a frame that ended without its EndUpdate
        EXPECT_GT(counted(), 0u);

        _model->Update();
        _model.Reset();
        EXPECT_GT(counted(), 0u) << "the destructor lets go of the pause";
    }

    TEST_F(ProfilerPanelModelTest, ASteadyUpdateAllocatesNothing) {
        _model->SetPausesAllocCounting(false);  // the pause would hide what the frame allocates
        for (int i = 0; i < kSteadyFrames; ++i) {
            AddFrame(kSteadyFrameTicks);
            Frame();
        }
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

}  // namespace BECore::Tests
