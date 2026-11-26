#include <BECore/EditorLayoutHostProbe.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/MainToolbarModel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// The toolbar's model over a fake host whose play model the test drives, with no ImGui
    class MainToolbarModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _directory = TestTempDirectory("MainToolbarModel");
            MakeEmptyDirectory(_directory);
            _host.emplace(_directory);
            _handle = EditorLayoutHostRegistry::GetInstance().Register(*_host);
            _model = New<MainToolbarModel>();
        }

        void TearDown() override {
            _model.Reset();
            _handle.Reset();
            _host.reset();
            RemoveDirectory(_directory);
        }

        const MainToolbarView& Updated() {
            _model->Update();
            return _model->GetView();
        }

        Path _directory;
        Optional<ProbeEditorLayoutHost> _host;
        EditorLayoutHostHandle _handle;
        IntrusivePtr<MainToolbarModel> _model;
    };

    TEST_F(MainToolbarModelTest, ThePlayStateIsCopiedIntoTheFlagsTheButtonsRead) {
        const MainToolbarView& stopped = Updated();
        EXPECT_TRUE(stopped.stopped);
        EXPECT_FALSE(stopped.playing);
        EXPECT_FALSE(stopped.paused);
        EXPECT_TRUE(stopped.canPause);
        EXPECT_FALSE(stopped.canStep);

        _host->playModel.PressPlay();
        const MainToolbarView& playing = Updated();
        EXPECT_FALSE(playing.stopped);
        EXPECT_TRUE(playing.playing);
        EXPECT_FALSE(playing.canStep);

        _host->playModel.PressPause();
        const MainToolbarView& paused = Updated();
        EXPECT_TRUE(paused.paused);
        EXPECT_FALSE(paused.stopped);
        EXPECT_TRUE(paused.canStep);
    }

    TEST_F(MainToolbarModelTest, WithoutAHostOrAPlayModelTheGameIsStoppedAndNothingCanBePressed) {
        _host->playModel.PressPause();
        ASSERT_TRUE(Updated().paused);

        _host->hasPlayModel = false;
        const MainToolbarView& noModel = Updated();
        EXPECT_TRUE(noModel.stopped);
        EXPECT_FALSE(noModel.paused);
        EXPECT_FALSE(noModel.canPause);
        EXPECT_FALSE(noModel.canStep);

        _host->hasPlayModel = true;
        ASSERT_TRUE(Updated().paused);
        _handle.Reset();
        const MainToolbarView& noHost = Updated();
        EXPECT_TRUE(noHost.stopped);
        EXPECT_FALSE(noHost.canPause);
    }

    TEST_F(MainToolbarModelTest, TheActiveWorkspaceIsReadFromTheHostAndThereIsNoneWithoutOne) {
        _host->workspace = EditorWorkspace::Animation;
        EXPECT_TRUE(Updated().hasWorkspace);
        EXPECT_EQ(Updated().workspace, EditorWorkspace::Animation);

        // The workspaces do not depend on the play model
        _host->hasPlayModel = false;
        EXPECT_TRUE(Updated().hasWorkspace);

        _host->workspace.reset();
        EXPECT_FALSE(Updated().hasWorkspace);

        _host->workspace = EditorWorkspace::UI;
        _handle.Reset();
        EXPECT_FALSE(Updated().hasWorkspace);
    }

    TEST_F(MainToolbarModelTest, TheModelRunsNoCommandBecauseTheLayoutWidgetOwnsThem) {
        EXPECT_FALSE(_model->RunCommand("game.play", {}));
        EXPECT_FALSE(_model->RunCommand("game.pause", {}));
    }

}  // namespace BECore::Tests
