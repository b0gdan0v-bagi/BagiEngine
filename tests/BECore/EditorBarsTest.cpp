#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/EditorLayout/EditorPlayModel.h>
#include <Widgets/EditorLayout/Hardware/HardwareMonitor.h>
#include <Widgets/EditorLayout/Hardware/HardwareText.h>
#include <Widgets/EditorLayout/MainToolbar.h>
#include <Widgets/EditorLayout/StatusBar.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 720.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kWarmUpFrames = 3;
        constexpr int kSteadyFrames = 10;
        /// Long enough at the frame time of the fixture for ImGui's key repeat to start and fire
        constexpr int kHoldFrames = 60;

        constexpr int32_t kTestTemperatureC = 64;
        constexpr uint32_t kTestLoadPercent = 87;
        constexpr uint32_t kTestMemUsedMb = 9318;
        constexpr uint32_t kTestMemTotalMb = 12288;
        constexpr size_t kLineSize = 96;

        /// Answers what the test last set, on the calling thread
        class SettableReader : public IHardwareReader {
        public:
            bool Read(HardwareReadings& out) override {
                out = readings;
                return answer;
            }

            bool answer = true;
            HardwareReadings readings;
        };

        HardwareReadings GpuReading(uint32_t load) {
            HardwareReadings readings;
            readings.temperatureC = kTestTemperatureC;
            readings.loadPercent = load;
            readings.memUsedMb = kTestMemUsedMb;
            readings.memTotalMb = kTestMemTotalMb;
            return readings;
        }

        /// A context of its own with no backend: the atlas takes the fonts, nothing rasterizes them, and the bars draw into a host window
        class EditorBarsTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                // The status bar asks the scene manager for the active scene
                sceneManager = New<SceneManager>();
                EngineEnvironment::UseSceneManager(sceneManager);
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = kDisplaySize;
                io.IniFilename = nullptr;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                fonts = LoadImGuiFonts(io, buffers);
                SetActiveImGuiFonts(fonts);
                ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
            }

            void TearDown() override {
                // The context goes first: its atlas reads the buffers
                ImGui::DestroyContext();
                SetActiveImGuiFonts({});
                EngineEnvironment::UseSceneManager({});
                sceneManager = {};
            }

            /// One frame with the toolbar and the status bar in a host window; the frame statistics of the engine get a frame too, so the
            /// meter has something to show
            void RunFrame() {
                CoreManager::GetFrameStats().BeginFrame();
                CoreManager::GetFrameStats().EndFrame({}, {});
                DrawBars();
            }

            /// The bars again with no new engine frame: the meter's text comes from the real clock, so only this keeps its length between two
            /// frames whose vertices a test compares
            void DrawBars() {
                ImGui::GetIO().DeltaTime = kFrameSeconds;
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(kDisplaySize);
                constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
                ImGui::Begin("##Host", nullptr, hostFlags);
                const MainToolbar::Presses presses = toolbar.Draw(model);
                stepPresses += presses.step ? 1 : 0;
                statusBar.Draw();
                ImGui::End();
                ImGui::Render();
            }

            /// The centre of the toolbar's window, where its middle button (Pause) sits; the others are a button and a gap away
            [[nodiscard]] ImVec2 ToolbarCenter() const {
                const ImGuiContext& context = *ImGui::GetCurrentContext();
                for (const ImGuiWindow* window : context.Windows) {
                    if (window->Name && std::strstr(window->Name, "MainToolbar") != nullptr) {
                        return ImVec2(window->Pos.x + window->Size.x * 0.5f, window->Pos.y + window->Size.y * 0.5f);
                    }
                }
                ADD_FAILURE() << "the toolbar window was not found";
                return {};
            }

            /// Moves the mouse over (@p x, @p y), presses and releases the left button: three frames, the click lands on the last
            void Click(float x, float y) {
                ImGuiIO& io = ImGui::GetIO();
                io.AddMousePosEvent(x, y);
                RunFrame();
                io.AddMouseButtonEvent(0, true);
                RunFrame();
                io.AddMouseButtonEvent(0, false);
                RunFrame();
            }

            /// Gives the status bar a monitor over a reader of the test's; the test polls it on its own thread, so no worker runs
            void InstallReader(bool answer, const HardwareReadings& readings) {
                auto gpu = MakeUnique<SettableReader>();
                reader = gpu.get();
                reader->answer = answer;
                reader->readings = readings;
                auto owned = MakeUnique<HardwareMonitor>(std::move(gpu), nullptr);
                monitor = owned.get();
                statusBar.SetHardwareMonitor(std::move(owned));
                monitor->PollOnce();
            }

            /// The number of vertices the status bar drew in the last frame: more text, more vertices
            [[nodiscard]] int StatusBarVertexCount() const {
                const ImGuiContext& context = *ImGui::GetCurrentContext();
                for (const ImGuiWindow* window : context.Windows) {
                    if (window->Name && std::strstr(window->Name, "StatusBar") != nullptr) {
                        return window->DrawList->VtxBuffer.Size;
                    }
                }
                ADD_FAILURE() << "the status bar window was not found";
                return 0;
            }

            [[nodiscard]] String HardwareLine() const {
                char line[kLineSize] = {};
                FormatHardwareLine(statusBar.GetHardwareText(), line);
                return String(line);
            }

            SettableReader* reader = nullptr;    // owned by the monitor
            HardwareMonitor* monitor = nullptr;  // owned by the status bar
            IntrusivePtrAtomic<SceneManager> sceneManager;
            ImGuiFontBuffers buffers;
            ImGuiFonts fonts;
            EditorPlayModel model;
            uint32_t stepPresses = 0;  // frames in which the Step button asked for a step
            MainToolbar toolbar;
            StatusBar statusBar;
        };

    }  // namespace

    TEST_F(EditorBarsTest, TheBarsHaveAHeightFromTheFrameHeightAndAreDrawnInsideTheHost) {
        RunFrame();

        EXPECT_GT(MainToolbar::GetHeight(), ImGui::GetFrameHeight());
        EXPECT_GT(StatusBar::GetHeight(), ImGui::GetFrameHeight());
        EXPECT_NE(ToolbarCenter().x, 0.0f);
    }

    TEST_F(EditorBarsTest, ThePlayButtonStartsAndStopsTheModel) {
        RunFrame();
        const ImVec2 center = ToolbarCenter();
        const float step = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;

        Click(center.x - step, center.y);
        EXPECT_EQ(model.GetState(), EditorPlayState::Playing);

        Click(center.x - step, center.y);
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);
    }

    TEST_F(EditorBarsTest, PauseWorksInEveryStateAndStepOnlyWhilePaused) {
        RunFrame();
        const ImVec2 center = ToolbarCenter();
        const float step = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;

        Click(center.x + step, center.y);
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);
        EXPECT_EQ(model.GetStepCount(), 0u);

        Click(center.x, center.y);
        EXPECT_EQ(model.GetState(), EditorPlayState::Paused);
        Click(center.x, center.y);
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);

        model.PressPlay();
        Click(center.x + step, center.y);
        EXPECT_EQ(model.GetStepCount(), 0u);

        Click(center.x, center.y);
        EXPECT_EQ(model.GetState(), EditorPlayState::Paused);

        Click(center.x + step, center.y);
        EXPECT_EQ(model.GetState(), EditorPlayState::Paused);
        EXPECT_EQ(model.GetStepCount(), 1u);
    }

    TEST_F(EditorBarsTest, HoldingTheStepButtonRepeatsTheStep) {
        model.PressPause();
        RunFrame();
        const ImVec2 center = ToolbarCenter();
        const float step = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;

        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(center.x + step, center.y);
        RunFrame();
        io.AddMouseButtonEvent(0, true);
        for (int i = 0; i < kHoldFrames; ++i) {
            RunFrame();
        }
        io.AddMouseButtonEvent(0, false);
        RunFrame();

        EXPECT_GT(stepPresses, 1u);
        EXPECT_EQ(model.GetStepCount(), stepPresses);
    }

    TEST_F(EditorBarsTest, NoHardwareItemIsDrawnWithoutAMonitorOrWithoutReadings) {
        RunFrame();
        const int bare = StatusBarVertexCount();
        EXPECT_FALSE(statusBar.GetHardwareText().visible);

        InstallReader(false, GpuReading(kTestLoadPercent));
        DrawBars();

        EXPECT_FALSE(statusBar.GetHardwareText().visible);
        EXPECT_EQ(StatusBarVertexCount(), bare);
    }

    TEST_F(EditorBarsTest, AGpuReadingIsShownInTheBar) {
        RunFrame();
        const int bare = StatusBarVertexCount();

        InstallReader(true, GpuReading(kTestLoadPercent));
        RunFrame();

        EXPECT_TRUE(statusBar.GetHardwareText().visible);
        EXPECT_EQ(HardwareLine(), "GPU 64\xC2\xB0"
                                  "C 87% 9.1/12 GB");
        EXPECT_GT(StatusBarVertexCount(), bare);
    }

    TEST_F(EditorBarsTest, ACpuFallbackIsShownWhenThereIsNoGpuReading) {
        constexpr uint32_t kCpuLoad = 45;
        // The reader is the same type as the GPU's one; the slot decides the kind
        auto cpu = MakeUnique<SettableReader>();
        cpu->readings.loadPercent = kCpuLoad;
        auto owned = MakeUnique<HardwareMonitor>(nullptr, std::move(cpu));
        owned->PollOnce();
        statusBar.SetHardwareMonitor(std::move(owned));
        RunFrame();

        EXPECT_EQ(HardwareLine(), "CPU 45%");
    }

    TEST_F(EditorBarsTest, TheReadoutIsMadeAgainOnlyWhenTheReadingsChanged) {
        InstallReader(true, GpuReading(kTestLoadPercent));
        const uint32_t firstVersion = monitor->Version();
        RunFrame();
        HardwareReadings sink;
        // The bar took the published readings: nothing is left to fetch
        EXPECT_FALSE(monitor->Fetch(sink));

        monitor->PollOnce();
        EXPECT_EQ(monitor->Version(), firstVersion);
        RunFrame();
        EXPECT_EQ(HardwareLine(), "GPU 64\xC2\xB0"
                                  "C 87% 9.1/12 GB");

        reader->readings = GpuReading(kTestLoadPercent + 1);
        monitor->PollOnce();
        EXPECT_NE(monitor->Version(), firstVersion);
        RunFrame();
        EXPECT_FALSE(monitor->Fetch(sink));
        EXPECT_EQ(HardwareLine(), "GPU 64\xC2\xB0"
                                  "C 88% 9.1/12 GB");
    }

    TEST_F(EditorBarsTest, ASteadyFrameWithAHardwareReadingAllocatesNothing) {
        InstallReader(true, GpuReading(kTestLoadPercent));
        for (int i = 0; i < kWarmUpFrames; ++i) {
            RunFrame();
        }

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                RunFrame();
            }
        });
        EXPECT_TRUE(statusBar.GetHardwareText().visible);
    }

    TEST_F(EditorBarsTest, ASteadyFrameAllocatesNothing) {
        for (int i = 0; i < kWarmUpFrames; ++i) {
            RunFrame();
        }

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                RunFrame();
            }
        });
    }

}  // namespace BECore::Tests
