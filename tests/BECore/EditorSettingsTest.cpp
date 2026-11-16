#include <BECore/Control/Json.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/EditorLayout/EditorSettings.h>
#include <Widgets/EditorLayout/UserFileIo.h>

namespace BECore::Tests {

    namespace {

        using namespace EditorCommandIds;

        constexpr KeyChord kCtrlK{ImGuiKey_K, kChordModCtrl};
        constexpr KeyChord kCtrlP{ImGuiKey_P, kChordModCtrl};
        constexpr KeyChord kCtrlS{ImGuiKey_S, kChordModCtrl};

        // A folder of its own per test and per process, made empty and removed at the end
        class EditorSettingsTest : public ::testing::Test {
        protected:
            void SetUp() override {
                _directory = TestTempDirectory("EditorSettings");
                std::error_code ec;
                std::filesystem::remove_all(_directory, ec);
                std::filesystem::create_directories(_directory, ec);
            }

            void TearDown() override {
                std::error_code ec;
                std::filesystem::remove_all(_directory, ec);
            }

            void WriteFile(const char* name, const String& text) const {
                std::ofstream(_directory / name, std::ios::binary) << text;
            }

            [[nodiscard]] String ReadFile(const char* name) const {
                std::ifstream file(_directory / name, std::ios::binary);
                std::ostringstream content;
                content << file.rdbuf();
                return FromStdString(content.str());
            }

            [[nodiscard]] bool Exists(const char* name) const {
                return std::filesystem::exists(_directory / name);
            }

            [[nodiscard]] Json ReadJsonFile(const char* name) const {
                const String text = ReadFile(name);
                auto parsed = ParseJson(text);
                EXPECT_TRUE(parsed.has_value()) << name;
                return parsed ? *parsed : Json::object();
            }

            std::filesystem::path _directory;
        };

    }  // namespace

    TEST_F(EditorSettingsTest, MissingFilesGiveTheDefaultsAndWriteNothing) {
        EditorSettings settings(_directory);
        settings.Load();
        EXPECT_EQ(settings.GetPreferences(), EditorPreferences{});
        for (const EditorCommand& command : GetEditorCommands()) {
            EXPECT_EQ(settings.GetHotkeys().GetChord(command.id), command.defaultChord);
        }
        EXPECT_FALSE(Exists("preferences.json"));
        EXPECT_FALSE(Exists("hotkeys.json"));
    }

    TEST_F(EditorSettingsTest, PreferencesRoundTrip) {
        EditorPreferences changed;
        changed.fontScale = 1.25f;
        changed.viewportGrid = true;
        changed.viewportGridStep = 48;

        EditorSettings writer(_directory);
        writer.SetPreferences(changed);
        ASSERT_TRUE(writer.SavePreferences());
        EXPECT_TRUE(writer.GetLastSaveError().empty());

        EditorSettings reader(_directory);
        reader.Load();
        EXPECT_EQ(reader.GetPreferences(), changed);
        EXPECT_EQ(ReadJsonFile("preferences.json")["version"], 1);
    }

    TEST_F(EditorSettingsTest, HotkeysRoundTrip) {
        EditorSettings writer(_directory);
        ASSERT_EQ(writer.GetHotkeys().Assign(kSceneSave, kCtrlK).status, AssignStatus::Assigned);
        ASSERT_TRUE(writer.GetHotkeys().Unbind(kFileQuit));
        ASSERT_EQ(writer.GetHotkeys().AssignSwapping(kTextureSave, kCtrlP).status, AssignStatus::Assigned);
        ASSERT_TRUE(writer.SaveHotkeys());

        EditorSettings reader(_directory);
        reader.Load();
        EXPECT_EQ(reader.GetHotkeys().GetChord(kSceneSave), kCtrlK);
        EXPECT_EQ(reader.GetHotkeys().GetChord(kTextureSave), kCtrlP);
        EXPECT_FALSE(reader.GetHotkeys().GetChord(kViewCommandPalette).has_value());
        EXPECT_EQ(reader.GetHotkeys().GetChord(kConfigSave), writer.GetHotkeys().GetChord(kConfigSave));
    }

