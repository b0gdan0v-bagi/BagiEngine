#include <BECore/Config/ConfigManager.h>
#include <BECore/EditorLayoutHostProbe.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/RefCounted/New.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr StringView kPanelConfigName = "PreferencesPanel";
        constexpr StringView kWindowLabel = "Preferences###Preferences";
        constexpr StringView kRebindButton = "##rebind";
        constexpr StringView kCancelButton = "Cancel";
        constexpr float kSliderEdgeInset = 3.0f;
        /// Not the default chord of any command, and a key a bare press may bind
        constexpr KeyChord kNewChord{ImGuiKey_F12, 0};

        Path ShippedAssetFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "PreferencesPanel.xml";
        }

        bool Contains(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        /// The row of the Hotkeys table of the first command whose shortcut can be changed
        size_t FirstRebindableRow() {
            size_t row = 0;
            for (const EditorCommand& command : GetEditorCommands()) {
                if (command.rebindable) {
                    return row;
                }
                ++row;
            }
            return row;
        }

        PoolString FirstRebindableCommand() {
            return GetEditorCommands()[FirstRebindableRow()].id;
        }

    }  // namespace

    /// The shipped `PreferencesPanel.xml` hosted by a real `AssetPanelWidget` over the real `PreferencesPanelModel` and a fake editor host, in an ImGui context with
    /// no backend and the keyboard navigation of the editor, driven by the labels of `ImGuiItemIndex` with the mouse and the keyboard of ImGui
    class PreferencesPanelAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("prefs_asset_config");
            _settingsDir = TestTempDirectory("prefs_asset_settings");
            MakeEmptyDirectory(_dir);
            MakeEmptyDirectory(_settingsDir);
            WriteFile(_dir / "PreferencesPanel.xml", ReadFile(ShippedAssetFile()));
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();
            _host.emplace(_settingsDir);
            _hostHandle = EditorLayoutHostRegistry::GetInstance().Register(*_host);

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = kDisplaySize;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            _fonts = LoadImGuiFonts(io, _buffers);
            SetActiveImGuiFonts(_fonts);
            ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
            _index.Arm(*ImGui::GetCurrentContext());

            _widget = New<AssetPanelWidget>();
            _widget->SetConfigManager(_manager);
            _widget->SetAsset(kPanelConfigName);
            const UniquePtr<IDeserializer> deserializer = _manager.CreateDeserializer(PoolString::Intern(kPanelConfigName));
            ASSERT_TRUE(deserializer);
            ASSERT_TRUE(_widget->Initialize(*deserializer));
            Settle();
        }

        void TearDown() override {
            _widget.Reset();
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
            _hostHandle.Reset();
            _host.reset();
            RemoveDirectory(_dir);
            RemoveDirectory(_settingsDir);
        }

        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            _widget->Update();
            ImGui::Render();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        /// Shows the window the way the menu item and Ctrl+, do
        void OpenWindow() {
            EditorCommandEvent::Emit(EditorCommandIds::kViewPreferences);
            Settle();
        }

        const ImGuiItemRecord* Find(StringView part) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && Contains(item.label, part)) {
                    return &item;
                }
            }
            return nullptr;
        }

        const ImGuiItemRecord* FindNth(StringView part, size_t n) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && Contains(item.label, part) && n-- == 0) {
                    return &item;
                }
            }
            return nullptr;
        }

        void ClickAt(ImVec2 point) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Frame();
            Settle();
        }

        void Click(const ImGuiItemRecord* item) {
            ASSERT_NE(item, nullptr);
            ClickAt(ImVec2(item->CenterX(), item->CenterY()));
        }

        void PressKey(ImGuiKey key) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(key, true);
            Frame();
            io.AddKeyEvent(key, false);
            Settle();
        }

        /// The second tab of the bar, found by ImGui's own tab list: a tab item is not in the item index
        void ClickHotkeysTab() {
            const ImGuiWindow* window = ImGui::FindWindowByName(String(kWindowLabel).c_str());
            ASSERT_NE(window, nullptr);
            const ImGuiTabBar* bar = nullptr;
            for (const ImGuiTabBar& candidate : ImGui::GetCurrentContext()->TabBars.Buf) {
                bar = &candidate;
            }
            ASSERT_NE(bar, nullptr);
            ASSERT_EQ(bar->Tabs.Size, 2);
            const ImGuiTabItem& tab = bar->Tabs[1];
            ClickAt(ImVec2(bar->BarRect.Min.x + tab.Offset + tab.Width * 0.5f, (bar->BarRect.Min.y + bar->BarRect.Max.y) * 0.5f));
        }

        /// The Rebind dialog is open while its Cancel button was drawn
        bool RebindDialogOpen() {
            return Find(kCancelButton) != nullptr;
        }

        EditorPreferences StoredPreferences() const {
            EditorSettings settings(_settingsDir);
            settings.Load();
            return settings.GetPreferences();
        }

        Optional<KeyChord> StoredChord(PoolString command) const {
            EditorSettings settings(_settingsDir);
            settings.Load();
            return settings.GetHotkeys().GetChord(command);
        }

        Path _dir;
        Path _settingsDir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        Optional<ProbeEditorLayoutHost> _host;
        EditorLayoutHostHandle _hostHandle;
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<AssetPanelWidget> _widget;
    };

    TEST_F(PreferencesPanelAssetTest, TheFileLoadsWithNoIssueAndTheWindowStartsHiddenAndOpensWithTheViewPreferencesCommand) {
        EXPECT_TRUE(_widget->GetHolder().Issues().empty());
        EXPECT_FALSE(_widget->IsOpen());
        EXPECT_EQ(Find("##fontScale"), nullptr);

        OpenWindow();
        EXPECT_TRUE(_widget->IsOpen());
        EXPECT_NE(Find("##fontScale"), nullptr);
        const ImGuiWindow* window = ImGui::FindWindowByName(String(kWindowLabel).c_str());
        ASSERT_NE(window, nullptr);
        EXPECT_NE(window->Flags & ImGuiWindowFlags_NoDocking, 0);

        // The same command closes it: the menu item and Ctrl+, toggle the window
        EditorCommandEvent::Emit(EditorCommandIds::kViewPreferences);
        Settle();
        EXPECT_FALSE(_widget->IsOpen());
    }

    TEST_F(PreferencesPanelAssetTest, ReleasingTheFontScaleSliderAppliesTheValueAndSavesItThroughTheCommand) {
        OpenWindow();
        const ImGuiItemRecord* slider = Find("##fontScale");
        ASSERT_NE(slider, nullptr);
        const float stored = StoredPreferences().fontScale;

        ClickAt(ImVec2(slider->maxX - kSliderEdgeInset, slider->CenterY()));

        EXPECT_FLOAT_EQ(_host->settings->GetPreferences().fontScale, kMaxFontScale);
        EXPECT_GT(_host->preferencesNotified, 0);
        EXPECT_NE(StoredPreferences().fontScale, stored);
        EXPECT_FLOAT_EQ(StoredPreferences().fontScale, kMaxFontScale);
    }

    TEST_F(PreferencesPanelAssetTest, TheGridStepSliderIsThereOnlyWhileTheGridIsOn) {
        OpenWindow();
        ASSERT_EQ(Find("##viewportGridStep"), nullptr);

        ASSERT_NO_FATAL_FAILURE(Click(Find("Show a grid over the image")));
        EXPECT_NE(Find("##viewportGridStep"), nullptr);
        EXPECT_TRUE(StoredPreferences().viewportGrid) << "a checkbox is saved at once";
    }

    TEST_F(PreferencesPanelAssetTest, TheTabsSwitchBetweenTheGeneralFieldsAndTheHotkeyTable) {
        OpenWindow();
        ASSERT_NE(Find("##fontScale"), nullptr);
        EXPECT_EQ(Find("Reset all"), nullptr);

        ASSERT_NO_FATAL_FAILURE(ClickHotkeysTab());
        EXPECT_EQ(Find("##fontScale"), nullptr);
        EXPECT_NE(Find("Reset all"), nullptr);
        EXPECT_NE(Find(kRebindButton), nullptr);
    }

    TEST_F(PreferencesPanelAssetTest, ARebindClickOpensTheDialogAndAPressedChordIsBoundAndSaved) {
        OpenWindow();
        ASSERT_NO_FATAL_FAILURE(ClickHotkeysTab());
        const PoolString command = FirstRebindableCommand();
        ASSERT_NO_FATAL_FAILURE(Click(FindNth(kRebindButton, FirstRebindableRow())));
        ASSERT_TRUE(RebindDialogOpen());

        PressKey(kNewChord.key);

        EXPECT_FALSE(RebindDialogOpen());
        ASSERT_TRUE(StoredChord(command).has_value());
        EXPECT_EQ(*StoredChord(command), kNewChord);
    }

    TEST_F(PreferencesPanelAssetTest, EscClosesTheRebindDialogAndTheNextRebindOpensItAgain) {
        OpenWindow();
        ASSERT_NO_FATAL_FAILURE(ClickHotkeysTab());
        ASSERT_NO_FATAL_FAILURE(Click(FindNth(kRebindButton, FirstRebindableRow())));
        ASSERT_TRUE(RebindDialogOpen());

        PressKey(ImGuiKey_Escape);
        EXPECT_FALSE(RebindDialogOpen());

        ASSERT_NO_FATAL_FAILURE(Click(FindNth(kRebindButton, FirstRebindableRow())));
        EXPECT_TRUE(RebindDialogOpen());
    }

    TEST_F(PreferencesPanelAssetTest, SpaceAndEnterWhileCapturingAreRefusedAsChordsAndDoNotPressTheCancelButton) {
        OpenWindow();
        ASSERT_NO_FATAL_FAILURE(ClickHotkeysTab());
        ASSERT_NO_FATAL_FAILURE(Click(FindNth(kRebindButton, FirstRebindableRow())));
        ASSERT_TRUE(RebindDialogOpen());

        // A key of the keyboard navigation shows its cursor, which is what lets Space and Enter press the focused item
        PressKey(ImGuiKey_DownArrow);
        ASSERT_TRUE(RebindDialogOpen());
        PressKey(ImGuiKey_Space);
        EXPECT_TRUE(RebindDialogOpen()) << "Space pressed the focused Cancel button";
        PressKey(ImGuiKey_Enter);
        EXPECT_TRUE(RebindDialogOpen()) << "Enter pressed the focused Cancel button";
    }

}  // namespace BECore::Tests
