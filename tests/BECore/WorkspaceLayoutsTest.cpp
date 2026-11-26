#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/EditorPanelRegistry.h>
#include <Widgets/EditorLayout/WorkspaceLayouts.h>
#include <Widgets/EditorLayout/WorkspaceSwitcher.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        using namespace EditorPanelIds;

        constexpr ImVec2 kDisplaySize{1600.0f, 900.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 4;

        /// A panel as the editor has it: a window that is submitted while open
        class WindowPanel : public IEditorPanel {
        public:
            explicit WindowPanel(const WorkspacePanel& panel) : id(panel.id), window(panel.window) {
                // As a fresh editor opens them: the panels of the Scene workspace
                open = MakeReadyState(EditorWorkspace::Scene).open[id];
                handle = EditorPanelRegistry::GetInstance().Register(id, *this);
            }

            [[nodiscard]] bool IsOpen() const override {
                return open;
            }
            void SetOpen(bool value) override {
                open = value;
            }

            PoolString id;
            StringView window;
            bool open = false;
            EditorPanelHandle handle;
        };

    }  // namespace

    /// The switcher over a real ImGui docking context with no backend, driven as EditorLayoutWidget drives it, with one window per engine panel
    class WorkspaceLayoutsTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _directory = TestTempDirectory("WorkspaceLayouts");
            MakeEmptyDirectory(_directory);
            StartRun();
        }

        void TearDown() override {
            EndRun();
            RemoveDirectory(_directory);
        }

        /// A new editor: a context with no ini, new panels, a switcher that reads the folder
        void StartRun() {
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = kDisplaySize;
            io.IniFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            for (const WorkspacePanel& panel : GetWorkspacePanels()) {
                _panels.push_back(MakeUnique<WindowPanel>(panel));
            }
            _switcher.emplace(_directory);
        }

        /// The editor's end: the panels die first, then the switcher stores, then the context
        void EndRun() {
            _panels.clear();
            if (_switcher) {
                _switcher->Shutdown();
                _switcher.reset();
            }
            ImGui::DestroyContext();
        }

        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            _switcher->ApplyPending();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(kDisplaySize);
            ImGui::Begin("##Host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus);
            _dockspace = ImGui::GetID("MainDockSpace");
            _switcher->Update(_dockspace, kDisplaySize);
            ImGui::DockSpace(_dockspace);
            ImGui::End();
            for (const UniquePtr<WindowPanel>& panel : _panels) {
                if (panel->open) {
                    ImGui::Begin(panel->window.data(), &panel->open);
                    ImGui::End();
                }
            }
            ImGui::EndFrame();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        void SwitchTo(EditorWorkspace workspace) {
            _switcher->SwitchTo(workspace);
            Settle();
        }

        [[nodiscard]] WindowPanel& Panel(PoolString id) {
            return *static_cast<WindowPanel*>(EditorPanelRegistry::GetInstance().Find(id));
        }

        /// The dock node the window of @p id is in, null when it floats or was never made
        [[nodiscard]] ImGuiDockNode* NodeOf(PoolString id) {
            const ImGuiWindow* const window = ImGui::FindWindowByName(Panel(id).window.data());
            ImGuiDockNode* const node = window != nullptr ? window->DockNode : nullptr;
            return node != nullptr && ImGui::DockNodeGetRootNode(node)->ID == _dockspace ? node : nullptr;
        }

        /// The user's drag of the window of @p moved onto the tabs of @p target
        void MoveWindowToNodeOf(PoolString moved, PoolString target) {
            const ImGuiDockNode* const node = NodeOf(target);
            ASSERT_NE(node, nullptr);
            ImGui::DockBuilderDockWindow(Panel(moved).window.data(), node->ID);
            Settle();
        }

        Path _directory;
        Vector<UniquePtr<WindowPanel>> _panels;
        Optional<WorkspaceSwitcher> _switcher;
        ImGuiID _dockspace = 0;
    };

    TEST_F(WorkspaceLayoutsTest, EveryReadyLayoutOpensItsPanelsAndDocksEachWhereItsSlotSays) {
        Settle();
        for (const EditorWorkspace workspace : EnumUtils<EditorWorkspace>::Values()) {
            SwitchTo(workspace);
            const String name(EnumUtils<EditorWorkspace>::ToString(workspace));
            const WorkspaceState ready = MakeReadyState(workspace);
            for (const UniquePtr<WindowPanel>& panel : _panels) {
                EXPECT_EQ(panel->open, ready.open.at(panel->id)) << name.c_str() << " " << panel->window.data();
            }
            const ImGuiDockNode* const center = NodeOf(kViewport);
            ASSERT_NE(center, nullptr) << name.c_str();
            for (const WorkspaceDock& dock : GetReadyWorkspace(workspace).docks) {
                const ImGuiDockNode* const node = NodeOf(dock.panel);
                ASSERT_NE(node, nullptr) << name.c_str() << " " << dock.panel.ToStringView().data();
                const bool left = dock.slot == WorkspaceSlot::Left || dock.slot == WorkspaceSlot::LeftBottom;
                const bool right = dock.slot == WorkspaceSlot::Right || dock.slot == WorkspaceSlot::RightBottom;
                EXPECT_EQ(node->Pos.x < center->Pos.x, left) << name.c_str() << " " << dock.panel.ToStringView().data();
                EXPECT_EQ(node->Pos.x > center->Pos.x, right) << name.c_str() << " " << dock.panel.ToStringView().data();
                // Each slot is a node of its own
                for (const WorkspaceDock& other : GetReadyWorkspace(workspace).docks) {
                    if (other.panel != dock.panel) {
                        EXPECT_NE(NodeOf(other.panel), node) << name.c_str();
                    }
                }
            }
        }
    }

    TEST_F(WorkspaceLayoutsTest, AMovedWindowAndAnOpenedPanelComeBackWithTheirWorkspace) {
        Settle();
        MoveWindowToNodeOf(kSpriteLibrary, kSceneEditor);
        ASSERT_EQ(NodeOf(kSpriteLibrary), NodeOf(kSceneEditor));
        Panel(kConsole).open = true;
        Settle();

        SwitchTo(EditorWorkspace::UI);
        EXPECT_NE(NodeOf(kSpriteLibrary), NodeOf(kSceneEditor));
        EXPECT_FALSE(Panel(kConsole).open);

        SwitchTo(EditorWorkspace::Scene);
        EXPECT_NE(NodeOf(kSpriteLibrary), nullptr);
        EXPECT_EQ(NodeOf(kSpriteLibrary), NodeOf(kSceneEditor));
        EXPECT_TRUE(Panel(kConsole).open);
    }

    TEST_F(WorkspaceLayoutsTest, TwoSwitchesBeforeAFrameStoreNothingOfTheScreenStillShown) {
        Settle();
        MoveWindowToNodeOf(kSpriteLibrary, kSceneEditor);

        _switcher->SwitchTo(EditorWorkspace::UI);
        SwitchTo(EditorWorkspace::Animation);
        SwitchTo(EditorWorkspace::UI);
        EXPECT_NE(NodeOf(kSpriteLibrary), NodeOf(kSceneEditor)) << "the UI never had the Scene's screen";

        SwitchTo(EditorWorkspace::Scene);
        EXPECT_EQ(NodeOf(kSpriteLibrary), NodeOf(kSceneEditor));
    }

    TEST_F(WorkspaceLayoutsTest, ResetBringsTheActiveWorkspaceBackToItsReadyLayoutAndPanels) {
        Settle();
        MoveWindowToNodeOf(kSpriteLibrary, kSceneEditor);
        Panel(kConsole).open = true;
        Settle();

        _switcher->Reset();
        Settle();

        EXPECT_NE(NodeOf(kSpriteLibrary), nullptr);
        EXPECT_NE(NodeOf(kSpriteLibrary), NodeOf(kSceneEditor));
        EXPECT_FALSE(Panel(kConsole).open);
    }

    TEST_F(WorkspaceLayoutsTest, TheNextRunWithoutADockTreeShowsTheActiveWorkspaceAsItWasLeft) {
        Settle();
        SwitchTo(EditorWorkspace::Profiling);
        MoveWindowToNodeOf(kProfiler, kSceneEditor);
        Panel(kActivity).open = true;
        Settle();

        EndRun();
        StartRun();
        Settle();

        EXPECT_EQ(_switcher->GetActive(), EditorWorkspace::Profiling);
        EXPECT_TRUE(Panel(kProfiler).open);
        EXPECT_TRUE(Panel(kActivity).open);
        EXPECT_FALSE(Panel(kSpriteLibrary).open);
        EXPECT_NE(NodeOf(kProfiler), nullptr);
        EXPECT_EQ(NodeOf(kProfiler), NodeOf(kSceneEditor));
    }

    TEST_F(WorkspaceLayoutsTest, ARunThatEndsWhileASwitchWaitsKeepsWhatTheSwitchStored) {
        Settle();
        MoveWindowToNodeOf(kSpriteLibrary, kSceneEditor);
        _switcher->SwitchTo(EditorWorkspace::UI);

        EndRun();
        StartRun();
        Settle();

        EXPECT_EQ(_switcher->GetActive(), EditorWorkspace::UI);
        EXPECT_NE(NodeOf(kSpriteLibrary), NodeOf(kSceneEditor)) << "the Scene's screen was never stored as the UI's";
        SwitchTo(EditorWorkspace::Scene);
        EXPECT_EQ(NodeOf(kSpriteLibrary), NodeOf(kSceneEditor));
    }

    TEST_F(WorkspaceLayoutsTest, ARunWithNoFileKeepsTheTreeAndThePanelsItStartsWith) {
        // What imgui.ini gives: a tree that is not the ready one
        Settle();
        MoveWindowToNodeOf(kSpriteLibrary, kSceneEditor);
        size_t size = 0;
        const char* text = ImGui::SaveIniSettingsToMemory(&size);
        const String ini(text, size);
        EndRun();
        RemoveDirectory(_directory);
        MakeEmptyDirectory(_directory);

        StartRun();
        ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
        Panel(kConsole).open = true;
        Settle();

        EXPECT_EQ(NodeOf(kSpriteLibrary), NodeOf(kSceneEditor));
        EXPECT_TRUE(Panel(kConsole).open);
        EXPECT_FALSE(GetFileInfo(_directory / kWorkspacesFileName)) << "nothing changed, so nothing is written yet";
    }

    TEST_F(WorkspaceLayoutsTest, ASettledFrameAllocatesNothing) {
        Settle();
        SwitchTo(EditorWorkspace::UI);

        ExpectNoAllocations([&] { Settle(); });
    }

    TEST_F(WorkspaceLayoutsTest, ARunThatNeverDrewStoresNothing) {
        EndRun();

        EXPECT_FALSE(GetFileInfo(_directory / kWorkspacesFileName));
        StartRun();
    }

    TEST_F(WorkspaceLayoutsTest, ALoadedLayoutBecomesTheActiveWorkspaces) {
        Settle();
        MoveWindowToNodeOf(kSpriteLibrary, kSceneEditor);
        size_t size = 0;
        const char* text = ImGui::SaveIniSettingsToMemory(&size);
        const String named(text, size);
        SwitchTo(EditorWorkspace::Animation);

        _switcher->LoadLayout(named);
        Settle();
        EXPECT_EQ(NodeOf(kSpriteLibrary), NodeOf(kSceneEditor));

        SwitchTo(EditorWorkspace::UI);
        SwitchTo(EditorWorkspace::Animation);
        EXPECT_NE(NodeOf(kSpriteLibrary), nullptr);
        EXPECT_EQ(NodeOf(kSpriteLibrary), NodeOf(kSceneEditor));
    }

}  // namespace BECore::Tests