    TEST_F(EditorSettingsTest, OnlyNonDefaultHotkeysAreWritten) {
        EditorSettings settings(_directory);
        ASSERT_TRUE(settings.GetHotkeys().Unbind(kFileQuit));
        ASSERT_EQ(settings.GetHotkeys().Assign(kSceneSave, kCtrlK).status, AssignStatus::Assigned);
        ASSERT_TRUE(settings.SaveHotkeys());

        const Json file = ReadJsonFile("hotkeys.json");
        ASSERT_TRUE(file["bindings"].is_object());
        EXPECT_EQ(file["bindings"].size(), 1u);  // file.quit had no default chord, so unbinding it changes nothing
        EXPECT_EQ(file["bindings"]["scene.save"], "Ctrl+K");

        ASSERT_TRUE(settings.GetHotkeys().Unbind(kViewPreferences));
        ASSERT_TRUE(settings.SaveHotkeys());
        const Json again = ReadJsonFile("hotkeys.json");
        EXPECT_EQ(again["bindings"].size(), 2u);
        EXPECT_EQ(again["bindings"]["view.preferences"], "");

        settings.GetHotkeys().ResetAll();
        ASSERT_TRUE(settings.SaveHotkeys());
        EXPECT_TRUE(ReadJsonFile("hotkeys.json")["bindings"].empty());
    }

    TEST_F(EditorSettingsTest, AnEntryEqualToTheDefaultIsNotWrittenBack) {
        WriteFile("hotkeys.json", R"({"version":1,"bindings":{"scene.save":"Ctrl+S"}})");
        EditorSettings settings(_directory);
        settings.Load();
        EXPECT_EQ(settings.GetHotkeys().GetChord(kSceneSave), kCtrlS);
        ASSERT_TRUE(settings.SaveHotkeys());
        EXPECT_TRUE(ReadJsonFile("hotkeys.json")["bindings"].empty());
    }

    TEST_F(EditorSettingsTest, ValuesAreClamped) {
        WriteFile("preferences.json", R"({"version":1,"fontScale":5.0,"viewportGridStep":1})");
        EditorSettings settings(_directory);
        settings.Load();
        EXPECT_FLOAT_EQ(settings.GetPreferences().fontScale, kMaxFontScale);
        EXPECT_EQ(settings.GetPreferences().viewportGridStep, kMinViewportGridStep);

        WriteFile("preferences.json", R"({"fontScale":0.1,"viewportGridStep":100000})");
        settings.Load();
        EXPECT_FLOAT_EQ(settings.GetPreferences().fontScale, kMinFontScale);
        EXPECT_EQ(settings.GetPreferences().viewportGridStep, kMaxViewportGridStep);

        EditorPreferences wild;
        wild.fontScale = 9.0f;
        wild.viewportGridStep = -3;
        settings.SetPreferences(wild);
        EXPECT_FLOAT_EQ(settings.GetPreferences().fontScale, kMaxFontScale);
        EXPECT_EQ(settings.GetPreferences().viewportGridStep, kMinViewportGridStep);
    }

    TEST_F(EditorSettingsTest, ACorruptFileGivesTheDefaultsAndIsNotRewrittenByLoad) {
        const String garbage = "{ this is not json";
        WriteFile("preferences.json", garbage);
        WriteFile("hotkeys.json", "[1, 2, 3]");
        EditorSettings settings(_directory);
        settings.Load();
        EXPECT_EQ(settings.GetPreferences(), EditorPreferences{});
        EXPECT_EQ(settings.GetHotkeys().GetChord(kSceneSave), kCtrlS);
        EXPECT_EQ(ReadFile("preferences.json"), garbage);
        EXPECT_EQ(ReadFile("hotkeys.json"), "[1, 2, 3]");
        EXPECT_FALSE(Exists("preferences.json.bad"));
    }

