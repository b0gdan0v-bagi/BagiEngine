#include <BECore/Control/ControlService.h>
#include <BECore/GameControlTest.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/ClassMeta.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Games/GameEditorTools.h>
#include <Games/GameManager.h>
#include <Widgets/GamePanel/GameCommandRunner.h>
#include <Widgets/GamePanel/GameEditorCommands.h>
#include <Widgets/GamePanel/GameEditorPanels.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 720.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr float kSideShare = 0.3f;
        constexpr int kSettleFrames = 4;
        constexpr const char* kHostName = "##Host";
        constexpr const char* kCentreName = "Centre";
        // The ids of the two panels' windows: part of the layout a user saved
        constexpr const char* kStateWindow = "State###GamePanel/fakecontrollable/state";
        constexpr const char* kHandsWindow = "hands###GamePanel/fakecontrollable/hands";

        /// The panels of the fake game in an ImGui context with no backend, over a dock space as the editor's
        class GameEditorPanelsTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                GameManager::GetInstance().Initialize();
                SwitchToPlain();
                CreateImGui();
                _runner.SetTools(&Tools());
                _runner.Connect();
                for (const IntrusivePtr<IGame>& game : GameManager::GetInstance().GetAll()) {
                    if (auto* fake = game->Cast<FakeControllableGame>()) {
                        fake->editorTools = [this](GameEditorTools& tools) {
                            GameToolPanel state;
                            state.id = "state";
                            state.title = "State";
                            state.draw = [this] {
                                ++_stateDraws;
                                ImGui::TextUnformatted("state");
                                if (_leaveFromState) {
                                    SwitchToPlain();
                                }
                            };
                            tools.AddPanel(std::move(state));
                            GameToolPanel hands;
                            hands.id = "hands";
                            hands.draw = [this] { ++_handsDraws; };
                            tools.AddPanel(std::move(hands));
                        };
                    }
                }
                SwitchToFake();
            }

            void TearDown() override {
                ImGui::DestroyContext();
                GameManager::GetInstance().Shutdown();
            }

            static void CreateImGui() {
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = kDisplaySize;
                io.IniFilename = nullptr;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            }

            static void SwitchToPlain() {
                GameManager::GetInstance().SwitchTo("FakePlain"_intern);
            }

            static void SwitchToFake() {
                GameManager::GetInstance().SwitchTo("FakeControllable"_intern);
            }

            static GameEditorTools& Tools() {
                return GameManager::GetInstance().GetEditorTools();
            }

            /// What the editor does in a frame: the host with its dock space, a window of the editor, then the game's panels
            void Frame() {
                ImGui::GetIO().DeltaTime = kFrameSeconds;
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(kDisplaySize);
                ImGui::Begin(kHostName, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);
                ImGui::DockSpace(ImGui::GetID("Space"));
                ImGui::End();
                ImGui::Begin(kCentreName);
                ImGui::End();
                const int openWindows = ImGui::GetCurrentContext()->CurrentWindowStack.Size;
                _panels.Draw(&Tools());
                EXPECT_EQ(ImGui::GetCurrentContext()->CurrentWindowStack.Size, openWindows);
                ImGui::Render();
            }

            void Settle() {
                for (int i = 0; i < kSettleFrames; ++i) {
                    Frame();
                }
            }

            /// Splits the dock space and puts both panels into one node at its right
            ImGuiID DockPanelsAtTheRight() {
                ImGui::GetIO().DeltaTime = kFrameSeconds;
                ImGui::NewFrame();
                ImGui::Begin(kHostName);
                const ImGuiID space = ImGui::GetID("Space");
                ImGui::End();
                ImGui::DockBuilderRemoveNode(space);
                ImGui::DockBuilderAddNode(space, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(space, kDisplaySize);
                ImGuiID right = 0;
                ImGuiID centre = 0;
                ImGui::DockBuilderSplitNode(space, ImGuiDir_Right, kSideShare, &right, &centre);
                ImGui::DockBuilderDockWindow(kCentreName, centre);
                ImGui::DockBuilderDockWindow(kStateWindow, right);
                ImGui::DockBuilderDockWindow(kHandsWindow, right);
                ImGui::DockBuilderFinish(space);
                ImGui::EndFrame();
                return right;
            }

            static bool IsDockedIn(const char* name, ImGuiID node) {
                const ImGuiWindow* window = ImGui::FindWindowByName(name);
                return window != nullptr && window->DockIsActive && window->DockId == node;
            }

            GameEditorPanels _panels;
            GameCommandRunner _runner;
            int _stateDraws = 0;
            int _handsDraws = 0;
            bool _leaveFromState = false;
        };

    }  // namespace

    TEST_F(GameEditorPanelsTest, AnOpenPanelIsDrawnOnceAFrameAndAClosedOneIsNot) {
        Settle();
        const int state = _stateDraws;
        const int hands = _handsDraws;

        Frame();
        EXPECT_EQ(_stateDraws, state + 1);
        EXPECT_EQ(_handsDraws, hands + 1);

        Require(Tools().GetPanels().front()).open = false;
        Frame();
        EXPECT_EQ(_stateDraws, state + 1);
        EXPECT_EQ(_handsDraws, hands + 2);
    }

    TEST_F(GameEditorPanelsTest, NoPanelIsDrawnAfterASwitchOrAShutdown) {
        Settle();
        SwitchToPlain();
        const int state = _stateDraws;
        Settle();
        EXPECT_EQ(_stateDraws, state);

        SwitchToFake();
        Settle();
        EXPECT_GT(_stateDraws, state);

        GameManager::GetInstance().Shutdown();
        const int last = _stateDraws;
        Settle();
        EXPECT_EQ(_stateDraws, last);
    }

    TEST_F(GameEditorPanelsTest, APanelThatSwitchesTheGameEndsTheWalk) {
        Settle();
        const int state = _stateDraws;
        const int hands = _handsDraws;
        _leaveFromState = true;

        Frame();

        EXPECT_EQ(_stateDraws, state + 1);
        EXPECT_EQ(_handsDraws, hands);
        EXPECT_TRUE(Tools().GetPanels().empty());
    }

    TEST_F(GameEditorPanelsTest, ThePaletteRowOfAPanelClosesItAndBringsItBackToTheFront) {
        const ImGuiID node = DockPanelsAtTheRight();
        Settle();
        GameEditorCommands commands;
        commands.Sync(&Tools());
        ASSERT_EQ(commands.GetCommands().size(), GetEditorCommands().size() + 2);
        const EditorCommand& row = commands.GetCommands()[GetEditorCommands().size()];
        EXPECT_EQ(row.title, "FakeControllable: Panel State");
        EXPECT_EQ(row.id, "fakecontrollable.view.state"_intern);
        for (const ControlMethodListing& method : CoreManager::GetControlService().GetMethods()) {
            EXPECT_NE(method.name, "fakecontrollable.view.state");
        }

        EditorCommandEvent::Emit(row.id);
        Settle();
        const int closed = _stateDraws;
        Frame();
        EXPECT_EQ(_stateDraws, closed);
        ASSERT_TRUE(IsDockedIn(kHandsWindow, node));

        EditorCommandEvent::Emit(row.id);
        Settle();
        // The one visible tab of the node is the reopened panel
        const int state = _stateDraws;
        const int hands = _handsDraws;
        Frame();
        EXPECT_EQ(_stateDraws, state + 1);
        EXPECT_EQ(_handsDraws, hands);
    }

    TEST_F(GameEditorPanelsTest, ADockedPanelKeepsItsDockWhileAnotherGameIsActive) {
        const ImGuiID node = DockPanelsAtTheRight();
        Settle();
        ASSERT_TRUE(IsDockedIn(kStateWindow, node));

        SwitchToPlain();
        Settle();
        SwitchToFake();
        Settle();

        EXPECT_TRUE(IsDockedIn(kStateWindow, node));
        EXPECT_TRUE(IsDockedIn(kHandsWindow, node));
    }

    TEST_F(GameEditorPanelsTest, ADockedPanelKeepsItsDockInASavedLayout) {
        const ImGuiID node = DockPanelsAtTheRight();
        Settle();
        ASSERT_TRUE(IsDockedIn(kStateWindow, node));
        // Saved while another game is active, as a layout may be
        SwitchToPlain();
        Settle();
        size_t size = 0;
        const char* text = ImGui::SaveIniSettingsToMemory(&size);
        const String layout(text, size);

        ImGui::DestroyContext();
        CreateImGui();
        ImGui::LoadIniSettingsFromMemory(layout.c_str(), layout.size());
        Settle();
        SwitchToFake();
        Settle();

        EXPECT_TRUE(IsDockedIn(kStateWindow, node));
        EXPECT_TRUE(IsDockedIn(kHandsWindow, node));
    }

}  // namespace BECore::Tests
