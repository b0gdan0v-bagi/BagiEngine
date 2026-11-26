#include <BECore/Config/ConfigManager.h>
#include <BECore/EditorLayoutHostProbe.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/RefCounted/New.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/EditorLayout/EditorChordText.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <Widgets/PanelAssets/PanelBars.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 720.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 4;
        constexpr int kSteadyFrames = 10;
        constexpr StringView kAsset = "MainMenuPanel";
        constexpr const char* kHostName = "##Host";
        constexpr StringView kAlpha = "Alpha";
        constexpr StringView kBeta = "Beta";
        constexpr StringView kPauseId = "game.pause";

        /// The commands a panel's subscriber would see, with their arguments
        class CommandRecorder : public SubscriptionHolder {
        public:
            CommandRecorder() {
                Subscribe<EditorCommandEvent, &CommandRecorder::OnCommand>(this);
            }

            [[nodiscard]] size_t Count(PoolString id) const {
                size_t count = 0;
                for (const EditorCommandEvent& seen : events) {
                    count += seen.commandId == id ? 1 : 0;
                }
                return count;
            }

            Vector<EditorCommandEvent> events;

        private:
            void OnCommand(const EditorCommandEvent& event) {
                events.push_back(event);
            }
        };

        Path ShippedFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "MainMenuPanel.xml";
        }

    }  // namespace

    /// The shipped menu asset hosted by a real `AssetPanelWidget` over its real models and a fake editor host, drawn inside a window with a menu bar the way
    /// `EditorLayoutWidget` does; items are found by the labels of `ImGuiItemIndex` and clicked with the mouse
    class MainMenuAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("main_menu_config");
            _settingsDir = TestTempDirectory("main_menu_settings");
            MakeEmptyDirectory(_dir);
            MakeEmptyDirectory(_settingsDir);
            WriteFile(_dir / "MainMenuPanel.xml", ReadFile(ShippedFile()));
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();
            _host.emplace(_settingsDir);
            _hostHandle = EditorLayoutHostRegistry::GetInstance().Register(*_host);

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = kDisplaySize;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            _fonts = LoadImGuiFonts(io, _buffers);
            SetActiveImGuiFonts(_fonts);
            ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
            _index.Arm(*ImGui::GetCurrentContext());
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

        /// Hosts the shipped asset; a test that skips it plays a game whose `WidgetsConfig.xml` lacks the line
        void StartWidget() {
            _widget = New<AssetPanelWidget>();
            _widget->SetConfigManager(_manager);
            _widget->SetAsset(kAsset);
            const UniquePtr<IDeserializer> deserializer = _manager.CreateDeserializer(PoolString::Intern(kAsset));
            ASSERT_TRUE(deserializer);
            ASSERT_TRUE(_widget->Initialize(*deserializer));
            for (const PanelIssue& issue : _widget->GetHolder().Issues()) {
                ADD_FAILURE() << issue.where.c_str() << ": " << issue.message.c_str();
            }
            ASSERT_TRUE(_widget->GetHolder().Issues().empty());
            Settle();
        }

        void SaveLayout(StringView name) {
            ASSERT_TRUE(_host->layouts->Save(name, kProbeLayoutText).has_value());
        }

        /// What the editor does in a frame: the widget updates, then the host window draws the bar between `BeginMenuBar` and `EndMenuBar`
        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            if (_widget) {
                _widget->Update();
            }
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(kDisplaySize);
            constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_MenuBar;
            if (ImGui::Begin(kHostName, nullptr, hostFlags)) {
                if (ImGui::BeginMenuBar()) {
                    PanelBarsRegistry::Draw(kAsset);
                    ImGui::EndMenuBar();
                }
            }
            ImGui::End();
            ImGui::Render();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        /// The visible item whose label has the element id @p id
        const ImGuiItemRecord* Find(StringView id) {
            const String link = Format("###{}", id);
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && item.label.find(link) != String::npos) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// The visible item of a row of a `PanelMenuItems`, whose label is the row's text
        const ImGuiItemRecord* FindRow(StringView text) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && item.MatchesLabel(text)) {
                    return &item;
                }
            }
            return nullptr;
        }

        void ClickAt(const ImGuiItemRecord* item) {
            ASSERT_NE(item, nullptr);
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(item->CenterX(), item->CenterY());
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Settle();
        }

        void Click(StringView id) {
            const ImGuiItemRecord* item = Find(id);
            ASSERT_NE(item, nullptr) << String(id.data(), id.size()).c_str();
            ClickAt(item);
        }

        /// Opens View and then its submenu @p submenu, the way a user does
        void OpenLayoutMenu(StringView submenu) {
            ASSERT_NO_FATAL_FAILURE(Click("view"));
            ASSERT_NO_FATAL_FAILURE(Click(submenu));
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
        CommandRecorder _commands;
    };

    TEST_F(MainMenuAssetTest, TheMenusAreTitlesInTheBarAndQuitRunsItsCommandAfterFileOpens) {
        ASSERT_NO_FATAL_FAILURE(StartWidget());
        EXPECT_NE(Find("file"), nullptr);
        EXPECT_NE(Find("game"), nullptr);
        EXPECT_NE(Find("view"), nullptr);
        EXPECT_EQ(Find("quit"), nullptr) << "a closed menu draws no items";

        ASSERT_NO_FATAL_FAILURE(Click("file"));
        ASSERT_NE(Find("quit"), nullptr);
        ASSERT_NO_FATAL_FAILURE(Click("quit"));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kFileQuit), 1u);
    }

    TEST_F(MainMenuAssetTest, TheViewMenuListsThePanelsInTheOrderOfTheOldMenu) {
        ASSERT_NO_FATAL_FAILURE(StartWidget());
        ASSERT_NO_FATAL_FAILURE(Click("view"));

        float previous = -1.0f;
        for (const StringView id : {"commandPalette", "preferences", "viewport", "sceneEditor", "configEditor", "fileChanges", "spriteLibrary", "assetBrowser", "gamePanel", "console", "generate",
                                    "profiler", "export", "layouts", "saveLayout", "deleteLayout", "resetLayout"}) {
            const ImGuiItemRecord* item = Find(id);
            ASSERT_NE(item, nullptr) << String(id.data(), id.size()).c_str();
            EXPECT_GT(item->minY, previous) << String(id.data(), id.size()).c_str();
            previous = item->minY;
        }
    }

    TEST_F(MainMenuAssetTest, StepOneFrameIsDisabledWhileTheModelCannotStepAndRunsItsCommandOnceItCan) {
        ASSERT_NO_FATAL_FAILURE(StartWidget());
        ASSERT_NO_FATAL_FAILURE(Click("game"));
        ASSERT_NO_FATAL_FAILURE(Click("step"));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kGameStep), 0u) << "a disabled item gives no click";

        // The click left the menu open
        _host->playModel.PressPause();
        Settle();
        ASSERT_NO_FATAL_FAILURE(Click("step"));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kGameStep), 1u);
    }

    TEST_F(MainMenuAssetTest, WithoutALayoutsFolderTheThreeLayoutItemsAreDisabled) {
        _host->layouts.reset();
        ASSERT_NO_FATAL_FAILURE(StartWidget());
        ASSERT_NO_FATAL_FAILURE(Click("view"));

        ASSERT_NO_FATAL_FAILURE(Click("saveLayout"));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kViewSaveLayout), 0u);
        ASSERT_NO_FATAL_FAILURE(Click("layouts"));
        ASSERT_NO_FATAL_FAILURE(Click("deleteLayout"));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kLayoutsMenuOpened), 0u) << "a disabled menu does not open";
    }

    TEST_F(MainMenuAssetTest, TheLayoutsMenuRescansWhenItOpensAndAClickLoadsTheLayoutByName) {
        ASSERT_NO_FATAL_FAILURE(StartWidget());
        ASSERT_NO_FATAL_FAILURE(OpenLayoutMenu("layouts"));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kLayoutsMenuOpened), 1u);
        EXPECT_EQ(FindRow(kAlpha), nullptr);

        // Saved behind the menu's back: it shows the next time the menu opens
        ASSERT_NO_FATAL_FAILURE(SaveLayout(kAlpha));
        ASSERT_NO_FATAL_FAILURE(SaveLayout(kBeta));
        ASSERT_NO_FATAL_FAILURE(Click("view"));
        ASSERT_NO_FATAL_FAILURE(OpenLayoutMenu("layouts"));
        EXPECT_NE(FindRow(kAlpha), nullptr);
        ASSERT_NO_FATAL_FAILURE(ClickAt(FindRow(kBeta)));

        ASSERT_EQ(_host->loadedLayouts.size(), 1u);
        EXPECT_EQ(_host->loadedLayouts.front(), kBeta);
    }

    TEST_F(MainMenuAssetTest, ADeleteLayoutRowRunsTheDeleteCommandWithTheNameAndTheFileGoes) {
        ASSERT_NO_FATAL_FAILURE(StartWidget());
        ASSERT_NO_FATAL_FAILURE(SaveLayout(kAlpha));
        ASSERT_NO_FATAL_FAILURE(SaveLayout(kBeta));
        ASSERT_NO_FATAL_FAILURE(OpenLayoutMenu("deleteLayout"));
        ASSERT_NO_FATAL_FAILURE(ClickAt(FindRow(kAlpha)));

        ASSERT_EQ(_commands.Count(EditorCommandIds::kLayoutsDelete), 1u);
        for (const EditorCommandEvent& event : _commands.events) {
            if (event.commandId == EditorCommandIds::kLayoutsDelete) {
                EXPECT_EQ(event.argument, kAlpha);
            }
        }
        const Vector<String> left = _host->layouts->List();
        ASSERT_EQ(left.size(), 1u);
        EXPECT_EQ(left.front(), kBeta);
    }

    TEST_F(MainMenuAssetTest, WithoutTheAssetTheMenuBarIsEmpty) {
        Settle();

        EXPECT_EQ(Find("file"), nullptr);
        EXPECT_EQ(Find("view"), nullptr);
    }

    TEST_F(MainMenuAssetTest, ASteadyFrameWithTheViewMenuOpenAllocatesNothing) {
        EditorChordText chords;
        ASSERT_NO_FATAL_FAILURE(StartWidget());
        ASSERT_NO_FATAL_FAILURE(Click("view"));
        // The index keeps a record per item and is not what is measured
        _index.Disarm();
        Settle();

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

    /// The chord text of the menus, over the hotkey map of the registered host
    class EditorChordTextTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _dir = TestTempDirectory("chord_text_settings");
            MakeEmptyDirectory(_dir);
            _host.emplace(_dir);
            _handle = EditorLayoutHostRegistry::GetInstance().Register(*_host);
        }

        void TearDown() override {
            _handle.Reset();
            _host.reset();
            RemoveDirectory(_dir);
        }

        Path _dir;
        Optional<ProbeEditorLayoutHost> _host;
        EditorLayoutHostHandle _handle;
    };

    TEST_F(EditorChordTextTest, ShowsTheChordOfTheCommandAndFollowsARebindWithoutAskingTheMapAgain) {
        EditorChordText chords;
        const String before(chords.ChordOf(kPauseId));
        EXPECT_FALSE(before.empty());

        const KeyChord moved{ImGuiKey_K, kChordModCtrl};
        ASSERT_EQ(_host->settings->GetHotkeys().Assign(EditorCommandIds::kGamePause, moved).status, AssignStatus::Assigned);
        EXPECT_EQ(String(chords.ChordOf(kPauseId)), ToString(moved));

        _host->settings->GetHotkeys().Unbind(EditorCommandIds::kGamePause);
        EXPECT_TRUE(chords.ChordOf(kPauseId).empty());
    }

    TEST_F(EditorChordTextTest, AnUnknownCommandAndAMissingHostHaveNoChord) {
        EditorChordText chords;
        EXPECT_TRUE(chords.ChordOf("no.such.command").empty());

        _handle.Reset();
        EXPECT_TRUE(chords.ChordOf(kPauseId).empty());
    }

    TEST_F(EditorChordTextTest, InstallsItselfForThePanelsAndUninstallsWhenItEnds) {
        EXPECT_EQ(PanelChordTextRegistry::GetInstance().Get(), nullptr);
        {
            EditorChordText chords;
            EXPECT_EQ(PanelChordTextRegistry::GetInstance().Get(), &chords);
        }
        EXPECT_EQ(PanelChordTextRegistry::GetInstance().Get(), nullptr);
    }

}  // namespace BECore::Tests
