#include <BECore/EditorLayoutHostProbe.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/HotkeyMap.h>
#include <Widgets/EditorLayout/PreferencesPanelModel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kSteadyFrames = 8;
        constexpr float kFontScaleOutOfRange = 9.0f;
        constexpr int32_t kGridStep = 64;
        constexpr float kNewFontScale = 1.5f;
        constexpr KeyChord kCtrlK{ImGuiKey_K, kChordModCtrl};
        constexpr KeyChord kCtrlP{ImGuiKey_P, kChordModCtrl};
        constexpr KeyChord kCtrlS{ImGuiKey_S, kChordModCtrl};
        constexpr KeyChord kCtrlZ{ImGuiKey_Z, kChordModCtrl};
        constexpr KeyChord kBareK{ImGuiKey_K, 0};
        constexpr KeyChord kEscape{ImGuiKey_Escape, 0};

        /// What a fresh start of the editor reads from the folder
        EditorPreferences StoredPreferences(const Path& folder) {
            EditorSettings settings(folder);
            settings.Load();
            return settings.GetPreferences();
        }

        /// The hotkeys a fresh start of the editor reads from the folder
        Optional<KeyChord> StoredChord(const Path& folder, PoolString command) {
            EditorSettings settings(folder);
            settings.Load();
            return settings.GetHotkeys().GetChord(command);
        }

        const HotkeyRow* FindRow(const PreferencesPanelView& view, StringView id) {
            for (const HotkeyRow& row : view.hotkeyRows) {
                if (row.id == id) {
                    return &row;
                }
            }
            return nullptr;
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
            _model->SetChordReader([this] {
                const Optional<KeyChord> pressed = _pressed;
                _pressed.reset();
                return pressed;
            });
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

        bool Command(StringView id, StringView argument) {
            return _model->RunCommand(id, argument);
        }

        /// The chord is pressed in the next frame
        void Press(const KeyChord& chord) {
            _pressed = chord;
            _model->Update();
        }

        HotkeyMap& Hotkeys() {
            return _host->settings->GetHotkeys();
        }

        /// Opens the dialog of @p command and lets the frame that opened it pass
        void StartRebind(StringView command) {
            ASSERT_TRUE(Command(EditorCommandIds::kPreferencesRebind.View(), command));
            _model->Update();
        }

        bool PreferencesFileExists() const {
            return PathExists(_directory / "preferences.json");
        }

        Path _directory;
        Optional<ProbeEditorLayoutHost> _host;
        EditorLayoutHostHandle _handle;
        IntrusivePtr<PreferencesPanelModel> _model;
        Optional<KeyChord> _pressed;
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

    TEST_F(PreferencesPanelModelTest, TheRowsListTheCommandTableWithTheChordTextAndTheFlagsOfEachButton) {
        EXPECT_EQ(View().hotkeyRows.size(), GetEditorCommands().size());

        const HotkeyRow* bound = FindRow(View(), "scene.save");
        ASSERT_NE(bound, nullptr);
        EXPECT_EQ(bound->title, "Save Scene");
        EXPECT_EQ(bound->chordText, "Ctrl+S");
        EXPECT_EQ(bound->chordStyle, PanelTextStyle::Normal);
        EXPECT_TRUE(bound->canRebind);
        EXPECT_TRUE(bound->canClear);
        EXPECT_FALSE(bound->canReset);

        const HotkeyRow* unbound = FindRow(View(), "view.sceneEditor");
        ASSERT_NE(unbound, nullptr);
        EXPECT_EQ(unbound->chordText, "none");
        EXPECT_EQ(unbound->chordStyle, PanelTextStyle::Disabled);
        EXPECT_TRUE(unbound->canRebind);
        EXPECT_FALSE(unbound->canClear);

        const HotkeyRow* fixed = FindRow(View(), "mode.toggle");
        ASSERT_NE(fixed, nullptr);
        EXPECT_EQ(fixed->chordText, "Ctrl+Shift+E (fixed)");
        EXPECT_FALSE(fixed->canRebind);
        EXPECT_FALSE(fixed->canClear);
        EXPECT_FALSE(fixed->canReset);
    }

    TEST_F(PreferencesPanelModelTest, RebindOpensTheDialogAndACapturedChordIsAssignedAndSaved) {
        StartRebind("scene.save");
        EXPECT_TRUE(View().rebindOpen);
        EXPECT_FALSE(View().inConflict);
        EXPECT_EQ(View().rebindTitle, "Save Scene");
        EXPECT_EQ(View().rebindText, "Press the new shortcut for Save Scene. Esc cancels.");

        Press(kCtrlK);

        EXPECT_EQ(Hotkeys().GetChord(EditorCommandIds::kSceneSave), kCtrlK);
        EXPECT_EQ(StoredChord(_directory, EditorCommandIds::kSceneSave), kCtrlK);
        EXPECT_FALSE(View().rebindOpen);
        EXPECT_FALSE(View().hasHotkeysMessage);
        EXPECT_EQ(FindRow(View(), "scene.save")->chordText, "Ctrl+K");
        EXPECT_TRUE(FindRow(View(), "scene.save")->canReset);
    }

    TEST_F(PreferencesPanelModelTest, TheFrameThatOpenedTheDialogCapturesNothing) {
        ASSERT_TRUE(Command(EditorCommandIds::kPreferencesRebind.View(), "scene.save"));

        Press(kCtrlK);
        EXPECT_TRUE(View().rebindOpen);
        EXPECT_EQ(Hotkeys().GetChord(EditorCommandIds::kSceneSave), kCtrlS);

        Press(kCtrlK);
        EXPECT_FALSE(View().rebindOpen);
        EXPECT_EQ(Hotkeys().GetChord(EditorCommandIds::kSceneSave), kCtrlK);
    }

    TEST_F(PreferencesPanelModelTest, AChordAnotherCommandHoldsGoesToTheConflictStageAndSwapTakesItFromThere) {
        StartRebind("scene.save");
        Press(kCtrlP);

        EXPECT_TRUE(View().rebindOpen);
        EXPECT_TRUE(View().inConflict);
        EXPECT_EQ(View().conflictText, "Ctrl+P is already used by Command Palette. Swap takes it from there and gives it to Save Scene.");
        EXPECT_EQ(Hotkeys().GetChord(EditorCommandIds::kSceneSave), kCtrlS);

        EXPECT_TRUE(Command(EditorCommandIds::kPreferencesSwap.View(), {}));

        EXPECT_EQ(Hotkeys().GetChord(EditorCommandIds::kSceneSave), kCtrlP);
        EXPECT_FALSE(Hotkeys().GetChord(EditorCommandIds::kViewCommandPalette).has_value());
        EXPECT_EQ(StoredChord(_directory, EditorCommandIds::kSceneSave), kCtrlP);
        EXPECT_FALSE(View().rebindOpen);
        EXPECT_FALSE(View().inConflict);
        EXPECT_EQ(FindRow(View(), "view.commandPalette")->chordText, "none");
    }

    TEST_F(PreferencesPanelModelTest, ARefusedChordShowsItsReasonAndKeepsTheDialogOpen) {
        StartRebind("scene.save");

        Press(kBareK);
        EXPECT_TRUE(View().rebindOpen);
        EXPECT_TRUE(View().hasRefusal);
        EXPECT_EQ(View().refusal, "Use Ctrl or Alt with a key, or an F key");

        Press(kCtrlZ);
        EXPECT_EQ(View().refusal, "The editor keeps this shortcut for itself");
        EXPECT_EQ(Hotkeys().GetChord(EditorCommandIds::kSceneSave), kCtrlS);

        Press(kCtrlK);
        EXPECT_FALSE(View().rebindOpen);
        EXPECT_FALSE(View().hasRefusal);
    }

    TEST_F(PreferencesPanelModelTest, EscapeAndTheCancelCommandCloseTheDialogWithoutAChange) {
        StartRebind("scene.save");
        Press(kEscape);
        EXPECT_FALSE(View().rebindOpen);
        EXPECT_EQ(Hotkeys().GetChord(EditorCommandIds::kSceneSave), kCtrlS);

        StartRebind("scene.save");
        Press(kCtrlP);
        ASSERT_TRUE(View().inConflict);
        EXPECT_TRUE(Command(EditorCommandIds::kPreferencesCancelRebind.View(), {}));
        EXPECT_FALSE(View().rebindOpen);
        EXPECT_FALSE(View().inConflict);
        EXPECT_EQ(Hotkeys().GetChord(EditorCommandIds::kViewCommandPalette), kCtrlP);
    }

    TEST_F(PreferencesPanelModelTest, SwapWithoutAConflictAndKeysOutsideTheCapturingStageDoNothing) {
        EXPECT_TRUE(Command(EditorCommandIds::kPreferencesSwap.View(), {}));
        Press(kCtrlK);
        EXPECT_EQ(Hotkeys().GetChord(EditorCommandIds::kSceneSave), kCtrlS);
        EXPECT_FALSE(View().rebindOpen);
    }

    TEST_F(PreferencesPanelModelTest, ClearUnbindsAndResetBringsTheDefaultBackBothSaved) {
        EXPECT_TRUE(Command(EditorCommandIds::kPreferencesClear.View(), "scene.save"));
        EXPECT_FALSE(Hotkeys().GetChord(EditorCommandIds::kSceneSave).has_value());
        EXPECT_FALSE(StoredChord(_directory, EditorCommandIds::kSceneSave).has_value());
        EXPECT_EQ(FindRow(View(), "scene.save")->chordText, "none");
        EXPECT_FALSE(FindRow(View(), "scene.save")->canClear);
        EXPECT_TRUE(FindRow(View(), "scene.save")->canReset);

        EXPECT_TRUE(Command(EditorCommandIds::kPreferencesReset.View(), "scene.save"));
        EXPECT_EQ(Hotkeys().GetChord(EditorCommandIds::kSceneSave), kCtrlS);
        EXPECT_EQ(StoredChord(_directory, EditorCommandIds::kSceneSave), kCtrlS);
        EXPECT_FALSE(FindRow(View(), "scene.save")->canReset);
    }

    TEST_F(PreferencesPanelModelTest, ResetOfACommandWhoseDefaultAnotherHoldsSaysSoAndChangesNothing) {
        Hotkeys().Unbind(EditorCommandIds::kSceneSave);
        ASSERT_EQ(Hotkeys().Assign(EditorCommandIds::kTextureSave, kCtrlS).status, AssignStatus::Assigned);
        _model->OnOpenChanged(true);

        EXPECT_TRUE(Command(EditorCommandIds::kPreferencesReset.View(), "scene.save"));

        EXPECT_TRUE(View().hasHotkeysMessage);
        EXPECT_EQ(View().hotkeysMessage, "The default shortcut is used by Save Texture");
        EXPECT_FALSE(Hotkeys().GetChord(EditorCommandIds::kSceneSave).has_value());
        EXPECT_FALSE(PathExists(_directory / "hotkeys.json"));
    }

    TEST_F(PreferencesPanelModelTest, ResetAllPutsEveryDefaultBackAndSaves) {
        Hotkeys().Unbind(EditorCommandIds::kSceneSave);
        ASSERT_EQ(Hotkeys().Assign(EditorCommandIds::kConfigSave, kCtrlK).status, AssignStatus::Assigned);
        _model->OnOpenChanged(true);

        EXPECT_TRUE(Command(EditorCommandIds::kPreferencesResetAll.View(), {}));

        EXPECT_EQ(Hotkeys().GetChord(EditorCommandIds::kSceneSave), kCtrlS);
        EXPECT_EQ(StoredChord(_directory, EditorCommandIds::kSceneSave), kCtrlS);
        EXPECT_FALSE(FindRow(View(), "config.save")->canReset);
    }

    TEST_F(PreferencesPanelModelTest, AnUnknownOrFixedCommandIdIsIgnoredByEveryHotkeyCommand) {
        for (const StringView id : {"nothing.here", "mode.toggle", "preferences.commit"}) {
            EXPECT_TRUE(Command(EditorCommandIds::kPreferencesRebind.View(), id));
            EXPECT_FALSE(View().rebindOpen) << String(id).c_str();
            EXPECT_TRUE(Command(EditorCommandIds::kPreferencesClear.View(), id));
            EXPECT_TRUE(Command(EditorCommandIds::kPreferencesReset.View(), id));
        }
        EXPECT_TRUE(Command(EditorCommandIds::kPreferencesRebind.View(), {}));
        EXPECT_FALSE(View().rebindOpen);
        EXPECT_EQ(Hotkeys().GetChord(EditorCommandIds::kModeToggle), (KeyChord{ImGuiKey_E, kChordModCtrl | kChordModShift}));
        EXPECT_FALSE(PathExists(_directory / "hotkeys.json"));
    }

    TEST_F(PreferencesPanelModelTest, AFailedHotkeySaveLeavesTheNotSavedMessage) {
        // A folder where the file should go makes the rename fail
        MakeDirectory(_directory / "hotkeys.json");

        Command(EditorCommandIds::kPreferencesClear.View(), "scene.save");

        EXPECT_TRUE(View().hasHotkeysMessage);
        EXPECT_EQ(View().hotkeysMessage.substr(0, 11), "Not saved: ");
    }

    TEST_F(PreferencesPanelModelTest, ClosingThePanelClosesTheDialogAndOpeningItForgetsTheMessage) {
        StartRebind("scene.save");
        _model->OnOpenChanged(false);
        EXPECT_FALSE(View().rebindOpen);

        MakeDirectory(_directory / "hotkeys.json");
        Command(EditorCommandIds::kPreferencesClear.View(), "scene.save");
        ASSERT_TRUE(View().hasHotkeysMessage);
        _model->OnOpenChanged(true);
        EXPECT_FALSE(View().hasHotkeysMessage);
    }

    TEST_F(PreferencesPanelModelTest, HotkeyCommandsWithNoHostAreTakenAndDoNothing) {
        _handle.Reset();
        EXPECT_TRUE(Command(EditorCommandIds::kPreferencesRebind.View(), "scene.save"));
        EXPECT_FALSE(View().rebindOpen);
    }

    TEST_F(PreferencesPanelModelTest, ASteadyUpdateWhileCapturingAllocatesNothing) {
        StartRebind("scene.save");
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