    TEST_F(EditorSettingsTest, TheFirstSaveAfterACorruptFileKeepsTheOldTextAside) {
        const String garbage = "{ this is not json";
        WriteFile("preferences.json", garbage);
        EditorSettings settings(_directory);
        settings.Load();
        ASSERT_TRUE(settings.SavePreferences());
        EXPECT_EQ(ReadFile("preferences.json.bad"), garbage);
        EXPECT_FALSE(ReadJsonFile("preferences.json").contains("theme"));

        WriteFile("preferences.json.bad", "kept");
        ASSERT_TRUE(settings.SavePreferences());
        EXPECT_EQ(ReadFile("preferences.json.bad"), "kept");
    }

    TEST_F(EditorSettingsTest, AValueOfTheWrongTypeGivesTheDefaultOfThatFieldOnly) {
        WriteFile("preferences.json", R"({"fontScale":"big","viewportGrid":true,"viewportGridStep":1.5})");
        EditorSettings settings(_directory);
        settings.Load();
        EditorPreferences expected;
        expected.viewportGrid = true;
        EXPECT_EQ(settings.GetPreferences(), expected);

        WriteFile("preferences.json", R"({"fontScale":1.5,"viewportGrid":"yes"})");
        settings.Load();
        expected = EditorPreferences{};
        expected.fontScale = 1.5f;
        EXPECT_EQ(settings.GetPreferences(), expected);
    }

    TEST_F(EditorSettingsTest, UnknownKeysSurviveASave) {
        WriteFile("preferences.json", R"({"version":1,"theme":"Classic","futureOption":{"a":[1,2]},"zoom":7})");
        WriteFile("hotkeys.json", R"({"version":1,"extra":"x","bindings":{"future.command":"Ctrl+J","scene.save":"Ctrl+K"}})");
        EditorSettings settings(_directory);
        settings.Load();
        EXPECT_EQ(settings.GetPreferences(), EditorPreferences{});
        EXPECT_TRUE(settings.GetHotkeys().Find(KeyChord{ImGuiKey_J, kChordModCtrl}).Empty());

        settings.SetPreferences(EditorPreferences{});
        ASSERT_TRUE(settings.SavePreferences());
        ASSERT_TRUE(settings.SaveHotkeys());

        const Json preferences = ReadJsonFile("preferences.json");
        EXPECT_EQ(preferences["futureOption"]["a"][1], 2);
        EXPECT_EQ(preferences["zoom"], 7);
        EXPECT_EQ(preferences["theme"], "Classic");
        const Json hotkeys = ReadJsonFile("hotkeys.json");
        EXPECT_EQ(hotkeys["extra"], "x");
        EXPECT_EQ(hotkeys["bindings"]["future.command"], "Ctrl+J");
        EXPECT_EQ(hotkeys["bindings"]["scene.save"], "Ctrl+K");
    }

    // An old file with a `theme` key loads: the key is kept and written back as it was, never applied (one theme, ImGuiTheme.h)
    TEST_F(EditorSettingsTest, AnOldThemeKeyIsKeptAndNeverApplied) {
        WriteFile("preferences.json", R"({"version":1,"theme":"Classic","fontScale":1.5})");
        EditorSettings settings(_directory);
        settings.Load();
        EditorPreferences expected;
        expected.fontScale = 1.5f;
        EXPECT_EQ(settings.GetPreferences(), expected);

        ASSERT_TRUE(settings.SavePreferences());
        const Json file = ReadJsonFile("preferences.json");
        EXPECT_EQ(file["theme"], "Classic");
        EXPECT_EQ(file["fontScale"], 1.5);
    }

    // ---- The viewport's preview choice ("viewportPreview" in preferences.json)

    TEST_F(EditorSettingsTest, ThePreviewChoiceIsTheDefaultWhenTheKeyIsMissing) {
        WriteFile("preferences.json", R"({"version":1,"theme":"Light"})");
        EditorSettings settings(_directory);
        settings.Load();
        EXPECT_EQ(settings.GetPreferences().viewportPreview, PreviewSettings{});
    }

