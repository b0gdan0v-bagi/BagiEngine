#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/PanelHotReloadProbe.h>
#include <BECore/RefCounted/New.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/ConfigEvents.h>
#include <Generated/PanelHotReloadProbe.gen.hpp>
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
        constexpr float kSearchStep = 6.0f;
        constexpr const char* kWindowName = "###Hot";
        constexpr const char* kBannerWindowName = "###HotPanel";
        constexpr StringView kConfigName = "HotPanel";
        const PoolString kConfig = "HotPanel"_intern;

        /// `{}` slots: the window title, the button text, the model class of the source, the name of the source
        constexpr const char* kAsset = R"(<PanelAsset id="Hot" title="{}" command="view.fileChanges" open="true" width="300" height="200">
  <sources><source name="{}" model="{}"/></sources>
  <elements>
    <element type="PanelButton" id="go" text="{}" command="fileChanges.rescan"/>
  </elements>
</PanelAsset>)";

        constexpr const char* kBadAsset = R"(<PanelAsset id="Hot" title="Broken" command="view.fileChanges" open="true">
  <elements>
    <element type="PanelNobody" id="go"/>
  </elements>
</PanelAsset>)";

        constexpr const char* kClosedAsset = R"(<PanelAsset id="Hot" title="Closed" command="view.fileChanges" open="false">
  <sources><source name="a" model="CountingPanelModel"/></sources>
  <elements>
    <element type="PanelButton" id="go" text="Go" command="fileChanges.rescan"/>
  </elements>
</PanelAsset>)";

        /// `{}` slot: the binding of `titleBind`; the source `a` is a `ProbePanelModel`, whose text field `lastCommand` is the last command it was told
        constexpr const char* kTitledAsset = R"(<PanelAsset id="Hot" title="Plain" titleBind="{}" command="view.fileChanges" open="true" width="300" height="200">
  <sources><source name="a" model="ProbePanelModel"/></sources>
  <elements>
    <element type="PanelButton" id="go" text="Go" command="fileChanges.rescan"/>
  </elements>
</PanelAsset>)";

        /// `{}` slots: the title and the `open` attribute; the source `a` is a `HookPanelModel`
        constexpr const char* kHookAsset = R"(<PanelAsset id="Hot" title="{}" command="view.fileChanges" open="{}" width="300" height="200">
  <sources><source name="a" model="HookPanelModel"/></sources>
  <elements>
    <element type="PanelButton" id="go" text="Go" command="fileChanges.rescan"/>
  </elements>
</PanelAsset>)";

        /// `{}` slots: the `window` kind and the `command` of the asset; the source `a` is a `DialogPanelModel` whose bool is true, so the popup is open at once
        constexpr const char* kWindowKindAsset = R"(<PanelAsset id="Hot" title="Kinds" window="{}" command="{}" open="true" width="300" height="200">
  <sources><source name="a" model="DialogPanelModel"/></sources>
  <elements>
    <element type="PanelPopup" id="dialog" title="Sure?" open="a.dialog">
      <elements><element type="PanelButton" id="yes" text="Yes" command="fileChanges.rescan"/></elements>
    </element>
  </elements>
