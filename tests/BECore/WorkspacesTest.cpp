#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/EditorPanelIds.h>
#include <Widgets/EditorLayout/Workspaces.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using namespace EditorPanelIds;

        constexpr StringView kShownScene = "[Docking][Data]\n; the Scene screen\n";
        constexpr StringView kShownUI = "[Docking][Data]\n; the UI screen\n";

        WorkspaceState Shown(StringView layout, PoolString panel, bool open) {
            WorkspaceState state{String(layout), {}};
            state.open[panel] = open;
            return state;
        }

        /// The flag @p state lists for @p panel; nothing when it does not list it
        Optional<bool> Flag(const WorkspaceState& state, PoolString panel) {
            const auto found = state.open.find(panel);
            return found != state.open.end() ? Optional<bool>(found->second) : std::nullopt;
        }

    }  // namespace

    class WorkspacesTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _directory = TestTempDirectory("Workspaces");
            MakeEmptyDirectory(_directory);
        }

        void TearDown() override {
            RemoveDirectory(_directory);
        }

        [[nodiscard]] Path File() const {
            return _directory / kWorkspacesFileName;
        }

        Path _directory;
    };

    TEST_F(WorkspacesTest, WithNoFileTheSceneWorkspaceHasNothingStoredAndLoadWritesNone) {
        Workspaces model(_directory);
        model.Load();

        EXPECT_EQ(model.GetActive(), EditorWorkspace::Scene);
        EXPECT_FALSE(model.IsPending());
        EXPECT_EQ(model.FindStored(EditorWorkspace::Scene), nullptr);
        EXPECT_FALSE(GetFileInfo(File()));
    }

    TEST_F(WorkspacesTest, ASwitchStoresTheScreenIntoTheWorkspaceLeftAndTheWayBackAnswersIt) {
        Workspaces model(_directory);
        const WorkspaceState shown = Shown(kShownScene, kConsole, true);

        const Optional<WorkspaceState> ui = model.SwitchTo(EditorWorkspace::UI, &shown);
        ASSERT_TRUE(ui);
        EXPECT_TRUE(ui->layout.empty()) << "a workspace never left gets its ready layout";
        EXPECT_EQ(Flag(*ui, kAssetBrowser), true);
        EXPECT_EQ(Flag(*ui, kConsole), false);
        EXPECT_TRUE(model.IsPending());
        model.MarkApplied();

        const WorkspaceState uiShown = Shown(kShownUI, kConsole, false);
        const Optional<WorkspaceState> scene = model.SwitchTo(EditorWorkspace::Scene, &uiShown);
        ASSERT_TRUE(scene);
        EXPECT_EQ(StringView(scene->layout), kShownScene);
        EXPECT_EQ(Flag(*scene, kConsole), true);
        EXPECT_EQ(Flag(*scene, kViewport), std::nullopt) << "the stored map says nothing about a panel it does not list";
    }

    TEST_F(WorkspacesTest, WhileAnAnswerWaitsAFurtherSwitchOnlyRetargetsAndStoresNothing) {
        Workspaces model(_directory);
        const WorkspaceState shown = Shown(kShownScene, kConsole, true);
        ASSERT_TRUE(model.SwitchTo(EditorWorkspace::UI, &shown));

        // The screen still shows Scene, so none of this is the UI's
        const WorkspaceState stale = Shown(kShownUI, kConsole, false);
        ASSERT_TRUE(model.SwitchTo(EditorWorkspace::Profiling, &stale));
        model.StoreLayout(kShownUI);
        EXPECT_FALSE(model.NoteOpen(kConsole, false));

        EXPECT_EQ(model.GetActive(), EditorWorkspace::Profiling);
        EXPECT_EQ(model.FindStored(EditorWorkspace::UI), nullptr);
        EXPECT_EQ(model.FindStored(EditorWorkspace::Profiling), nullptr);
        ASSERT_NE(model.FindStored(EditorWorkspace::Scene), nullptr);
        EXPECT_EQ(StringView(model.FindStored(EditorWorkspace::Scene)->layout), kShownScene);
    }

    TEST_F(WorkspacesTest, ASwitchToTheActiveWorkspaceDoesNothing) {
        Workspaces model(_directory);
        const WorkspaceState shown = Shown(kShownScene, kConsole, true);

        EXPECT_FALSE(model.SwitchTo(EditorWorkspace::Scene, &shown));
        EXPECT_FALSE(model.IsPending());
        EXPECT_EQ(model.FindStored(EditorWorkspace::Scene), nullptr);
    }

    TEST_F(WorkspacesTest, AStoreKeepsWhatItHadOfAPanelThatIsNotAlive) {
        Workspaces model(_directory);
        EXPECT_FALSE(model.NoteOpen(kGenerate, true));
        const WorkspaceState shown = Shown(kShownScene, kConsole, true);
        ASSERT_TRUE(model.SwitchTo(EditorWorkspace::UI, &shown));

        EXPECT_EQ(Flag(*model.FindStored(EditorWorkspace::Scene), kGenerate), true);
    }

    TEST_F(WorkspacesTest, OnlyAChangedFlagIsAChange) {
        Workspaces model(_directory);

        EXPECT_FALSE(model.NoteOpen(kConsole, false)) << "the first note of a panel";
        EXPECT_FALSE(model.NoteOpen(kConsole, false));
        EXPECT_TRUE(model.NoteOpen(kConsole, true));
        EXPECT_EQ(Flag(*model.FindStored(EditorWorkspace::Scene), kConsole), true);
    }

    TEST_F(WorkspacesTest, ResetForgetsTheActiveWorkspaceAloneAndAnswersItsReadyState) {
        Workspaces model(_directory);
        const WorkspaceState shown = Shown(kShownScene, kConsole, true);
        ASSERT_TRUE(model.SwitchTo(EditorWorkspace::UI, &shown));
        model.MarkApplied();
        model.StoreLayout(kShownUI);

        const WorkspaceState ready = model.Reset();

        EXPECT_TRUE(ready.layout.empty());
        EXPECT_EQ(Flag(ready, kAssetBrowser), true);
        EXPECT_EQ(Flag(ready, kProfiler), false);
        EXPECT_TRUE(model.IsPending());
        EXPECT_EQ(model.FindStored(EditorWorkspace::UI), nullptr);
        EXPECT_NE(model.FindStored(EditorWorkspace::Scene), nullptr);
    }

    TEST_F(WorkspacesTest, AStoredTextWithNoDockTreeGivesTheReadyState) {
        Workspaces model(_directory);
        const WorkspaceState shown = Shown("[Window][Viewport]\n", kConsole, true);
        ASSERT_TRUE(model.SwitchTo(EditorWorkspace::UI, &shown));
        model.MarkApplied();

        const Optional<WorkspaceState> scene = model.SwitchTo(EditorWorkspace::Scene, nullptr);

        ASSERT_TRUE(scene);
        EXPECT_TRUE(scene->layout.empty());
        EXPECT_EQ(Flag(*scene, kConsole), false);
    }

    TEST_F(WorkspacesTest, AStoredLayoutGetsTheNewWindowNames) {
        Workspaces model(_directory);
        model.LoadLayout("[Window][Config Editor]\nDockId=0x1,0\n\n[Docking][Data]\n");

        const WorkspaceState state = model.Restore();

        EXPECT_NE(state.layout.find("[Window][ConfigEditor]"), String::npos) << state.layout.c_str();
    }

    TEST_F(WorkspacesTest, TheNextRunReadsTheActiveWorkspaceAndWhatWasStored) {
        {
            Workspaces model(_directory);
            const WorkspaceState shown = Shown(kShownScene, kConsole, true);
            ASSERT_TRUE(model.SwitchTo(EditorWorkspace::Animation, &shown));
            model.MarkApplied();
            EXPECT_FALSE(model.NoteOpen(kProfiler, true));
            ASSERT_TRUE(model.Save());
        }
        Workspaces model(_directory);
        model.Load();

        EXPECT_EQ(model.GetActive(), EditorWorkspace::Animation);
        const WorkspaceState* const scene = model.FindStored(EditorWorkspace::Scene);
        ASSERT_NE(scene, nullptr);
        EXPECT_EQ(StringView(scene->layout), kShownScene);
        EXPECT_EQ(Flag(*scene, kConsole), true);
        // Only flags are stored of the active one: its ready layout stands in, with those flags over the ready ones
        const WorkspaceState animation = model.Restore();
        EXPECT_TRUE(animation.layout.empty());
        EXPECT_EQ(Flag(animation, kProfiler), true);
        EXPECT_EQ(Flag(animation, kAssetBrowser), true);
    }

    TEST_F(WorkspacesTest, WhatThisBuildDoesNotKnowSurvivesASaveAndAReset) {
        WriteFile(File(), R"({"active": "Scene", "later": [1, 2], "workspaces": {
            "Scene": {"open": {"Plugin": true}, "pinned": "yes"},
            "Debugging": {"layout": "x"}}})");
        Workspaces model(_directory);
        model.Load();
        static_cast<void>(model.Reset());
        ASSERT_TRUE(model.Save());

        const String saved = ReadFile(File());
        EXPECT_NE(saved.find("\"later\""), String::npos) << saved.c_str();
        EXPECT_NE(saved.find("\"pinned\""), String::npos) << saved.c_str();
        EXPECT_NE(saved.find("\"Debugging\""), String::npos) << saved.c_str();
        EXPECT_EQ(saved.find("\"Plugin\""), String::npos) << "the reset forgets the open flags";
    }

    TEST_F(WorkspacesTest, AnUnknownPanelOfTheFileIsKept) {
        WriteFile(File(), R"({"workspaces": {"Scene": {"open": {"Plugin": true}}}})");
        Workspaces model(_directory);
        model.Load();
        ASSERT_TRUE(model.Save());

        Workspaces again(_directory);
        again.Load();
        ASSERT_NE(again.FindStored(EditorWorkspace::Scene), nullptr);
        EXPECT_EQ(Flag(*again.FindStored(EditorWorkspace::Scene), "Plugin"_intern), true);
    }

    TEST_F(WorkspacesTest, AFileThatDoesNotParseGivesTheDefaultsAndTheFirstSaveKeepsIt) {
        WriteFile(File(), "{ not json");
        Workspaces model(_directory);
        model.Load();

        EXPECT_EQ(model.GetActive(), EditorWorkspace::Scene);
        EXPECT_EQ(ReadFile(File()), "{ not json") << "Load never writes";
        ASSERT_TRUE(model.Save());
        EXPECT_EQ(ReadFile(_directory / "workspaces.json.bad"), "{ not json");
    }

    TEST_F(WorkspacesTest, AValueOfAWrongTypeGivesItsDefault) {
        WriteFile(File(), R"({"active": 3, "workspaces": {"Scene": {"layout": 7, "open": {"Console": "yes", "Profiler": true}}, "UI": []}})");
        Workspaces model(_directory);
        model.Load();

        EXPECT_EQ(model.GetActive(), EditorWorkspace::Scene);
        EXPECT_EQ(model.FindStored(EditorWorkspace::UI), nullptr);
        const WorkspaceState* const scene = model.FindStored(EditorWorkspace::Scene);
        ASSERT_NE(scene, nullptr);
        EXPECT_TRUE(scene->layout.empty());
        EXPECT_EQ(Flag(*scene, kConsole), std::nullopt);
        EXPECT_EQ(Flag(*scene, kProfiler), true);
    }

    TEST_F(WorkspacesTest, WithoutAFolderItWorksInMemory) {
        Workspaces model{Path()};
        model.Load();
        const WorkspaceState shown = Shown(kShownScene, kConsole, true);
        ASSERT_TRUE(model.SwitchTo(EditorWorkspace::UI, &shown));
        model.MarkApplied();

        EXPECT_FALSE(model.Save());
        const Optional<WorkspaceState> scene = model.SwitchTo(EditorWorkspace::Scene, nullptr);
        ASSERT_TRUE(scene);
        EXPECT_EQ(StringView(scene->layout), kShownScene);
    }

    TEST_F(WorkspacesTest, EveryPanelAReadyDefinitionOpensHasADockSlotAndAWindowName) {
        for (const EditorWorkspace workspace : EnumUtils<EditorWorkspace>::Values()) {
            const WorkspaceState state = MakeReadyState(workspace);
            EXPECT_EQ(state.open.size(), GetWorkspacePanels().size()) << "a ready definition docks only panels of the table";
            EXPECT_EQ(Flag(state, kViewport), true);
            size_t open = 0;
            for (const auto& [panel, isOpen] : state.open) {
                open += isOpen ? 1 : 0;
            }
            EXPECT_EQ(open, GetReadyWorkspace(workspace).docks.size()) << "one dock slot per open panel";
        }
    }

}  // namespace BECore::Tests