    TEST_F(EditorSettingsTest, ThePreviewChoiceRoundTripsWithTheDeviceStoredByName) {
        const auto devices = UIPreview::GetDevices();
        ASSERT_GE(devices.size(), 2u);
        EditorPreferences changed;
        changed.viewportPreview.shape.aspect = ViewportAspect::Wide16x9;
        changed.viewportPreview.shape.device = 1;
        changed.viewportPreview.shape.custom = true;
        changed.viewportPreview.shape.customWidth = 700;
        changed.viewportPreview.shape.customHeight = 1234;
        changed.viewportPreview.shape.rotated = true;
        changed.viewportPreview.safeArea = SafeAreaSimulation::SideInsets;
        changed.viewportPreview.overlays = {false, true, true, true, true};

        EditorSettings writer(_directory);
        writer.SetPreferences(changed);
        ASSERT_TRUE(writer.SavePreferences());

        const Json file = ReadJsonFile("preferences.json");
        EXPECT_EQ(file["viewportPreview"]["device"], ToStdString(StringView(devices[1].name.data(), devices[1].name.size())));
        EXPECT_EQ(file["viewportPreview"]["aspect"], "Wide16x9");
        EXPECT_EQ(file["viewportPreview"]["safeArea"], "SideInsets");

        EditorSettings reader(_directory);
        reader.Load();
        EXPECT_EQ(reader.GetPreferences(), changed);
    }

    TEST_F(EditorSettingsTest, ADeviceThisBuildLacksGivesTheFreeShape) {
        WriteFile("preferences.json", R"({"viewportPreview":{"aspect":"Standard4x3","device":"No Such Phone 9","safeArea":"Device","rotated":true,"overlayTouchTargets":true}})");
        EditorSettings settings(_directory);
        settings.Load();
        const PreviewSettings& preview = settings.GetPreferences().viewportPreview;
        EXPECT_EQ(preview.shape.device, -1);
        EXPECT_FALSE(preview.shape.custom);
        EXPECT_EQ(preview.shape.aspect, ViewportAspect::Free);
        EXPECT_EQ(preview.safeArea, SafeAreaSimulation::None);
        EXPECT_TRUE(preview.shape.rotated);
        EXPECT_TRUE(preview.overlays.touchTargets);
        // An old file has no regions key
        EXPECT_FALSE(preview.overlays.regions);
    }

    TEST_F(EditorSettingsTest, PreviewMembersOfTheWrongTypeGiveTheirDefaultAndSizesAreClamped) {
        const String text = R"({"viewportPreview":{"aspect":3,"custom":"yes","customWidth":10,"customHeight":99999,"rotated":true,"safeArea":"Nope","overlayViewBounds":1,"overlayRegions":1}})";
        WriteFile("preferences.json", text);
        EditorSettings settings(_directory);
        settings.Load();
        const PreviewSettings& preview = settings.GetPreferences().viewportPreview;
        EXPECT_EQ(preview.shape.aspect, ViewportAspect::Free);
        EXPECT_FALSE(preview.shape.custom);
        EXPECT_EQ(preview.shape.customWidth, kMinPreviewSide);
        EXPECT_EQ(preview.shape.customHeight, kMaxPreviewSide);
        EXPECT_TRUE(preview.shape.rotated);
        EXPECT_EQ(preview.safeArea, SafeAreaSimulation::None);
        EXPECT_FALSE(preview.overlays.viewBounds);
        EXPECT_FALSE(preview.overlays.regions);
        EXPECT_EQ(ReadFile("preferences.json"), text);

        WriteFile("preferences.json", R"({"viewportPreview":[1,2]})");
        settings.Load();
        EXPECT_EQ(settings.GetPreferences().viewportPreview, PreviewSettings{});

        EditorPreferences wild;
        wild.viewportPreview.shape.customWidth = 1;
        wild.viewportPreview.shape.customHeight = 100000;
        wild.viewportPreview.shape.device = 9999;
        settings.SetPreferences(wild);
        EXPECT_EQ(settings.GetPreferences().viewportPreview.shape.customWidth, kMinPreviewSide);
        EXPECT_EQ(settings.GetPreferences().viewportPreview.shape.customHeight, kMaxPreviewSide);
        EXPECT_EQ(settings.GetPreferences().viewportPreview.shape.device, -1);
    }

