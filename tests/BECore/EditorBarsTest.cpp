#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/EditorLayout/EditorPlayModel.h>
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

                ImGui::GetIO().DeltaTime = kFrameSeconds;
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(kDisplaySize);
                constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
                ImGui::Begin("##Host", nullptr, hostFlags);
                toolbar.Draw(model);
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

            IntrusivePtrAtomic<SceneManager> sceneManager;
            ImGuiFontBuffers buffers;
            ImGuiFonts fonts;
            EditorPlayModel model;
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

    TEST_F(EditorBarsTest, PauseAndStepWorkOnlyWhenTheModelAllowsThem) {
        RunFrame();
        const ImVec2 center = ToolbarCenter();
        const float step = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;

        // Stopped: Pause and Step are disabled
        Click(center.x, center.y);
        Click(center.x + step, center.y);
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);
        EXPECT_EQ(model.GetStepCount(), 0u);

        model.PressPlay();
        Click(center.x + step, center.y);
        EXPECT_EQ(model.GetStepCount(), 0u);

        Click(center.x, center.y);
        EXPECT_EQ(model.GetState(), EditorPlayState::Paused);

        Click(center.x + step, center.y);
        EXPECT_EQ(model.GetState(), EditorPlayState::Paused);
        EXPECT_EQ(model.GetStepCount(), 1u);
    }

    TEST_F(EditorBarsTest, ASteadyFrameAllocatesNothing) {
        for (int i = 0; i < kWarmUpFrames; ++i) {
            RunFrame();
        }

        const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
        for (int i = 0; i < kSteadyFrames; ++i) {
            RunFrame();
        }
        const AllocTracker::ThreadCounters after = AllocTracker::GetThreadCounters();

        // The counters stay zero without ENGINE_PROFILER_ALLOC, so the check only bites in the profile build
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(after.count, before.count);
        }
    }

}  // namespace BECore::Tests
