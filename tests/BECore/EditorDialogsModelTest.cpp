#include <BECore/EditorLayoutHostProbe.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorDialogsModel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kName = "Wide";
        constexpr StringView kBadName = "bad/name";
        constexpr StringView kOldText = "an older layout";
        constexpr StringView kBlockedName = "blocked";

    }  // namespace

    /// The Save Layout dialog's model over a fake host with a folder of its own and no ImGui
    class EditorDialogsModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _directory = TestTempDirectory("EditorDialogsModel");
            MakeEmptyDirectory(_directory);
            _host.emplace(_directory);
            _handle = EditorLayoutHostRegistry::GetInstance().Register(*_host);
            _model = New<EditorDialogsModel>();
        }

        void TearDown() override {
            _model.Reset();
            _handle.Reset();
            _host.reset();
            RemoveDirectory(_directory);
        }

        EditorDialogsView& View() {
            return _model->GetView();
        }

        template <typename TId>
        bool Command(const TId& id) {
            return _model->RunCommand(id.View(), {});
        }

        /// What the input does in a frame: writes the field, then the model reads it
        void Type(StringView name) {
            View().layoutName.assign(name.data(), name.size());
            _model->Update();
        }

        void OpenDialog() {
            ASSERT_TRUE(Command(EditorCommandIds::kViewSaveLayout));
            _model->Update();
            ASSERT_TRUE(View().saveLayoutOpen);
        }

        Path _directory;
        Optional<ProbeEditorLayoutHost> _host;
        EditorLayoutHostHandle _handle;
        IntrusivePtr<EditorDialogsModel> _model;
    };

    TEST_F(EditorDialogsModelTest, TheSaveLayoutCommandOpensTheDialogWithAnEmptyNameAndNoError) {
        View().layoutName = "left over";
        View().error = "old";
        View().hasError = true;

        ASSERT_NO_FATAL_FAILURE(OpenDialog());

        EXPECT_TRUE(View().layoutName.empty());
        EXPECT_FALSE(View().hasError);
        EXPECT_FALSE(View().canSave);
        EXPECT_FALSE(View().hasWarning) << "an empty name is not a mistake yet";
        EXPECT_FALSE(View().noFolder);
    }

    TEST_F(EditorDialogsModelTest, AnInvalidNameWarnsAndCannotBeSaved) {
        ASSERT_NO_FATAL_FAILURE(OpenDialog());

        Type(kBadName);

        EXPECT_FALSE(View().canSave);
        EXPECT_TRUE(View().hasWarning);
        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsSaveDialogSave));
        EXPECT_TRUE(View().saveLayoutOpen);
        EXPECT_TRUE(_host->layouts->List().empty());
    }

    TEST_F(EditorDialogsModelTest, ANewNameIsSavedAndClosesTheDialog) {
        ASSERT_NO_FATAL_FAILURE(OpenDialog());
        Type(kName);
        EXPECT_TRUE(View().canSave);
        EXPECT_FALSE(View().exists);

        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsSaveDialogSave));

        EXPECT_FALSE(View().saveLayoutOpen);
        EXPECT_TRUE(_host->layouts->Exists(kName));
    }

    TEST_F(EditorDialogsModelTest, AnExistingNameWarnsAndOnlyTheSaveButtonReplacesItNotEnter) {
        ASSERT_TRUE(_host->layouts->Save(kName, kOldText));
        ASSERT_NO_FATAL_FAILURE(OpenDialog());
        Type(kName);
        EXPECT_TRUE(View().exists);
        EXPECT_TRUE(View().hasWarning);

        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsSaveDialogEnter));
        EXPECT_TRUE(View().saveLayoutOpen) << "Enter never replaces a layout";

        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsSaveDialogSave));
        EXPECT_FALSE(View().saveLayoutOpen);
        EXPECT_EQ(_host->layouts->Load(kName).value(), kProbeLayoutText) << "the Save button replaced the layout";
    }

    TEST_F(EditorDialogsModelTest, EnterSavesANewValidName) {
        ASSERT_NO_FATAL_FAILURE(OpenDialog());
        Type(kName);

        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsSaveDialogEnter));

        EXPECT_FALSE(View().saveLayoutOpen);
        EXPECT_TRUE(_host->layouts->Exists(kName));
    }

    TEST_F(EditorDialogsModelTest, ANameTypedAndSavedInOneFrameIsJudgedByWhatTheFieldHolds) {
        ASSERT_NO_FATAL_FAILURE(OpenDialog());
        // The click comes in the frame of the last keystroke, before the model's Update has read the field
        View().layoutName.assign(kName.data(), kName.size());

        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsSaveDialogSave));

        EXPECT_TRUE(_host->layouts->Exists(kName));
    }

    TEST_F(EditorDialogsModelTest, AFailedSaveKeepsTheDialogOpenWithTheSentence) {
        MakeDirectory(_directory / kLayoutsFolderName);
        MakeDirectory(_directory / kLayoutsFolderName / "blocked.ini");
        ASSERT_NO_FATAL_FAILURE(OpenDialog());
        Type(kBlockedName);

        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsSaveDialogSave));

        EXPECT_TRUE(View().saveLayoutOpen);
        EXPECT_TRUE(View().hasError);
        EXPECT_FALSE(View().error.empty());

        // A second try with a good name goes through and drops the sentence
        Type(kName);
        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsSaveDialogSave));
        EXPECT_FALSE(View().saveLayoutOpen);
        EXPECT_FALSE(View().hasError);
    }

    TEST_F(EditorDialogsModelTest, CancelClosesTheDialogAndSavesNothing) {
        ASSERT_NO_FATAL_FAILURE(OpenDialog());
        Type(kName);

        EXPECT_TRUE(Command(EditorCommandIds::kLayoutsSaveDialogCancel));

        EXPECT_FALSE(View().saveLayoutOpen);
        EXPECT_TRUE(_host->layouts->List().empty());
    }

    TEST_F(EditorDialogsModelTest, WithNoLayoutsFolderTheCommandOpensNothing) {
        _host->layouts.reset();

        EXPECT_TRUE(Command(EditorCommandIds::kViewSaveLayout));
        _model->Update();

        EXPECT_FALSE(View().saveLayoutOpen);
        EXPECT_TRUE(View().noFolder);
    }

    TEST_F(EditorDialogsModelTest, AHostThatGoesAwayWhileTheDialogIsOpenClosesIt) {
        ASSERT_NO_FATAL_FAILURE(OpenDialog());

        _handle.Reset();
        _model->Update();

        EXPECT_FALSE(View().saveLayoutOpen);
    }

    TEST_F(EditorDialogsModelTest, ACommandOfAnotherPanelIsNotTaken) {
        EXPECT_FALSE(_model->RunCommand("preferences.commit", {}));
    }

}  // namespace BECore::Tests