    TEST_F(EditorSettingsTest, UnknownMembersOfThePreviewObjectSurviveASave) {
        WriteFile("preferences.json", R"({"viewportPreview":{"future":{"a":1},"overlayViewBounds":true}})");
        EditorSettings settings(_directory);
        settings.Load();
        EXPECT_TRUE(settings.GetPreferences().viewportPreview.overlays.viewBounds);
        ASSERT_TRUE(settings.SavePreferences());
        const Json file = ReadJsonFile("preferences.json");
        EXPECT_EQ(file["viewportPreview"]["future"]["a"], 1);
        EXPECT_EQ(file["viewportPreview"]["overlayViewBounds"], true);
    }

    TEST_F(EditorSettingsTest, AnOldFileGainsOnlyTheKeysOfLaterBuilds) {
        WriteFile("preferences.json", R"({"version":1,"theme":"Classic","fontScale":1.5,"viewportGrid":true,"viewportGridStep":16,"futureOption":7})");
        EditorSettings settings(_directory);
        settings.Load();
        EditorPreferences expected;
        expected.fontScale = 1.5f;
        expected.viewportGrid = true;
        expected.viewportGridStep = 16;
        EXPECT_EQ(settings.GetPreferences(), expected);

        ASSERT_TRUE(settings.SavePreferences());
        Json file = ReadJsonFile("preferences.json");
        ASSERT_TRUE(file.contains("viewportPreview"));
        file.erase("viewportPreview");
        EXPECT_EQ(file["viewportInputMode"], "play");
        file.erase("viewportInputMode");
        EXPECT_EQ(file["externalEditor"], "");
        file.erase("externalEditor");
        EXPECT_TRUE(file["consoleHistory"].is_array());
        file.erase("consoleHistory");
        const Json oldFile = *ParseJson(R"({"version":1,"theme":"Classic","fontScale":1.5,"viewportGrid":true,"viewportGridStep":16,"futureOption":7})");
        EXPECT_EQ(file, oldFile);
    }

    TEST_F(EditorSettingsTest, TheViewportInputModeIsPlayWhenTheKeyIsMissing) {
        WriteFile("preferences.json", R"({"version":1,"fontScale":1.5})");
        EditorSettings settings(_directory);
        settings.Load();
        EXPECT_EQ(settings.GetPreferences().viewportInputMode, ViewportInputMode::Play);
    }

    TEST_F(EditorSettingsTest, TheViewportInputModeRoundTripsAsAWord) {
        EditorPreferences changed;
        changed.viewportInputMode = ViewportInputMode::Edit;
        EditorSettings writer(_directory);
        writer.SetPreferences(changed);
        ASSERT_TRUE(writer.SavePreferences());
        EXPECT_EQ(ReadJsonFile("preferences.json")["viewportInputMode"], "edit");

        EditorSettings reader(_directory);
        reader.Load();
        EXPECT_EQ(reader.GetPreferences().viewportInputMode, ViewportInputMode::Edit);
        EXPECT_EQ(reader.GetPreferences(), changed);
        EXPECT_NE(reader.GetPreferences(), EditorPreferences{});
    }

    TEST_F(EditorSettingsTest, AnUnknownViewportInputModeGivesPlayAndTheOtherFieldsStay) {
        for (const char* text : {R"("fly")", R"("")", "3", "true", R"("Edit")"}) {
            WriteFile("preferences.json", String(R"({"version":1,"fontScale":1.5,"viewportInputMode":)") + text + "}");
            EditorSettings settings(_directory);
            settings.Load();
            EXPECT_EQ(settings.GetPreferences().viewportInputMode, ViewportInputMode::Play) << text;
            EXPECT_EQ(settings.GetPreferences().fontScale, 1.5f) << text;
        }
    }

