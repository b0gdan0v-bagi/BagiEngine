#include <BECore/Control/ControlService.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/Feedback/ToastEvents.h>
#include <Widgets/Feedback/ToastOverlay.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 720.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr float kBottomInset = 40.0f;
        constexpr int kWarmUpFrames = 20;  // past the fade-in
        constexpr int kSteadyFrames = 10;
        constexpr float kAntiAliasFringe = 1.0f;

        /// A context of its own with no backend, as EditorBarsTest's: a host window over the display, the overlay, and a floating window on request
        class ToastOverlayTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = kDisplaySize;
                io.IniFilename = nullptr;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                fonts = LoadImGuiFonts(io, buffers);
                SetActiveImGuiFonts(fonts);
                ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
                overlay.Connect(service.Activity());
            }

            void TearDown() override {
                // The context goes first: its atlas reads the buffers
                ImGui::DestroyContext();
                SetActiveImGuiFonts({});
            }

            void RunFrame(float seconds = kFrameSeconds) {
                ImGui::GetIO().DeltaTime = seconds;
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(kDisplaySize);
                ImGui::Begin("##Host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
                ImGui::End();
                overlay.Draw(kBottomInset);
                if (showFloating) {
                    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                    ImGui::SetNextWindowSize(kDisplaySize);
                    ImGui::Begin("Floating", nullptr, ImGuiWindowFlags_NoSavedSettings);
                    ImGui::End();
                }
                ImGui::Render();
            }

            /// The overlay's window when it was drawn in the last frame, else null
            [[nodiscard]] static ImGuiWindow* DrawnToasts() {
                ImGuiWindow* const window = ImGui::FindWindowByName("##Toasts");
                return window != nullptr && window->Active ? window : nullptr;
            }

            [[nodiscard]] static float RightmostVertex(const ImGuiWindow& window) {
                float right = 0.0f;
                for (const ImDrawVert& vertex : window.DrawList->VtxBuffer) {
                    right = eastl::max(right, vertex.pos.x);
                }
                return right;
            }

            ControlService service;
            ToastOverlay overlay;
            ImGuiFontBuffers buffers;
            ImGuiFonts fonts;
            bool showFloating = false;
        };

    }  // namespace

    TEST_F(ToastOverlayTest, NothingIsDrawnWithNoToast) {
        RunFrame();
        RunFrame();

        EXPECT_EQ(ImGui::FindWindowByName("##Toasts"), nullptr);
    }

    TEST_F(ToastOverlayTest, AToastIsDrawnInTheBottomRightCornerAboveTheInsetAndIsGoneAfterItsLife) {
        EditorToastEvent::Emit(ToastLevel::Success, "Scene saved");
        RunFrame();
        RunFrame();

        const ImGuiWindow* const toasts = DrawnToasts();
        ASSERT_NE(toasts, nullptr);
        EXPECT_GT(toasts->DrawList->VtxBuffer.Size, 0);
        const ImVec2 corner(toasts->Pos.x + toasts->Size.x, toasts->Pos.y + toasts->Size.y);
        EXPECT_LT(corner.x, kDisplaySize.x);
        EXPECT_GT(corner.x, kDisplaySize.x - toasts->Size.x);
        EXPECT_LT(corner.y, kDisplaySize.y - kBottomInset);
        EXPECT_GT(corner.y, kDisplaySize.y - kBottomInset - toasts->Size.y);

        RunFrame(ToastList::kLifeSeconds);

        EXPECT_EQ(DrawnToasts(), nullptr);
        EXPECT_EQ(overlay.GetList().Size(), 0u);
    }

    TEST_F(ToastOverlayTest, TheCardsStackUpwards) {
        EditorToastEvent::Emit(ToastLevel::Info, "first");
        RunFrame();
        const ImGuiWindow* const one = DrawnToasts();
        ASSERT_NE(one, nullptr);
        const float bottom = one->Pos.y + one->Size.y;
        const float height = one->Size.y;

        EditorToastEvent::Emit(ToastLevel::Error, "second");
        RunFrame();

        const ImGuiWindow* const two = DrawnToasts();
        ASSERT_NE(two, nullptr);
        EXPECT_FLOAT_EQ(two->Pos.y + two->Size.y, bottom);
        EXPECT_GT(two->Size.y, height);
    }

    TEST_F(ToastOverlayTest, ItTakesNeitherTheFocusNorTheMouseAndStaysAboveAFloatingWindow) {
        showFloating = true;
        RunFrame();
        EditorToastEvent::Emit(ToastLevel::Info, "over the floating window");
        RunFrame();
        const ImGuiWindow* const toasts = DrawnToasts();
        ASSERT_NE(toasts, nullptr);
        ImGuiWindow* const floating = ImGui::FindWindowByName("Floating");
        ASSERT_NE(floating, nullptr);
        ImGui::FocusWindow(floating);
        ImGui::GetIO().AddMousePosEvent(toasts->Pos.x + toasts->Size.x * 0.5f, toasts->Pos.y + toasts->Size.y * 0.5f);
        RunFrame();
        RunFrame();

        const ImGuiContext& context = *ImGui::GetCurrentContext();
        EXPECT_EQ(context.Windows.back(), toasts);
        EXPECT_EQ(context.HoveredWindow, floating);
        EXPECT_EQ(context.NavWindow, floating);
    }

    TEST_F(ToastOverlayTest, ALongTextIsCutAtTheEdgeOfTheCard) {
        String text;
        text.resize(1000, 'W');
        EditorToastEvent::Emit(ToastLevel::Warning, StringView(text.data(), text.size()));
        RunFrame();
        RunFrame();

        const ImGuiWindow* const toasts = DrawnToasts();
        ASSERT_NE(toasts, nullptr);
        EXPECT_LE(RightmostVertex(*toasts), toasts->Pos.x + toasts->Size.x + kAntiAliasFringe);
    }

    TEST_F(ToastOverlayTest, ASteadyFrameWithNoToastAllocatesNothing) {
        for (int i = 0; i < kWarmUpFrames; ++i) {
            RunFrame();
        }

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                RunFrame();
            }
        });
    }

    TEST_F(ToastOverlayTest, ASteadyFrameWithStandingToastsAllocatesNothing) {
        EditorToastEvent::Emit(ToastLevel::Info, "Control API client connected");
        EditorToastEvent::Emit(ToastLevel::Info, "Control API client connected");
        EditorToastEvent::Emit(ToastLevel::Error, "Scene save failed");
        for (int i = 0; i < kWarmUpFrames; ++i) {
            RunFrame();
        }

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                RunFrame();
            }
        });
        EXPECT_EQ(overlay.GetList().Size(), 2u);
    }

}  // namespace BECore::Tests
