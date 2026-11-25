#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <BECore/Profiler/Profiler.h>
#include <BECore/Profiler/ProfilerToggle.h>
#include <BECore/Renderer/RendererManager.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/ProfilerTestSupport.h>
#include <Common/RecordingRenderer.h>
#include <Common/TestText.h>
#include <CoreSDL/SDLEvents.h>
#include <Widgets/ProfilerOverlayWidget.h>
#include <chrono>
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

        constexpr ProfileSite kBusySite{"Overlay test busy zone", "Test", "ProfilerOverlayTest.cpp", 1, 0};
        constexpr ProfileSite kQuickSite{"Overlay test quick zone", "Test", "ProfilerOverlayTest.cpp", 2, 0};
        constexpr std::chrono::milliseconds kBusyTime{20};  // far above a zone that does nothing, so the order holds on a loaded machine

        void SpinFor(std::chrono::milliseconds duration) {
            const auto until = std::chrono::steady_clock::now() + duration;
            while (std::chrono::steady_clock::now() < until) {
            }
        }

        /// Frames of the process's ring with one busy and one quick zone each; each frame ends with a mark
        void FeedRing(int frames) {
            for (int i = 0; i < frames; ++i) {
                {
                    const ProfileZone busy(kBusySite);
                    SpinFor(kBusyTime);
                }
                {
                    const ProfileZone quick(kQuickSite);
                }
                Profiler::FrameMark();
            }
        }

        FrameStats SampleFrame() {
            FrameStats stats;
            stats.frame = 42;
            stats.frameMs = 20.0;
            stats.cpuMs = 5.0;
            stats.stageMs[+FrameStage::Wait] = 8.5;
            stats.draws = RenderCounters{.drawCalls = 12, .primitives = 30, .triangles = 24, .textureSwitches = 3};
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

    TEST(ProfilerToggleTest, CtrlShiftTSavesTheTraceWithTheSameModifierRules) {
        EXPECT_TRUE(IsProfilerExportChord(SDLK_T, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT, false));
        EXPECT_TRUE(IsProfilerExportChord(SDLK_T, SDL_KMOD_RCTRL | SDL_KMOD_RSHIFT | SDL_KMOD_NUM, false));
        EXPECT_FALSE(IsProfilerExportChord(SDLK_T, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT, true));
        EXPECT_FALSE(IsProfilerExportChord(SDLK_T, SDL_KMOD_LSHIFT, false));
        EXPECT_FALSE(IsProfilerExportChord(SDLK_T, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT | SDL_KMOD_LALT, false));
        EXPECT_FALSE(IsProfilerExportChord(SDLK_P, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT, false));
        EXPECT_FALSE(IsProfilerToggle(SDLK_T, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT, false));
    }

    TEST(FrameStatsTextTest, TheLinesNameTheFrameTheDrawsAndTheCounters) {
        AllocTotals totals;
        totals.liveBytes = 3 * 1024 * 1024;
        const Vector<String> lines = FormatOverlayLines(SampleFrame(), totals, true);
        EXPECT_TRUE(AnyLineContains(lines, "Frame 42"));
        EXPECT_TRUE(AnyLineContains(lines, "50 fps"));
        EXPECT_TRUE(AnyLineContains(lines, "Wait 8.50 ms"));
        EXPECT_TRUE(AnyLineContains(lines, "Draw calls 12  Primitives 30  Triangles 24"));
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
        for (uint32_t frame = 0; frame < ProfilerOverlayWidget::kZoneRefreshFrames + 2; ++frame) {  // a refresh of the zones included
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

    TEST_F(ProfilerOverlayWidgetTest, TheRingRunsOnlyWhileTheOverlayIsVisible) {
        if constexpr (BE_PROFILER == 0) {
            GTEST_SKIP() << "the ring is the overlay's zone source only in a build with the profiler";
        }
        const ProfilerStateGuard guard;
        {
            ProfilerOverlayWidget overlay;
            EXPECT_FALSE(Profiler::IsRingEnabled());
            overlay.SetVisible(true);
            EXPECT_TRUE(Profiler::IsRingEnabled());
            overlay.SetVisible(false);
            EXPECT_FALSE(Profiler::IsRingEnabled());
            overlay.SetVisible(true);
            EXPECT_TRUE(Profiler::IsRingEnabled());
        }
        EXPECT_FALSE(Profiler::IsRingEnabled());  // destroyed while visible
    }

    TEST_F(ProfilerOverlayWidgetTest, TheTopZonesAreInSelfTimeOrderAndLeaveOutTheOverlaysOwnZone) {
        if constexpr (BE_PROFILER == 0) {
            GTEST_SKIP() << "the overlay has no zones without the profiler";
        }
        const ProfilerStateGuard guard;
        ProfilerOverlayWidget overlay;
        overlay.SetVisible(true);
        FeedRing(3);
        overlay.DrawScreen();  // its zone is in the ring once the frame is marked
        Profiler::FrameMark();
        overlay.RefreshZones();

        size_t overlayZones = 0;
        for (const RingThread& thread : Profiler::GetFrameRing().Snapshot().threads) {
            for (const ZoneRecord& zone : thread.zones) {
                overlayZones += StringView(zone.site->name) == kProfilerOverlayZone ? 1 : 0;
            }
        }
        ASSERT_GT(overlayZones, 0u);

        Vector<String> lines;
        overlay.EmitLines(*CoreManager::GetFrameStats().GetLast(), [&lines](StringView line) { lines.push_back(String(line.data(), line.size())); });
        const auto indexOf = [&lines](StringView part) {
            for (size_t i = 0; i < lines.size(); ++i) {
                if (Contains(lines[i], part)) {
                    return i;
                }
            }
            return lines.size();
        };
        EXPECT_NE(indexOf("Top zones by self time"), lines.size());
        const size_t busy = indexOf("Overlay test busy zone");
        const size_t quick = indexOf("Overlay test quick zone");
        ASSERT_NE(busy, lines.size());
        ASSERT_NE(quick, lines.size());
        EXPECT_LT(busy, quick);
        EXPECT_EQ(indexOf(kProfilerOverlayZone), lines.size());
    }

    TEST_F(ProfilerOverlayWidgetTest, ADrawLeavesTheOuterZoneInTheRingAndNoneOfTheZonesInsideIt) {
        if constexpr (BE_PROFILER == 0) {
            GTEST_SKIP() << "the ring has no zones without the profiler";
        }
        const ProfilerStateGuard guard;
        ProfilerOverlayWidget overlay;
        overlay.SetVisible(true);
        overlay.DrawScreen();  // the first draw loads the font and builds the atlas: zones that must not be recorded

        size_t overlayZones = 0;
        for (const RingThread& thread : Profiler::GetFrameRing().Snapshot().threads) {
            for (const ZoneRecord& outer : thread.zones) {
                if (StringView(outer.site->name) != kProfilerOverlayZone) {
                    continue;
                }
                ++overlayZones;
                for (const ZoneRecord& zone : thread.zones) {
                    const bool inside = zone.beginTicks >= outer.beginTicks && zone.endTicks <= outer.endTicks && &zone != &outer;
                    EXPECT_FALSE(inside) << zone.site->name << " was recorded inside the overlay zone";
                }
            }
        }
        EXPECT_EQ(overlayZones, 1u);
    }

    TEST_F(ProfilerOverlayWidgetTest, TheZonesAreReadOnceInKZoneRefreshFrames) {
        if constexpr (BE_PROFILER == 0) {
            GTEST_SKIP() << "the overlay has no zones without the profiler";
        }
        const ProfilerStateGuard guard;
        ProfilerOverlayWidget overlay;
        overlay.SetVisible(true);
        overlay.DrawScreen();  // reads the (empty) ring at once
        FeedRing(2);
        const auto zoneLines = [&overlay] {
            size_t count = 0;
            overlay.EmitLines(*CoreManager::GetFrameStats().GetLast(), [&count](StringView line) { count += Contains(line, "Overlay test") ? 1 : 0; });
            return count;
        };
        for (uint32_t frame = 1; frame < ProfilerOverlayWidget::kZoneRefreshFrames; ++frame) {
            overlay.DrawScreen();
            ASSERT_EQ(zoneLines(), 0u) << "frame " << frame << " read the ring before its time";
        }
        overlay.DrawScreen();
        EXPECT_EQ(zoneLines(), 2u);
    }

    namespace {

        /// What the overlay's export hands to its sink
        struct ExportCapture {
            int calls = 0;
            String fileName;
            String json;
            bool fail = false;
        };

        void CaptureExports(ProfilerOverlayWidget& overlay, ExportCapture& capture) {
            overlay.SetExportSink([&capture](StringView fileName, const String& json) -> std::expected<void, String> {
                ++capture.calls;
                capture.fileName = String(fileName.data(), fileName.size());
                capture.json = json;
                if (capture.fail) {
                    return std::unexpected(String("disk full"));
                }
                return {};
            });
        }

        SDL_Event ExportChord() {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = SDLK_T;
            event.key.mod = SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT;
            return event;
        }

        SDL_MouseButtonEvent Press(float x, float y, Uint8 button = SDL_BUTTON_LEFT) {
            SDL_MouseButtonEvent event{};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button = button;
            event.x = x;
            event.y = y;
            return event;
        }

        String StatusLine(const ProfilerOverlayWidget& overlay) {
            String status;
            overlay.EmitLines(*CoreManager::GetFrameStats().GetLast(), [&status](StringView line) {
                if (Contains(line, "Saved ") || Contains(line, "Save failed")) {
                    status = String(line.data(), line.size());
                }
            });
            return status;
        }

    }  // namespace

    TEST_F(ProfilerOverlayWidgetTest, TheChordSavesTheRingsLastSecondsAndSaysSo) {
        if constexpr (BE_PROFILER == 0) {
            GTEST_SKIP() << "the export needs the profiler";
        }
        const ProfilerStateGuard guard;
        ProfilerOverlayWidget overlay;
        ExportCapture capture;
        CaptureExports(overlay, capture);
        overlay.SetVisible(true);
        FeedRing(3);
        overlay.OnSDLEvent(SDLEvents::SDLEventWrapper(ExportChord()));

        ASSERT_EQ(capture.calls, 1);
        EXPECT_EQ(capture.fileName, kProfilerTraceFileName);
        EXPECT_TRUE(Contains(capture.json, "Overlay test busy zone"));
        EXPECT_TRUE(Contains(capture.json, "\"Frame\""));
        EXPECT_TRUE(Contains(StatusLine(overlay), Format("Saved {} (", kProfilerTraceFileName)));
    }

    TEST_F(ProfilerOverlayWidgetTest, AFailedSaveShowsItsErrorAndTheStatusGoesAfterKStatusFrames) {
        if constexpr (BE_PROFILER == 0) {
            GTEST_SKIP() << "the export needs the profiler";
        }
        const ProfilerStateGuard guard;
        ProfilerOverlayWidget overlay;
        ExportCapture capture;
        capture.fail = true;
        CaptureExports(overlay, capture);
        overlay.SetVisible(true);
        EXPECT_FALSE(overlay.ExportTrace());
        EXPECT_TRUE(Contains(StatusLine(overlay), "Save failed: disk full"));
        for (uint32_t frame = 0; frame < ProfilerOverlayWidget::kStatusFrames; ++frame) {
            overlay.DrawScreen();
        }
        EXPECT_TRUE(StatusLine(overlay).empty());
    }

    TEST_F(ProfilerOverlayWidgetTest, ThereIsNoExportWhileHidden) {
        if constexpr (BE_PROFILER == 0) {
            GTEST_SKIP() << "the export needs the profiler";
        }
        const ProfilerStateGuard guard;
        ProfilerOverlayWidget overlay;
        ExportCapture capture;
        CaptureExports(overlay, capture);
        overlay.OnSDLEvent(SDLEvents::SDLEventWrapper(ExportChord()));
        EXPECT_FALSE(overlay.ExportTrace());
        overlay.SetVisible(true);
        overlay.SetVisible(false);
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Press(0.0f, 0.0f)));
        EXPECT_EQ(capture.calls, 0);
    }

    TEST_F(ProfilerOverlayWidgetTest, AClickOnTheSaveLineSavesAndIsTakenAnyOtherPressPassesOn) {
        if constexpr (BE_PROFILER == 0) {
            GTEST_SKIP() << "the export needs the profiler";
        }
        const ProfilerStateGuard guard;
        ProfilerOverlayWidget overlay;
        ExportCapture capture;
        CaptureExports(overlay, capture);
        overlay.SetVisible(true);
        overlay.DrawScreen();
        const Rect rect = overlay.GetSaveTraceRect();
        ASSERT_FALSE(rect.IsEmpty()) << "no font: the line has no place";
        UIInput& input = CoreManager::GetUIInput();

        EXPECT_FALSE(input.DispatchMouseButtonDown(Press(rect.x + rect.w + 1.0f, rect.y + rect.h * 0.5f)));
        EXPECT_FALSE(input.DispatchMouseButtonDown(Press(rect.x + 1.0f, rect.y - 1.0f)));
        EXPECT_FALSE(input.DispatchMouseButtonDown(Press(rect.x + 1.0f, rect.y + rect.h * 0.5f, SDL_BUTTON_RIGHT)));
        EXPECT_EQ(capture.calls, 0);

        EXPECT_TRUE(input.DispatchMouseButtonDown(Press(rect.x + 1.0f, rect.y + rect.h * 0.5f)));
        EXPECT_EQ(capture.calls, 1);
    }

    TEST_F(ProfilerOverlayWidgetTest, TheExportLeavesOnlyTheOverlaysOwnZoneInTheRing) {
        if constexpr (BE_PROFILER == 0) {
            GTEST_SKIP() << "the ring has no zones without the profiler";
        }
        const ProfilerStateGuard guard;
        ProfilerOverlayWidget overlay;
        ExportCapture capture;
        CaptureExports(overlay, capture);
        overlay.SetVisible(true);
        FeedRing(2);
        ASSERT_TRUE(overlay.ExportTrace());
        Profiler::FrameMark();

        for (const RingThread& thread : Profiler::GetFrameRing().Snapshot().threads) {
            for (const ZoneRecord& zone : thread.zones) {
                const StringView name = zone.site->name;
                EXPECT_TRUE(Contains(name, "Overlay test") || name == kProfilerOverlayZone) << String(name.data(), name.size());
            }
        }
    }

    TEST_F(ProfilerOverlayWidgetTest, AHiddenOverlayDrawsNothing) {
        ProfilerOverlayWidget overlay;
        EXPECT_FALSE(overlay.IsVisible());
        overlay.DrawScreen();
        EXPECT_TRUE(_renderer->calls.empty());
    }

}  // namespace BECore::Tests