    // ---- The Console: externalEditor and consoleHistory

    TEST_F(EditorSettingsTest, TheConsoleKeysAreEmptyWhenTheFileLacksThem) {
        WriteFile("preferences.json", R"({"version":1,"fontScale":1.5})");
        EditorSettings settings(_directory);
        settings.Load();
        EXPECT_TRUE(settings.GetPreferences().externalEditor.empty());
        EXPECT_TRUE(settings.GetPreferences().consoleHistory.empty());
    }

    TEST_F(EditorSettingsTest, TheConsoleKeysRoundTrip) {
        EditorPreferences changed;
        changed.externalEditor = "cmd /c code -g {file}:{line}";
        changed.consoleHistory = {"ping", "step frames=3", "echo {\"a\":1}"};
        EditorSettings writer(_directory);
        writer.SetPreferences(changed);
        ASSERT_TRUE(writer.SavePreferences());

        const Json file = ReadJsonFile("preferences.json");
        EXPECT_EQ(file["externalEditor"], "cmd /c code -g {file}:{line}");
        ASSERT_TRUE(file["consoleHistory"].is_array());
        EXPECT_EQ(file["consoleHistory"].size(), 3u);

        EditorSettings reader(_directory);
        reader.Load();
        EXPECT_EQ(reader.GetPreferences(), changed);
    }

    TEST_F(EditorSettingsTest, AConsoleValueOfTheWrongTypeGivesItsDefaultAndKeepsTheFile) {
        struct Case {
            const char* text;
            const char* editor;
            size_t historySize;
        };
        const Case cases[] = {
            {R"({"externalEditor":5,"consoleHistory":["ping"]})", "", 1},
            {R"({"externalEditor":"code","consoleHistory":"ping"})", "code", 0},
            {R"({"externalEditor":"code","consoleHistory":["ping",3]})", "code", 0},
            {R"({"externalEditor":["code"],"consoleHistory":{"a":1}})", "", 0},
        };
        for (const Case& item : cases) {
            WriteFile("preferences.json", item.text);
            EditorSettings settings(_directory);
            settings.Load();
            EXPECT_EQ(settings.GetPreferences().externalEditor, item.editor) << item.text;
            EXPECT_EQ(settings.GetPreferences().consoleHistory.size(), item.historySize) << item.text;
            EXPECT_EQ(ReadFile("preferences.json"), item.text);
            EXPECT_FALSE(Exists("preferences.json.bad"));
        }
    }

    TEST_F(EditorSettingsTest, UnknownKeysBesideTheConsoleKeysSurviveASave) {
        WriteFile("preferences.json", R"({"version":1,"externalEditor":"code","consoleHistory":["ping"],"futureOption":{"a":1}})");
        EditorSettings settings(_directory);
        settings.Load();
        ASSERT_TRUE(settings.SavePreferences());

        const Json file = ReadJsonFile("preferences.json");
        EXPECT_EQ(file["futureOption"]["a"], 1);
        EXPECT_EQ(file["externalEditor"], "code");
        EXPECT_EQ(file["consoleHistory"].size(), 1u);
        EXPECT_EQ(file.size(), 9u);  // version, the seven known keys, futureOption: no key is written twice
    }

