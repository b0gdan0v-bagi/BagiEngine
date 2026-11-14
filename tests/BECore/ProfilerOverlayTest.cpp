#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <BECore/Profiler/ProfilerToggle.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Widgets/ProfilerOverlayWidget.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        bool Contains(const eastl::vector<String>& lines, const char* text) {
            for (const String& line : lines) {
                if (line.find(text) != String::npos) {
                    return true;
                }
            }
            return false;
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
        const eastl::vector<String> lines = FormatOverlayLines(SampleFrame(), totals, true);
        EXPECT_TRUE(Contains(lines, "Frame 42"));
        EXPECT_TRUE(Contains(lines, "50 fps"));
        EXPECT_TRUE(Contains(lines, "Draw calls 12"));
        EXPECT_TRUE(Contains(lines, "Texture switches 3"));
        EXPECT_TRUE(Contains(lines, "Allocations 7 (2.0 KB)"));
        EXPECT_TRUE(Contains(lines, "Live 3.0 MB"));
        EXPECT_TRUE(Contains(lines, "ImGui draw calls 9"));
    }

    TEST(FrameStatsTextTest, OnlyAHeldFrameSaysPaused) {
        FrameStats frame = SampleFrame();
        EXPECT_FALSE(Contains(FormatOverlayLines(frame, AllocTotals{}, true), "paused"));
        frame.paused = true;
        EXPECT_TRUE(Contains(FormatOverlayLines(frame, AllocTotals{}, true), "paused"));
    }

    TEST(FrameStatsTextTest, WithoutTrackingTheLineSaysSo) {
        const eastl::vector<String> lines = FormatOverlayLines(SampleFrame(), AllocTotals{}, false);
        EXPECT_TRUE(Contains(lines, "ENGINE_PROFILER_ALLOC=OFF"));
        EXPECT_FALSE(Contains(lines, "Live"));
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

    TEST_F(ProfilerOverlayWidgetTest, AHiddenOverlayDrawsNothing) {
        ProfilerOverlayWidget overlay;
        EXPECT_FALSE(overlay.IsVisible());
        overlay.DrawScreen();
        EXPECT_TRUE(_renderer->calls.empty());
    }

}  // namespace BECore::Tests