</PanelAsset>)";
        constexpr const char* kPopupWindowName = "Sure?##dialog";

        constexpr int kSteadyFrames = 8;

        String Asset(StringView title, StringView button, StringView model = "CountingPanelModel", StringView source = "a") {
            return Format(kAsset, title, source, model, button);
        }

    }  // namespace

    /// The widget in an ImGui context of its own with no backend, reading a config folder of its own through a `ConfigManager` of its own
    class PanelAssetHotReloadTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            DialogPanelModel::commands = 0;
            CountingPanelModel::Reset();
            ReloadingPanelModel::Reset();
            _dir = TestTempDirectory("panel_hot");
            MakeEmptyDirectory(_dir);
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = kDisplaySize;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            _fonts = LoadImGuiFonts(io, _buffers);
            SetActiveImGuiFonts(_fonts);
            ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
        }

        void TearDown() override {
            _widget.Reset();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
            RemoveDirectory(_dir);
        }

        /// Writes the file, then starts the manager and the widget over it
        void Start(StringView text) {
            WriteFile(_dir / "HotPanel.xml", text);
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();
            _widget = New<AssetPanelWidget>();
            _widget->SetConfigManager(_manager);
            _widget->SetAsset(kConfigName);
            const UniquePtr<IDeserializer> deserializer = _manager.CreateDeserializer(kConfigName);
            ASSERT_TRUE(deserializer);
            ASSERT_TRUE(_widget->Initialize(*deserializer));
            Settle();
        }

        /// A saved edit of the file, as the file watcher would deliver it
        void Edit(StringView text) {
            WriteFile(_dir / "HotPanel.xml", text);
            ASSERT_TRUE(_manager.ReloadConfig(kConfig));
            Settle();
        }

        void Settle() {
            Frame();
            Frame();
        }

        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            _widget->Update();
            ImGui::Render();
        }

        static ImGuiWindow* Window() {
            return ImGui::FindWindowByName(kWindowName);
        }

        static float ContentHeight() {
            const ImGuiWindow* window = Window();
            return window != nullptr ? window->ContentSize.y : 0.0f;
        }

        /// The id of a control: the element id and the label hashed onto the id of the window
        static ImGuiID ButtonId(const char* label) {
            const ImGuiWindow* window = Window();
            if (window == nullptr) {
                return 0;
            }
            return ImHashStr(label, 0, ImHashStr("go", 0, window->ID));
        }

        /// A point of the window where the mouse is over the control @p id, found by moving it over the window; none when no point is
        Optional<ImVec2> TryLocate(ImGuiID id, const ImGuiWindow* window = Window()) {
            if (window == nullptr || id == 0) {
                return std::nullopt;
            }
            const ImVec2 from = window->Pos;
            const ImVec2 to(from.x + window->Size.x, from.y + window->Size.y);
            ImGuiIO& io = ImGui::GetIO();
            for (float y = from.y; y < to.y; y += kSearchStep) {
                for (float x = from.x; x < to.x; x += kSearchStep) {
                    io.AddMousePosEvent(x, y);
                    Settle();
                    if (ImGui::GetCurrentContext()->HoveredId == id) {
                        return ImVec2(x, y);
                    }
                }
            }
            return std::nullopt;
        }

        void ClickAt(ImVec2 point) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Frame();
        }

        static void Command(PoolString id) {
            EditorCommandEvent::Emit(id);
        }

        Path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        IntrusivePtr<AssetPanelWidget> _widget;
    };

    TEST_F(PanelAssetHotReloadTest, AGoodFileIsDrawnAndAClickReachesTheModelThroughTheCommand) {
        ASSERT_NO_FATAL_FAILURE(Start(Asset("Good title", "Go")));
        ASSERT_NE(Window(), nullptr);
        EXPECT_TRUE(_widget->GetHolder().Issues().empty());
        EXPECT_EQ(CountingPanelModel::created, 1);

        const Optional<ImVec2> point = TryLocate(ButtonId("Go"));
        ASSERT_TRUE(point.has_value()) << "the button is not at the id of its asset's ids";
        ClickAt(*point);

        EXPECT_EQ(CountingPanelModel::commands, 1);
    }

    TEST_F(PanelAssetHotReloadTest, AChangedFileShowsTheNewTextAfterTheChangeEvent) {
        ASSERT_NO_FATAL_FAILURE(Start(Asset("Old title", "Go")));
        ASSERT_NE(Window(), nullptr);
        EXPECT_TRUE(TryLocate(ButtonId("Go")).has_value());

        ASSERT_NO_FATAL_FAILURE(Edit(Asset("New title", "Run")));

        ASSERT_NE(Window(), nullptr);
        EXPECT_TRUE(TryLocate(ButtonId("Run")).has_value());
        EXPECT_FALSE(TryLocate(ButtonId("Go")).has_value());
    }

    TEST_F(PanelAssetHotReloadTest, ABrokenFileShowsTheBannerAndTheOldContentBelowItAndARestoredOneClearsIt) {
        const String good = Asset("Good title", "Go");
        ASSERT_NO_FATAL_FAILURE(Start(good));
        const float clean = ContentHeight();

        ASSERT_NO_FATAL_FAILURE(Edit(kBadAsset));
        EXPECT_FALSE(_widget->GetHolder().Issues().empty());
        EXPECT_GT(ContentHeight(), clean) << "no banner";
        EXPECT_TRUE(TryLocate(ButtonId("Go")).has_value()) << "the last good asset is not drawn below the banner";

        // A file that could not be read at all: the manager tells the panel why
        ConfigEvents::ConfigReloadFailedEvent::Emit(kConfig, String("not well-formed"));
        EXPECT_EQ(_widget->GetHolder().Failure(), "not well-formed");

        ASSERT_NO_FATAL_FAILURE(Edit(good));
        EXPECT_TRUE(_widget->GetHolder().Issues().empty());
        EXPECT_TRUE(_widget->GetHolder().Failure().empty());
        EXPECT_FLOAT_EQ(ContentHeight(), clean);
    }

    TEST_F(PanelAssetHotReloadTest, AFailureToldAfterAGoodEditWithinOneFrameStillShowsTheBanner) {
        ASSERT_NO_FATAL_FAILURE(Start(Asset("Good title", "Go")));

        // Both events arrive before the panel's next Update: the load of the good file must not wipe the failure that came after it
        WriteFile(_dir / "HotPanel.xml", Asset("Newer title", "Run"));
        ASSERT_TRUE(_manager.ReloadConfig(kConfig));
        ConfigEvents::ConfigReloadFailedEvent::Emit(kConfig, String("not well-formed"));
        Settle();

        EXPECT_EQ(_widget->GetHolder().Failure(), "not well-formed");
        EXPECT_TRUE(TryLocate(ButtonId("Run")).has_value()) << "the good edit was lost";
    }

    TEST_F(PanelAssetHotReloadTest, AFirstLoadThatFailedShowsTheBannerAloneAndAFixedFileDrawsTheAsset) {
        ASSERT_NO_FATAL_FAILURE(Start(kBadAsset));
        // Without an asset the window has no asset id to be named by: the config name stands in
        EXPECT_NE(ImGui::FindWindowByName(kBannerWindowName), nullptr);
        EXPECT_TRUE(_widget->IsOpen());
        EXPECT_FALSE(_widget->GetHolder().Current());
        EXPECT_FALSE(TryLocate(ButtonId("Go")).has_value());

        ASSERT_NO_FATAL_FAILURE(Edit(Asset("Fixed", "Go")));
        EXPECT_TRUE(TryLocate(ButtonId("Go")).has_value());
    }

    TEST_F(PanelAssetHotReloadTest, AModelOfAnUnchangedSourceKeepsItsInstanceAcrossAReload) {
        ASSERT_NO_FATAL_FAILURE(Start(Asset("One", "Go")));
        ASSERT_EQ(CountingPanelModel::created, 1);

        ASSERT_NO_FATAL_FAILURE(Edit(Asset("Two", "Go")));
        EXPECT_EQ(CountingPanelModel::created, 1) << "the model was made again";
        EXPECT_EQ(CountingPanelModel::alive, 1);

        ASSERT_NO_FATAL_FAILURE(Edit(Asset("Two", "Go", "CountingPanelModel", "renamed")));
        EXPECT_EQ(CountingPanelModel::created, 2);
        EXPECT_EQ(CountingPanelModel::alive, 1) << "the model of the old source name lives on";

        ASSERT_NO_FATAL_FAILURE(Edit(Asset("Two", "Go", "ProbePanelModel", "renamed")));
        EXPECT_EQ(CountingPanelModel::alive, 0);
    }

    TEST_F(PanelAssetHotReloadTest, AReloadThatDropsTheModelDuringItsCommandDoesNotDestroyItWhileItRuns) {
        ASSERT_NO_FATAL_FAILURE(Start(Asset("One", "Go", "ReloadingPanelModel")));
        ASSERT_EQ(ReloadingPanelModel::alive, 1);
        // What applying the panel's own file does from a command: the file changes and the manager announces it from inside the command
        ReloadingPanelModel::hook = [this] {
            WriteFile(_dir / "HotPanel.xml", Asset("Two", "Go", "CountingPanelModel", "renamed"));
            EXPECT_TRUE(_manager.ReloadConfig(kConfig));
        };

        Command(EditorCommandIds::kFileChangesRescan);

        EXPECT_EQ(ReloadingPanelModel::commandsFinished, 1);
        EXPECT_EQ(ReloadingPanelModel::destroyedInCommand, 0) << "the model was destroyed while its command ran";
        EXPECT_EQ(ReloadingPanelModel::alive, 0) << "the reload was not applied after the command";
        EXPECT_EQ(CountingPanelModel::alive, 1);
    }

    TEST_F(PanelAssetHotReloadTest, ClosedDrawsNothingAndNoCommandReachesAModelButTheOpenCommandOpens) {
        ASSERT_NO_FATAL_FAILURE(Start(kClosedAsset));
        EXPECT_FALSE(_widget->IsOpen());
        EXPECT_EQ(Window(), nullptr);
        EXPECT_EQ(CountingPanelModel::updates, 0);

        Command(EditorCommandIds::kFileChangesRescan);
        EXPECT_EQ(CountingPanelModel::commands, 0);

        Command(EditorCommandIds::kViewFileChanges);
        Settle();
        EXPECT_TRUE(_widget->IsOpen());
        EXPECT_NE(Window(), nullptr);
        EXPECT_GT(CountingPanelModel::updates, 0);
        Command(EditorCommandIds::kFileChangesRescan);
        EXPECT_EQ(CountingPanelModel::commands, 1);

        Command(EditorCommandIds::kViewFileChanges);
        EXPECT_FALSE(_widget->IsOpen());
        Command(EditorCommandIds::kFileChangesRescan);
        EXPECT_EQ(CountingPanelModel::commands, 1);
    }

    TEST_F(PanelAssetHotReloadTest, TheModelsAreToldEveryChangeOfTheOpenFlagAndEveryDrawnFrameEnds) {
        HookPanelModel::log.clear();
        ASSERT_NO_FATAL_FAILURE(Start(String(Format(kHookAsset, "Hook", "false"))));
        Settle();
        EXPECT_EQ(HookPanelModel::log, "") << "a closed panel is neither updated nor told anything";

        Command(EditorCommandIds::kViewFileChanges);
        Settle();
        EXPECT_EQ(HookPanelModel::log, "+UEUE");

        HookPanelModel::log.clear();
        Command(EditorCommandIds::kViewFileChanges);
        Settle();
        EXPECT_EQ(HookPanelModel::log, "-");
    }

    TEST_F(PanelAssetHotReloadTest, AnAssetThatOpensAtStartTellsTheModelOnTheFirstFrame) {
        HookPanelModel::log.clear();
        ASSERT_NO_FATAL_FAILURE(Start(String(Format(kHookAsset, "Hook", "true"))));
        EXPECT_EQ(HookPanelModel::log, "+UEUE");
    }

    TEST_F(PanelAssetHotReloadTest, TheCloseButtonOfTheWindowIsToldOnceAndStopsTheUpdates) {
        HookPanelModel::log.clear();
        ASSERT_NO_FATAL_FAILURE(Start(String(Format(kHookAsset, "Hook", "true"))));
        ImGuiWindow* window = Window();
        ASSERT_NE(window, nullptr);

        const Optional<ImVec2> point = TryLocate(window->GetID("#CLOSE"));
        ASSERT_TRUE(point.has_value()) << "the close button was not found";
        HookPanelModel::log.clear();
        ClickAt(*point);
        EXPECT_FALSE(_widget->IsOpen());
        EXPECT_EQ(HookPanelModel::log.find('-'), HookPanelModel::log.rfind('-')) << "told twice: " << HookPanelModel::log;
        EXPECT_NE(HookPanelModel::log.find('-'), String::npos);

        HookPanelModel::log.clear();
        Settle();
        EXPECT_EQ(HookPanelModel::log, "") << "the panel went on after its close button";
    }

    TEST_F(PanelAssetHotReloadTest, AReloadThatKeepsTheModelDoesNotTellItAgainAndOneThatDropsOrAddsOneTellsIt) {
        HookPanelModel::log.clear();
        ASSERT_NO_FATAL_FAILURE(Start(String(Format(kHookAsset, "Hook", "true"))));
        HookPanelModel::log.clear();

        ASSERT_NO_FATAL_FAILURE(Edit(String(Format(kHookAsset, "Reloaded", "true"))));
        EXPECT_EQ(HookPanelModel::log, "UEUE");

        HookPanelModel::log.clear();
        ASSERT_NO_FATAL_FAILURE(Edit(Asset("Other", "Go", "CountingPanelModel", "other")));
        EXPECT_EQ(HookPanelModel::log, "-");
    }

    TEST_F(PanelAssetHotReloadTest, AWidgetThatDiesOpenTellsItsModelsItClosed) {
        HookPanelModel::log.clear();
        ASSERT_NO_FATAL_FAILURE(Start(String(Format(kHookAsset, "Hook", "true"))));
        HookPanelModel::log.clear();

        _widget.Reset();
        EXPECT_EQ(HookPanelModel::log, "-");
    }

    TEST_F(PanelAssetHotReloadTest, ABoundTitleFollowsTheTextAndKeepsTheIdAfterTheMarker) {
        ASSERT_NO_FATAL_FAILURE(Start(Format(kTitledAsset, "a.lastCommand")));
        // The model has told nothing yet: its text is empty and readable, so the title is the empty text
        EXPECT_EQ(_widget->GetWindowTitle(), "###Hot");

        Command("probe.first"_intern);
        Settle();
        EXPECT_EQ(_widget->GetWindowTitle(), "probe.first###Hot");

        Command("probe.second"_intern);
        Settle();
        EXPECT_EQ(_widget->GetWindowTitle(), "probe.second###Hot");
        EXPECT_EQ(ImGui::FindWindowByName(kWindowName), Window()) << "the window is found by its id after the title changed";
    }

    TEST_F(PanelAssetHotReloadTest, AnUnreadableBoundTitleKeepsThePlainTitleAndAReloadRebindsIt) {
        ASSERT_NO_FATAL_FAILURE(Start(Format(kTitledAsset, "nobody.text")));
        EXPECT_EQ(_widget->GetWindowTitle(), "Plain###Hot");

        ASSERT_NO_FATAL_FAILURE(Edit(Format(kTitledAsset, "a.lastCommand")));
        Command("probe.third"_intern);
        Settle();
        EXPECT_EQ(_widget->GetWindowTitle(), "probe.third###Hot");

        ASSERT_NO_FATAL_FAILURE(Edit(Asset("Another", "Go")));
        EXPECT_EQ(_widget->GetWindowTitle(), "Another###Hot") << "the bound text outlived the binding";
    }

    TEST_F(PanelAssetHotReloadTest, ASteadyFrameWithABoundTitleAllocatesNothing) {
        ASSERT_NO_FATAL_FAILURE(Start(Format(kTitledAsset, "a.lastCommand")));
        Command("probe.steady"_intern);
        for (int i = 0; i < kSteadyFrames; ++i) {
            Frame();
        }
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

    TEST_F(PanelAssetHotReloadTest, ADialogsAssetIsAnInvisibleAlwaysOpenHostWhoseModalIsDrawnAndTakesClicks) {
        ASSERT_NO_FATAL_FAILURE(Start(Format(kWindowKindAsset, "Dialogs", "view.fileChanges")));
        const ImGuiWindow* host = Window();
        ASSERT_NE(host, nullptr);
        constexpr ImGuiWindowFlags kInvisible = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings;
        EXPECT_EQ(host->Flags & kInvisible, kInvisible);
        EXPECT_FALSE(host->HasCloseButton);

        const ImGuiWindow* popup = ImGui::FindWindowByName(kPopupWindowName);
        ASSERT_NE(popup, nullptr) << "the modal of the host is not drawn";
        const Optional<ImVec2> yes = TryLocate(ImHashStr("Yes", 0, ImHashStr("yes", 0, popup->ID)), popup);
        ASSERT_TRUE(yes.has_value());
        ClickAt(*yes);
        EXPECT_EQ(DialogPanelModel::commands, 1);

        // Its own command does not close it, and it is always open
        Command(EditorCommandIds::kViewFileChanges);
        Settle();
        EXPECT_TRUE(_widget->IsOpen());
        EXPECT_NE(Window(), nullptr);
    }

    TEST_F(PanelAssetHotReloadTest, ADialogsAssetWithNoCommandHasItsModelsUpdatedEveryFrame) {
        HookPanelModel::log.clear();
        ASSERT_NO_FATAL_FAILURE(Start(R"(<PanelAsset id="Hot" title="Hooks" window="Dialogs" open="true">
  <sources><source name="a" model="HookPanelModel"/></sources>
  <elements/>
</PanelAsset>)"));
        EXPECT_EQ(HookPanelModel::log, "+UEUE");
        EXPECT_TRUE(_widget->IsOpen());
    }

    TEST_F(PanelAssetHotReloadTest, AFloatingWindowIsNeverDockedAndADockedOneMayBe) {
        ASSERT_NO_FATAL_FAILURE(Start(Format(kWindowKindAsset, "Floating", "view.fileChanges")));
        ASSERT_NE(Window(), nullptr);
        EXPECT_NE(Window()->Flags & ImGuiWindowFlags_NoDocking, 0);

        ASSERT_NO_FATAL_FAILURE(Edit(Format(kWindowKindAsset, "Docked", "view.fileChanges")));
        EXPECT_EQ(Window()->Flags & ImGuiWindowFlags_NoDocking, 0);
    }

}  // namespace BECore::Tests
