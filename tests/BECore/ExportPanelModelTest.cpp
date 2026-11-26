#include <BECore/ExportFakes.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        bool Contains(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        class ExportPanelModelTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                _model = MakeUnique<ExportPanelModel>();
                _fixture.Apply(*_model);
            }

            void Run(PoolString command) {
                EXPECT_TRUE(_model->RunCommand(command.ToStringView(), {}));
            }

            /// Mutable, as the controls of the panel write it
            ExportPanelView& View() {
                return const_cast<ExportPanelView&>(_model->GetView());
            }

            /// A started export that ended with a package in @p folder
            void Succeed(StringView folder) {
                Run(EditorCommandIds::kExportStart);
                _fixture.launcher.End(0, Format("== export ok: {}\n", folder));
                _model->Update();
            }

            ExportFixture _fixture{"export_model"};
            UniquePtr<ExportPanelModel> _model;
        };

    }  // namespace

    TEST_F(ExportPanelModelTest, StartRunsTheScriptOnceOverTheProgramWithTheChoices) {
        EXPECT_EQ(_model->GetState(), ExportState::Idle);
        EXPECT_TRUE(View().canStart);
        EXPECT_FALSE(View().canCancel);
        View().config = ExportConfig::Release;
        View().outputDirectory = "  /my out/folder ";

        Run(EditorCommandIds::kExportStart);
        Run(EditorCommandIds::kExportStart);

        EXPECT_EQ(_fixture.launcher.starts, 1);
        EXPECT_EQ(_model->GetState(), ExportState::Running);
        EXPECT_TRUE(View().running);
        EXPECT_FALSE(View().canStart);
        EXPECT_TRUE(View().canCancel);
        EXPECT_EQ(_fixture.launcher.logPath, _fixture.logPath);
        const Vector<String> expected{_fixture.launcher.started.at(0), _fixture.script, "build", "--exe", _fixture.program, "--config", "release", "--out", "/my out/folder",
                                      "--cancel-on-stdin-close"};
        EXPECT_EQ(_fixture.launcher.started, expected);
    }

    TEST_F(ExportPanelModelTest, TheOutputIsFollowedAndASuccessOffersItsFolder) {
        Run(EditorCommandIds::kExportStart);
        _fixture.launcher.Write("== step 1/3: configure\nA.cpp:1: warning: w\n");
        _model->Update();
        EXPECT_EQ(_model->GetLog().GetLineCount(), 2u);
        EXPECT_EQ(View().warningCount, 1u);
        EXPECT_EQ(View().countsText, "Errors: 0, warnings: 1");
        EXPECT_FALSE(View().hasResult);

        Run(EditorCommandIds::kExportOpenFolder);
        Run(EditorCommandIds::kExportRun);
        EXPECT_TRUE(_fixture.openedUrl.empty());
        EXPECT_EQ(_fixture.launcher.detachedStarts, 0);

        // The last line comes with the end of the process and has no line end
        _fixture.launcher.End(0, "== export ok: /my games/dist");
        _model->Update();

        EXPECT_EQ(_model->GetState(), ExportState::Succeeded);
        EXPECT_EQ(View().statusStyle, PanelTextStyle::Success);
        EXPECT_TRUE(View().hasResult);
        EXPECT_EQ(View().resultFolder, "/my games/dist");
        EXPECT_TRUE(View().canStart);
        EXPECT_FALSE(View().canCancel);

        Run(EditorCommandIds::kExportOpenFolder);
        EXPECT_EQ(_fixture.openedUrl, "file:///my%20games/dist");
        Run(EditorCommandIds::kExportRun);
        ASSERT_EQ(_fixture.launcher.detachedStarts, 1);
        const Vector<String> expected{_fixture.launcher.started.at(0), _fixture.script, "run", "--dir", "/my games/dist"};
        EXPECT_EQ(_fixture.launcher.detached, expected);
    }

    TEST_F(ExportPanelModelTest, AResultThatCannotBeOpenedOrRunSaysSo) {
        Succeed("/pkg");
        _fixture.openerRefusal = "no file manager";
        Run(EditorCommandIds::kExportOpenFolder);
        _model->Update();
        EXPECT_TRUE(Contains(View().status, "no file manager")) << View().status.c_str();
        EXPECT_EQ(View().statusStyle, PanelTextStyle::Error);
        EXPECT_TRUE(View().hasResult);

        _fixture.launcher.refusal = "no python";
        Run(EditorCommandIds::kExportRun);
        EXPECT_TRUE(Contains(View().status, "no python")) << View().status.c_str();

        _fixture.launcher.refusal.clear();
        Run(EditorCommandIds::kExportRun);
        EXPECT_EQ(View().statusStyle, PanelTextStyle::Success);
    }

    TEST_F(ExportPanelModelTest, AFailedExportGivesTheReasonOfTheScriptOrItsExitCode) {
        Run(EditorCommandIds::kExportStart);
        _fixture.launcher.End(1, "== export failed: build failed with exit code 1\n");
        _model->Update();
        EXPECT_EQ(_model->GetState(), ExportState::Failed);
        EXPECT_TRUE(Contains(View().status, "build failed with exit code 1")) << View().status.c_str();
        EXPECT_FALSE(View().hasResult);
        EXPECT_EQ(View().errorCount, 1u);

        // A script that dies before its result line
        Run(EditorCommandIds::kExportStart);
        _fixture.launcher.End(7);
        _model->Update();
        EXPECT_EQ(_model->GetState(), ExportState::Failed);
        EXPECT_TRUE(Contains(View().status, "exit code 7")) << View().status.c_str();
    }

    TEST_F(ExportPanelModelTest, CancelAsksTheProcessAndTheStateWaitsForItsEnd) {
        Run(EditorCommandIds::kExportStart);
        Run(EditorCommandIds::kExportCancel);
        EXPECT_TRUE(_fixture.launcher.run.cancelled);
        EXPECT_EQ(_model->GetState(), ExportState::Running);
        EXPECT_FALSE(View().canCancel);
        EXPECT_EQ(View().status, "Cancelling...");

        _fixture.launcher.End(kExportExitCancelled, "== export cancelled\n");
        _model->Update();
        EXPECT_EQ(_model->GetState(), ExportState::Cancelled);
        EXPECT_TRUE(View().canStart);
        EXPECT_FALSE(_fixture.launcher.run.alive);

        // One that had to be killed was cancelled too
        Run(EditorCommandIds::kExportStart);
        EXPECT_TRUE(View().canCancel);
        Run(EditorCommandIds::kExportCancel);
        _fixture.launcher.End(-9);
        _model->Update();
        EXPECT_EQ(_model->GetState(), ExportState::Cancelled);
    }

    TEST_F(ExportPanelModelTest, AnExportThatCannotStartSaysWhyAndCanBeStartedAgain) {
        _fixture.launcher.refusal = "no python here";
        Run(EditorCommandIds::kExportStart);
        EXPECT_EQ(_model->GetState(), ExportState::Failed);
        EXPECT_TRUE(Contains(View().status, "no python here")) << View().status.c_str();
        EXPECT_TRUE(View().canStart);

        _fixture.launcher.refusal.clear();
        _fixture.commandLine.SetProgramPath("");
        Run(EditorCommandIds::kExportStart);
        EXPECT_EQ(_model->GetState(), ExportState::Failed);
        EXPECT_EQ(_fixture.launcher.starts, 0);

        _fixture.commandLine.SetProgramPath(_fixture.program);
        RemoveDirectory(Path(_fixture.folder.PathOf("engine/tools")));
        Run(EditorCommandIds::kExportStart);
        EXPECT_TRUE(Contains(View().status, kExportScript)) << View().status.c_str();
        EXPECT_EQ(_fixture.launcher.starts, 0);

        WriteFile(Path(_fixture.script), "");
        Run(EditorCommandIds::kExportStart);
        EXPECT_EQ(_model->GetState(), ExportState::Running);
    }

    TEST_F(ExportPanelModelTest, ANewExportForgetsTheLastResultAndOutput) {
        Succeed("/pkg");
        ASSERT_TRUE(View().hasResult);

        Run(EditorCommandIds::kExportStart);
        _model->Update();
        EXPECT_FALSE(View().hasResult);
        EXPECT_TRUE(View().resultFolder.empty());
        EXPECT_EQ(_model->GetLog().GetLineCount(), 0u);

        // A success without a result line has nothing to open
        _fixture.launcher.End(0);
        _model->Update();
        EXPECT_FALSE(View().hasResult);
    }

    TEST_F(ExportPanelModelTest, DestroyingTheModelCancelsARunningExport) {
        Run(EditorCommandIds::kExportStart);
        _model->OnOpenChanged(false);
        EXPECT_FALSE(_fixture.launcher.run.cancelled);

        _model.reset();
        EXPECT_TRUE(_fixture.launcher.run.cancelled);
        EXPECT_FALSE(_fixture.launcher.run.alive);
    }

    TEST_F(ExportPanelModelTest, UnsavedDocumentsAreSaidWhenThePanelOpensAndThePointerComesBack) {
        _fixture.dirty = true;
        _model->OnOpenChanged(true);
        _model->Update();
        EXPECT_TRUE(View().hasUnsaved);

        _fixture.dirty = false;
        _model->NotePointerOver(true);
        _model->Update();
        EXPECT_FALSE(View().hasUnsaved);

        _fixture.dirty = true;
        Run(EditorCommandIds::kExportStart);
        EXPECT_TRUE(View().hasUnsaved);
        EXPECT_EQ(_model->GetState(), ExportState::Running) << "unsaved changes do not stop an export";
    }

    TEST_F(ExportPanelModelTest, AnUpdateWithNoNewOutputAllocatesNothing) {
        Run(EditorCommandIds::kExportStart);
        _fixture.launcher.Write("one\n");
        _model->Update();
        _model->Update();
        ExpectNoAllocations([&] {
            for (int frame = 0; frame < 8; ++frame) {
                _model->Update();
            }
        });
    }

}  // namespace BECore::Tests
