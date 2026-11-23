#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <BECore/Profiler/ProfilerToggle.h>
#include <BECore/Renderer/RendererManager.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/TestText.h>
#include <Widgets/ProfilerOverlayWidget.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        bool AnyLineContains(const Vector<String>& lines, StringView part) {
            return eastl::any_of(lines.begin(), lines.end(), [part](const String& line) { return Contains(line, part); });
        }

        constexpr uint64_t kFrameNanoseconds = 16000000;
        uint64_t g_clockNanoseconds = 0;
        uint64_t SteadyClock() {
            return g_clockNanoseconds;
        }

        FrameStats SampleFrame() {
            FrameStats stats;
            stats.frame = 42;
            stats.frameMs = 20.0;
            stats.cpuMs = 5.0;
            stats.draws = RenderCounters{.drawCalls = 12, .triangles = 24, .textureSwitches = 3};
            stats.allocCount = 7;
            stats.allocBytes = 2048;
            stats.counters.push_back({"ImGui draw calls"_intern, 9.0});
            return stats;
        }

    }  // namespace

    TEST(ProfilerToggleTest, CtrlShiftPWithoutRepeatOrOtherModifiers) {
        EXPECT_TRUE(IsProfilerToggle(SDLK_P, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT, false));
        EXPECT_TRUE(IsProfilerToggle(SDLK_P, SDL_KMOD_RCTRL | SDL_KMOD_RSHIFT | SDL_KMOD_NUM, false));
        EXPECT_FALSE(IsProfilerToggle(SDLK_P, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT, true));
        EXPECT_FALSE(IsProfilerToggle(SDLK_P, SDL_KMOD_LCTRL, false));
        EXPECT_FALSE(IsProfilerToggle(SDLK_P, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT | SDL_KMOD_LALT, false));
        EXPECT_FALSE(IsProfilerToggle(SDLK_P, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT | SDL_KMOD_LGUI, false));
        EXPECT_FALSE(IsProfilerToggle(SDLK_E, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT, false));
    }

    TEST(FrameStatsTextTest, TheLinesNameTheFrameTheDrawsAndTheCounters) {
        AllocTotals totals;
        totals.liveBytes = 3 * 1024 * 1024;
        const Vector<String> lines = FormatOverlayLines(SampleFrame(), totals, true);
        EXPECT_TRUE(AnyLineContains(lines, "Frame 42"));
        EXPECT_TRUE(AnyLineContains(lines, "50 fps"));
        EXPECT_TRUE(AnyLineContains(lines, "Draw calls 12"));
        EXPECT_TRUE(AnyLineContains(lines, "Texture switches 3"));
        EXPECT_TRUE(AnyLineContains(lines, "Allocations 7 (2.0 KB)"));
        EXPECT_TRUE(AnyLineContains(lines, "Live 3.0 MB"));
        EXPECT_TRUE(AnyLineContains(lines, "ImGui draw calls 9"));
    }

    TEST(FrameStatsTextTest, OnlyAHeldFrameSaysPaused) {
        FrameStats frame = SampleFrame();
        EXPECT_FALSE(AnyLineContains(FormatOverlayLines(frame, AllocTotals{}, true), "paused"));
        frame.paused = true;
        EXPECT_TRUE(AnyLineContains(FormatOverlayLines(frame, AllocTotals{}, true), "paused"));
    }

    TEST(FrameStatsTextTest, WithoutTrackingTheLineSaysSo) {
        const Vector<String> lines = FormatOverlayLines(SampleFrame(), AllocTotals{}, false);
        EXPECT_TRUE(AnyLineContains(lines, "ENGINE_PROFILER_ALLOC=OFF"));
        EXPECT_FALSE(AnyLineContains(lines, "Live"));
    }

    TEST(FrameStatsTextTest, TheLinesToAVisitorAreTheLinesOfTheVector) {
        AllocTotals totals;
        totals.liveBytes = 3 * 1024 * 1024;
        totals.peakLiveBytes = 5 * 1024 * 1024;
        FrameStats paused = SampleFrame();
        paused.paused = true;
        for (const FrameStats& stats : {SampleFrame(), paused}) {
            for (const bool tracking : {true, false}) {
                const Vector<String> expected = FormatOverlayLines(stats, totals, tracking);
                Vector<String> got;
                FormatOverlayLinesTo(stats, totals, tracking, [&got](StringView line) { got.push_back(String(line.data(), line.size())); });
                EXPECT_EQ(got, expected);
            }
        }
    }

    TEST(FrameStatsTextTest, ALineLongerThanTheCapacityIsCut) {
        FrameStats stats = SampleFrame();
        const String longName(kOverlayLineCapacity * 2, 'x');
        stats.counters.push_back({PoolString::Intern(longName), 1.0});
        size_t longest = 0;
        FormatOverlayLinesTo(stats, AllocTotals{}, true, [&longest](StringView line) { longest = eastl::max(longest, line.size()); });
        EXPECT_EQ(longest, kOverlayLineCapacity);
    }

    TEST(FrameStatsTextTest, TheLinesToAVisitorAllocateNothing) {
        const FrameStats stats = SampleFrame();
        const AllocTotals totals{};
        size_t length = 0;
        const auto emit = [&length](StringView line) { length += line.size(); };
        FormatOverlayLinesTo(stats, totals, true, emit);
        ExpectNoAllocations([&] { FormatOverlayLinesTo(stats, totals, true, emit); });
        EXPECT_GT(length, 0u);
    }

    TEST(FrameStatsTextTest, BytesAreReadable) {
        EXPECT_EQ(FormatBytes(512), "512 B");
        EXPECT_EQ(FormatBytes(1536), "1.5 KB");
        EXPECT_EQ(FormatBytes(5 * 1024 * 1024), "5.0 MB");
    }

    class ProfilerOverlayWidgetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            FrameStatsCollector& collector = CoreManager::GetFrameStats();
            for (int i = 0; i < 3; ++i) {
                collector.BeginFrame();
                collector.EndFrame(RenderCounters{.drawCalls = 4}, AllocTotals{});
            }
        }

        void TearDown() override {
            CoreManager::GetRendererManager().SetRenderer({});
        }

        IntrusivePtr<RecordingRenderer> _renderer;
    };

    TEST_F(ProfilerOverlayWidgetTest, AVisibleOverlayDrawsWithoutCountingItsDraws) {
        ProfilerOverlayWidget overlay;
        overlay.SetVisible(true);
        overlay.DrawScreen();
        if (!kProfilerViewsBuilt) {
            EXPECT_TRUE(_renderer->calls.empty());
            return;
        }
        EXPECT_GT(_renderer->calls.size(), 3u);  // the background, the text, the bars, the budget line
        EXPECT_EQ(_renderer->PeekRenderCounters().drawCalls, 0u);
        EXPECT_FLOAT_EQ(_renderer->GetViewTransform().scale, 1.0f);
    }

    TEST_F(ProfilerOverlayWidgetTest, AVisibleOverlayAllocatesNothingTheTrackerCounts) {
        if (!AllocTracker::kEnabled || !kProfilerViewsBuilt) {
            GTEST_SKIP() << "needs ENGINE_PROFILER_ALLOC and the profiler views";
        }
        ProfilerOverlayWidget overlay;
        overlay.SetVisible(true);
        const uint64_t before = AllocTracker::GetThreadCounters().count;
        for (int frame = 0; frame < 4; ++frame) {
            overlay.DrawScreen();
        }
        EXPECT_GT(_renderer->calls.size(), 0u);
        EXPECT_EQ(AllocTracker::GetThreadCounters().count, before);
    }

    TEST_F(ProfilerOverlayWidgetTest, DrawingTwiceWithTheSameStatsRecordsTheSameCalls) {
        if (!kProfilerViewsBuilt) {
            GTEST_SKIP() << "the overlay draws only in a build with the profiler views";
        }
        ProfilerOverlayWidget overlay;
        overlay.SetVisible(true);
        overlay.DrawScreen();
        const auto first = _renderer->calls;
        _renderer->ClearCalls();
        overlay.DrawScreen();
        ASSERT_EQ(_renderer->calls.size(), first.size());
        for (size_t i = 0; i < first.size(); ++i) {
            EXPECT_EQ(_renderer->calls[i].kind, first[i].kind) << "call " << i;
        }
    }

    TEST_F(ProfilerOverlayWidgetTest, ANewFrameWithTheSameTextShapeDrawsTheSameNumberOfCalls) {
        if (!kProfilerViewsBuilt) {
            GTEST_SKIP() << "the overlay draws only in a build with the profiler views";
        }
        FrameStatsCollector& collector = CoreManager::GetFrameStats();
        collector.SetClock(SteadyClock);
        // A full history (the bars stay as many) and two frames of the same length whose numbers have the same digit count
        const auto endFrame = [&collector] {
            g_clockNanoseconds += kFrameNanoseconds;
            collector.BeginFrame();
            g_clockNanoseconds += kFrameNanoseconds;
            collector.EndFrame(RenderCounters{.drawCalls = 4}, AllocTotals{});
        };
        for (size_t i = 0; i < FrameStatsCollector::kFrameHistory || collector.GetFrameCount() % 10 != 5; ++i) {
            endFrame();
        }
        ProfilerOverlayWidget overlay;
        overlay.SetVisible(true);
        overlay.DrawScreen();
        const size_t firstCount = _renderer->calls.size();
        _renderer->ClearCalls();
        endFrame();
        overlay.DrawScreen();
        collector.SetClock(nullptr);
        EXPECT_EQ(_renderer->calls.size(), firstCount);
    }

    TEST_F(ProfilerOverlayWidgetTest, AHiddenOverlayDrawsNothing) {
        ProfilerOverlayWidget overlay;
        EXPECT_FALSE(overlay.IsVisible());
        overlay.DrawScreen();
        EXPECT_TRUE(_renderer->calls.empty());
    }

}  // namespace BECore::Tests
