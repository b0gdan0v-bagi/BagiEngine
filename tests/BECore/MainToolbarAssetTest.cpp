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
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/MainToolbar.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 720.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr int kSteadyFrames = 10;
        /// Long enough at the frame time of the fixture for ImGui's button repeat to start and fire
        constexpr int kHoldFrames = 60;
        constexpr StringView kAsset = "MainToolbarPanel";
        constexpr const char* kHostName = "##Host";
        constexpr float kCenterTolerance = 2.0f;

        /// The commands a panel's subscriber would see
        class CommandRecorder : public SubscriptionHolder {
        public:
            CommandRecorder() {
                Subscribe<EditorCommandEvent, &CommandRecorder::OnCommand>(this);
            }

            [[nodiscard]] size_t Count(PoolString id) const {
                size_t count = 0;
                for (const PoolString seen : ids) {
                    count += seen == id ? 1 : 0;
                }
                return count;
            }

            Vector<PoolString> ids;

        private:
            void OnCommand(const EditorCommandEvent& event) {
                ids.push_back(event.commandId);
            }
        };

        Path ShippedFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "MainToolbarPanel.xml";
        }

    }  // namespace

    /// The shipped toolbar asset hosted by a real `AssetPanelWidget` over its real model and a fake editor host, drawn by `MainToolbar` into a host window in an ImGui
    /// context with no backend; the buttons are found by the labels of `ImGuiItemIndex` and clicked with the mouse
    class MainToolbarAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("main_toolbar_config");
            _settingsDir = TestTempDirectory("main_toolbar_settings");
            MakeEmptyDirectory(_dir);
            MakeEmptyDirectory(_settingsDir);
            WriteFile(_dir / "MainToolbarPanel.xml", ReadFile(ShippedFile()));
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
            ASSERT_TRUE(_widget->GetHolder().Issues().empty());
            Settle();
        }

        /// What the editor does in a frame: the widget updates, then the host window draws the toolbar
        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            if (_widget) {
                _widget->Update();
            }
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(kDisplaySize);
            constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
            if (ImGui::Begin(kHostName, nullptr, hostFlags)) {
                MainToolbar::Draw();
            }
            ImGui::End();
            ImGui::Render();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        /// The visible button whose label has the element id @p id
        const ImGuiItemRecord* Find(StringView id) {
            const String link = Format("##{}", id);
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && item.label.find(link) != String::npos) {
                    return &item;
                }
            }
            return nullptr;
        }

        void Click(StringView id) {
            const ImGuiItemRecord* item = Find(id);
            ASSERT_NE(item, nullptr) << String(id.data(), id.size()).c_str();
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(item->CenterX(), item->CenterY());
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Settle();
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

    TEST_F(MainToolbarAssetTest, PlayIsShownWhileStoppedAndStopWhileRunningAndBothRunThePlayCommand) {
        ASSERT_NO_FATAL_FAILURE(StartWidget());
        EXPECT_NE(Find("play"), nullptr);
        EXPECT_EQ(Find("stop"), nullptr);

        ASSERT_NO_FATAL_FAILURE(Click("play"));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kGamePlay), 1u);

        _host->playModel.PressPlay();
        Settle();
        EXPECT_EQ(Find("play"), nullptr);
        EXPECT_NE(Find("stop"), nullptr);
        ASSERT_NO_FATAL_FAILURE(Click("stop"));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kGamePlay), 2u);
    }

    TEST_F(MainToolbarAssetTest, PauseRunsItsCommandAndIsDisabledWhenTheModelCannotPause) {
        ASSERT_NO_FATAL_FAILURE(StartWidget());
        ASSERT_NO_FATAL_FAILURE(Click("pause"));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kGamePause), 1u);

        _host->hasPlayModel = false;
        Settle();
        ASSERT_NO_FATAL_FAILURE(Click("pause"));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kGamePause), 1u) << "a disabled button gives no click";
    }

    TEST_F(MainToolbarAssetTest, StepIsDisabledUntilPausedAndRepeatsWhileTheButtonIsHeld) {
        ASSERT_NO_FATAL_FAILURE(StartWidget());
        ASSERT_NO_FATAL_FAILURE(Click("step"));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kGameStep), 0u);

        _host->playModel.PressPause();
        Settle();
        const ImGuiItemRecord* step = Find("step");
        ASSERT_NE(step, nullptr);
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(step->CenterX(), step->CenterY());
        Frame();
        io.AddMouseButtonEvent(0, true);
        for (int i = 0; i < kHoldFrames; ++i) {
            Frame();
        }
        io.AddMouseButtonEvent(0, false);
        Frame();

        EXPECT_GT(_commands.Count(EditorCommandIds::kGameStep), 1u);
    }

    TEST_F(MainToolbarAssetTest, TheButtonsSitInTheMiddleOfTheBarWithEqualGapsOnBothSides) {
        ASSERT_NO_FATAL_FAILURE(StartWidget());
        const ImGuiItemRecord* first = Find("play");
        const ImGuiItemRecord* last = Find("step");
        ASSERT_NE(first, nullptr);
        ASSERT_NE(last, nullptr);

        const ImGuiWindow* bar = nullptr;
        for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
            if (window->Name != nullptr && std::strstr(window->Name, "MainToolbar") != nullptr) {
                bar = window;
            }
        }
        ASSERT_NE(bar, nullptr);
        const float left = first->minX - bar->Pos.x;
        const float right = bar->Pos.x + bar->Size.x - last->maxX;
        EXPECT_NEAR(left, right, kCenterTolerance);
    }

    TEST_F(MainToolbarAssetTest, WithoutTheAssetTheToolbarIsAnEmptyBar) {
        Settle();

        EXPECT_EQ(Find("play"), nullptr);
        EXPECT_EQ(Find("pause"), nullptr);
        EXPECT_EQ(Find("step"), nullptr);
    }

    TEST_F(MainToolbarAssetTest, ASteadyFrameAllocatesNothing) {
        ASSERT_NO_FATAL_FAILURE(StartWidget());
        // The index keeps a record per item and is not what is measured
        _index.Disarm();
        Settle();

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

}  // namespace BECore::Tests