    TEST_F(EditorSettingsTest, AnExternalEditorOverTheLimitIsCutAtACharacter) {
        const String longText(kMaxExternalEditorLength + 40, 'x');
        WriteFile("preferences.json", String(R"({"externalEditor":")") + longText + R"("})");
        EditorSettings settings(_directory);
        settings.Load();
        EXPECT_EQ(settings.GetPreferences().externalEditor.size(), kMaxExternalEditorLength);

        // A two-byte character that straddles the limit goes whole
        EditorPreferences wild;
        wild.externalEditor = String(kMaxExternalEditorLength - 1, 'x') + "\xC3\xA9" + "yyy";
        settings.SetPreferences(wild);
        EXPECT_EQ(settings.GetPreferences().externalEditor, String(kMaxExternalEditorLength - 1, 'x'));
    }

    TEST_F(EditorSettingsTest, TheHistoryDropsEmptyAndTooLongEntriesAndKeepsTheNewest) {
        EditorPreferences wild;
        wild.consoleHistory.push_back("");
        wild.consoleHistory.push_back(String(kMaxConsoleCommandLength + 1, 'a'));
        wild.consoleHistory.push_back(String(kMaxConsoleCommandLength, 'b'));
        EditorSettings settings(_directory);
        settings.SetPreferences(wild);
        ASSERT_EQ(settings.GetPreferences().consoleHistory.size(), 1u);
        EXPECT_EQ(settings.GetPreferences().consoleHistory[0].size(), kMaxConsoleCommandLength);

        wild.consoleHistory.clear();
        for (size_t i = 0; i < 100; ++i) {
            wild.consoleHistory.push_back(Format("cmd{}", i));
        }
        settings.SetPreferences(wild);
        const Vector<String>& kept = settings.GetPreferences().consoleHistory;
        ASSERT_EQ(kept.size(), kMaxConsoleHistory);
        EXPECT_EQ(kept.front(), "cmd36");
        EXPECT_EQ(kept.back(), "cmd99");
    }

    TEST_F(EditorSettingsTest, AddConsoleHistoryMovesAnEqualCommandToTheEndAndBoundsTheList) {
        EditorPreferences preferences;
        AddConsoleHistory(preferences, "a");
        AddConsoleHistory(preferences, "b");
        AddConsoleHistory(preferences, "a");
        ASSERT_EQ(preferences.consoleHistory.size(), 2u);
        EXPECT_EQ(preferences.consoleHistory[0], "b");
        EXPECT_EQ(preferences.consoleHistory[1], "a");

        AddConsoleHistory(preferences, "");
        AddConsoleHistory(preferences, String(kMaxConsoleCommandLength + 1, 'z'));
        EXPECT_EQ(preferences.consoleHistory.size(), 2u);

        for (size_t i = 0; i < 2 * kMaxConsoleHistory; ++i) {
            AddConsoleHistory(preferences, Format("c{}", i));
        }
        ASSERT_EQ(preferences.consoleHistory.size(), kMaxConsoleHistory);
        EXPECT_EQ(preferences.consoleHistory.back(), Format("c{}", 2 * kMaxConsoleHistory - 1));
        EXPECT_EQ(preferences.consoleHistory.front(), Format("c{}", kMaxConsoleHistory));
    }

    TEST_F(EditorSettingsTest, BadHotkeyEntriesAreDropped) {
        WriteFile("hotkeys.json", R"({"bindings":{
            "scene.save":"Ctrl+P",
            "config.save":"Banana",
            "texture.save":"K",
            "view.preferences":7,
            "mode.toggle":"Ctrl+Shift+K",
            "file.quit":"Ctrl+Shift+E",
            "view.saveLayout":"Ctrl+Q"}})");
        EditorSettings settings(_directory);
        settings.Load();
        const HotkeyMap& map = settings.GetHotkeys();
        // Conflict with the palette: dropped, so the default stays
        EXPECT_EQ(map.GetChord(kSceneSave), kCtrlS);
        EXPECT_EQ(map.GetChord(kViewCommandPalette), kCtrlP);
        EXPECT_TRUE(map.IsDefault(kConfigSave));
        EXPECT_FALSE(map.GetChord(kTextureSave).has_value());
        EXPECT_TRUE(map.IsDefault(kViewPreferences));
        EXPECT_EQ(map.GetChord(kModeToggle), (KeyChord{ImGuiKey_E, kChordModCtrl | kChordModShift}));
        EXPECT_FALSE(map.GetChord(kFileQuit).has_value());
        EXPECT_EQ(map.GetChord(kViewSaveLayout), (KeyChord{ImGuiKey_Q, kChordModCtrl}));
    }

    TEST_F(EditorSettingsTest, TwoEntriesForOneChordKeepOnlyOne) {
        WriteFile("hotkeys.json", R"({"bindings":{"scene.save":"Ctrl+K","texture.save":"Ctrl+K"}})");
        EditorSettings settings(_directory);
        settings.Load();
        // The entries are read in the order of their ids; the later one finds the chord taken and is dropped
        EXPECT_EQ(settings.GetHotkeys().GetChord(kSceneSave), kCtrlK);
        EXPECT_FALSE(settings.GetHotkeys().GetChord(kTextureSave).has_value());
    }

    TEST_F(EditorSettingsTest, TwoCommandsCanSwapTheirChordsInTheFile) {
        WriteFile("hotkeys.json", R"({"bindings":{"scene.save":"Ctrl+P","view.commandPalette":"Ctrl+S"}})");
        EditorSettings settings(_directory);
        settings.Load();
        EXPECT_EQ(settings.GetHotkeys().GetChord(kSceneSave), kCtrlP);
        EXPECT_EQ(settings.GetHotkeys().GetChord(kViewCommandPalette), kCtrlS);
    }

    TEST_F(EditorSettingsTest, AFailedSaveKeepsTheOldFileAndLeavesNoTempFile) {
        // A folder where the file should go makes the rename fail
        std::filesystem::create_directories(_directory / "preferences.json");
        EditorSettings settings(_directory);
        EXPECT_FALSE(settings.SavePreferences());
        EXPECT_FALSE(settings.GetLastSaveError().empty());
        EXPECT_TRUE(std::filesystem::is_directory(_directory / "preferences.json"));
        EXPECT_FALSE(Exists("preferences.json.tmp"));
    }

    TEST_F(EditorSettingsTest, ASaveWithoutAFolderFails) {
        EditorSettings settings{std::filesystem::path()};
        EXPECT_FALSE(settings.SavePreferences());
        EXPECT_FALSE(settings.GetLastSaveError().empty());
    }

    TEST_F(EditorSettingsTest, AtomicWriteLeavesNoTempFileAndReplacesTheOldText) {
        EditorSettings settings(_directory);
        ASSERT_TRUE(settings.SavePreferences());
        ASSERT_TRUE(settings.SaveHotkeys());
        EXPECT_FALSE(Exists("preferences.json.tmp"));
        EXPECT_FALSE(Exists("hotkeys.json.tmp"));

        const std::filesystem::path path = _directory / "note.txt";
        ASSERT_TRUE(WriteTextFileAtomically(path, "first").has_value());
        ASSERT_TRUE(WriteTextFileAtomically(path, "second").has_value());
        const auto text = ReadTextFile(path);
        ASSERT_TRUE(text.has_value());
        EXPECT_EQ(*text, "second");
        EXPECT_FALSE(std::filesystem::exists(_directory / "note.txt.tmp"));
        EXPECT_FALSE(ReadTextFile(_directory / "missing.txt").has_value());
    }

    TEST_F(EditorSettingsTest, SaveMakesTheFolder) {
        EditorSettings settings(_directory / "a" / "b");
        ASSERT_TRUE(settings.SavePreferences());
        EXPECT_TRUE(std::filesystem::exists(_directory / "a" / "b" / "preferences.json"));
    }

    TEST_F(EditorSettingsTest, TheEnvironmentVariableMovesTheFolder) {
        const String value = FromStdString(_directory.string());
#if defined(_WIN32)
        ASSERT_EQ(_putenv_s(kEditorDirectoryEnvironmentVariable, value.c_str()), 0);
#else
        ASSERT_EQ(setenv(kEditorDirectoryEnvironmentVariable, value.c_str(), 1), 0);
#endif
        EXPECT_EQ(EditorUserDirectory(), _directory);
#if defined(_WIN32)
        _putenv_s(kEditorDirectoryEnvironmentVariable, "");
#else
        unsetenv(kEditorDirectoryEnvironmentVariable);
#endif
    }

}  // namespace BECore::Tests
