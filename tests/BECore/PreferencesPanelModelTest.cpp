#include <BECore/EditorLayoutHostProbe.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/PreferencesPanelModel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kSteadyFrames = 8;
        constexpr float kFontScaleOutOfRange = 9.0f;
        constexpr int32_t kGridStep = 64;
        constexpr float kNewFontScale = 1.5f;

        /// What a fresh start of the editor reads from the folder
        EditorPreferences StoredPreferences(const Path& folder) {
            EditorSettings settings(folder);
            settings.Load();
            return settings.GetPreferences();
        }

    }  // namespace

    /// The General tab's model over a fake host with a folder of its own and no ImGui
    class PreferencesPanelModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _directory = TestTempDirectory("PreferencesPanelModel");
            MakeEmptyDirectory(_directory);
            _host.emplace(_directory);
            _handle = EditorLayoutHostRegistry::GetInstance().Register(*_host);
            _model = New<PreferencesPanelModel>();
            _model->OnOpenChanged(true);
            _model->Update();
        }

        void TearDown() override {
            _model.Reset();
            _handle.Reset();
            _host.reset();
            RemoveDirectory(_directory);
        }

        PreferencesPanelView& View() {
            return _model->GetView();
        }

        bool Command(StringView id) {
            return _model->RunCommand(id, {});
        }

        bool PreferencesFileExists() const {
            return PathExists(_directory / "preferences.json");
        }

        Path _directory;
        Optional<ProbeEditorLayoutHost> _host;
        EditorLayoutHostHandle _handle;
        IntrusivePtr<PreferencesPanelModel> _model;
    };

    TEST_F(PreferencesPanelModelTest, TheViewStartsWithTheStoredPreferences) {
        _handle.Reset();
        EditorPreferences stored;
        stored.fontScale = kNewFontScale;
        stored.viewportGridStep = kGridStep;
        stored.externalEditor = "code -g {file}:{line}";
        _host->settings->SetPreferences(stored);
        _handle = EditorLayoutHostRegistry::GetInstance().Register(*_host);

        _model->OnOpenChanged(true);

        EXPECT_FALSE(View().noFolder);
        EXPECT_EQ(View().fontScale, kNewFontScale);
        EXPECT_EQ(View().viewportGridStep, kGridStep);
        EXPECT_EQ(View().externalEditor, "code -g {file}:{line}");
    }

    TEST_F(PreferencesPanelModelTest, AChangedFontScaleIsAppliedAtOnceAndSavedOnlyOnCommit) {
        View().fontScale = kNewFontScale;
        _model->Update();

        EXPECT_EQ(_host->settings->GetPreferences().fontScale, kNewFontScale);
        EXPECT_EQ(_host->preferencesNotified, 1);
        EXPECT_FALSE(PreferencesFileExists());

        EXPECT_TRUE(Command(EditorCommandIds::kPreferencesCommit.View()));

        EXPECT_EQ(StoredPreferences(_directory).fontScale, kNewFontScale);
        EXPECT_FALSE(View().hasError);
    }

    TEST_F(PreferencesPanelModelTest, ACommitInTheFrameOfTheLastChangeSavesThatChange) {
        View().fontScale = kNewFontScale;

        EXPECT_TRUE(Command(EditorCommandIds::kPreferencesCommit.View()));

        EXPECT_EQ(StoredPreferences(_directory).fontScale, kNewFontScale);
        EXPECT_EQ(_host->preferencesNotified, 1);
    }

    TEST_F(PreferencesPanelModelTest, TheGridCheckIsSavedAtOnce) {
        View().viewportGrid = true;
        _model->Update();

        EXPECT_TRUE(StoredPreferences(_directory).viewportGrid);
        EXPECT_EQ(_host->preferencesNotified, 1);
    }

    TEST_F(PreferencesPanelModelTest, ANumberOutsideTheRangeIsClampedAndShownClamped) {
        View().fontScale = kFontScaleOutOfRange;
        View().viewportGridStep = 0;
        _model->Update();

        EXPECT_EQ(_host->settings->GetPreferences().fontScale, kMaxFontScale);
        EXPECT_EQ(_host->settings->GetPreferences().viewportGridStep, kMinViewportGridStep);
        EXPECT_EQ(View().fontScale, kMaxFontScale);
        EXPECT_EQ(View().viewportGridStep, kMinViewportGridStep);
    }

    TEST_F(PreferencesPanelModelTest, ASaveThatFailsSetsTheErrorAndTheNextOneThatWorksClearsIt) {
        // A folder where the file should go makes the rename fail
        MakeDirectory(_directory / "preferences.json");
        View().fontScale = kNewFontScale;
        _model->Update();
        Command(EditorCommandIds::kPreferencesCommit.View());

        EXPECT_TRUE(View().hasError);
        EXPECT_FALSE(View().error.empty());
        EXPECT_EQ(View().message, Format("Not saved: {}", View().error));
        EXPECT_EQ(_host->settings->GetPreferences().fontScale, kNewFontScale);

        RemoveDirectory(_directory / "preferences.json");
        Command(EditorCommandIds::kPreferencesCommit.View());
        EXPECT_FALSE(View().hasError);
        EXPECT_TRUE(View().message.empty());
    }

    TEST_F(PreferencesPanelModelTest, WithNoHostNothingRunsAndNoFolderIsSet) {
        _handle.Reset();
        View().fontScale = kNewFontScale;
        _model->Update();

        EXPECT_TRUE(View().noFolder);
        EXPECT_EQ(_host->settings->GetPreferences().fontScale, 1.0f);
        EXPECT_EQ(_host->preferencesNotified, 0);
        EXPECT_TRUE(Command(EditorCommandIds::kPreferencesCommit.View()));
        EXPECT_FALSE(PreferencesFileExists());
    }

    TEST_F(PreferencesPanelModelTest, AHostWithoutSettingsIsNoHostEither) {
        _host->settings.reset();
        _model->Update();
        EXPECT_TRUE(View().noFolder);
    }

    TEST_F(PreferencesPanelModelTest, TheExternalEditorOfTheLimitSurvivesAndALongerOneIsCut) {
        const String atLimit(kMaxExternalEditorLength, 'x');
        View().externalEditor = atLimit;
        _model->Update();
        Command(EditorCommandIds::kPreferencesCommit.View());

        EXPECT_EQ(View().externalEditor, atLimit);
        EXPECT_EQ(StoredPreferences(_directory).externalEditor, atLimit);

        View().externalEditor = String(kMaxExternalEditorLength + 100, 'y');
        _model->Update();
        EXPECT_EQ(View().externalEditor.size(), kMaxExternalEditorLength);
    }

    TEST_F(PreferencesPanelModelTest, AnUnknownCommandIsNotTaken) {
        EXPECT_FALSE(Command("preferences.nothing"));
    }

    TEST_F(PreferencesPanelModelTest, ASteadyUpdateAllocatesNothing) {
        View().externalEditor = "code -g {file}:{line}";
        _model->Update();
        for (int i = 0; i < kSteadyFrames; ++i) {
            _model->Update();
        }
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                _model->Update();
            }
        });
    }

}  // namespace BECore::Tests
