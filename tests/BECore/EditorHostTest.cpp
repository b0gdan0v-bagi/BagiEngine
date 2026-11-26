#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/PanelHotReloadProbe.h>
#include <BECore/RefCounted/New.h>
#include <BECore/ScopedEditorHost.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/CommandFilter.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/HotkeyMap.h>
#include <Widgets/EditorLayout/PreferencesHotkeys.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <algorithm>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kConfigName = "HostPanel";

        /// A panel that is open from the start, with a model, under a command a browser does not offer
        constexpr const char* kUnofferedPanel = R"(<PanelAsset id="HostPanel" title="Changes" command="view.fileChanges" open="true" width="300" height="200">
  <sources><source name="a" model="CountingPanelModel"/></sources>
  <elements>
    <element type="PanelText" id="text" text="Changes"/>
  </elements>
</PanelAsset>)";

        bool Offers(const Vector<const EditorCommand*>& commands, StringView id) {
            return std::ranges::any_of(commands, [id](const EditorCommand* command) { return command->id.ToStringView() == id; });
        }

        bool HasRow(const Vector<HotkeyRow>& rows, StringView id) {
            return std::ranges::any_of(rows, [id](const HotkeyRow& row) { return StringView(row.id) == id; });
        }

    }  // namespace

    TEST(EditorHostTest, ABrowserDoesNotOfferWhatNeedsAProcessAFileWatchOrAFileToTakeAway) {
        const ScopedEditorHost host(EditorHostKind::Browser);
        for (const StringView id : {"view.generate", "artgen.run", "artgen.accept", "view.fileChanges", "fileChanges.apply", "profiler.trace"}) {
            EXPECT_FALSE(IsEditorCommandAvailable(id)) << id.data();
        }
        for (const StringView id : {"scene.save", "view.profiler", "profiler.spikeClear", "file.quit", "view.console", "game.pause"}) {
            EXPECT_TRUE(IsEditorCommandAvailable(id)) << id.data();
        }
    }

    TEST(EditorHostTest, ADesktopOffersEveryCommandOfTheTable) {
        const ScopedEditorHost host(EditorHostKind::Desktop);
        for (const EditorCommand& command : GetEditorCommands()) {
            EXPECT_TRUE(IsEditorCommandAvailable(command.id.ToStringView())) << command.id.ToStringView().data();
        }
    }

    TEST(EditorHostTest, ThePaletteAndTheHotkeyRowsSkipWhatTheHostDoesNotOffer) {
        const ScopedEditorHost host(EditorHostKind::Browser);
        for (const StringView query : {"", "generate", "file changes", "trace"}) {
            const Vector<const EditorCommand*> found = FilterCommands(query, GetEditorCommands());
            EXPECT_FALSE(Offers(found, "view.generate")) << query.data();
            EXPECT_FALSE(Offers(found, "view.fileChanges")) << query.data();
            EXPECT_FALSE(Offers(found, "profiler.trace")) << query.data();
        }
        EXPECT_TRUE(Offers(FilterCommands("save scene", GetEditorCommands()), "scene.save"));

        PreferencesHotkeys hotkeys;
        hotkeys.Rebuild(HotkeyMap{});
        EXPECT_FALSE(HasRow(hotkeys.GetRows(), "view.generate"));
        EXPECT_FALSE(HasRow(hotkeys.GetRows(), "fileChanges.rescan"));
        EXPECT_TRUE(HasRow(hotkeys.GetRows(), "scene.save"));
    }

    TEST(EditorHostTest, TheNoticeIsDueOnceAndADesktopNeverTakesIt) {
        ReloadNotice notice;
        EXPECT_FALSE(notice.Take(EditorHostKind::Desktop));
        EXPECT_TRUE(notice.Take(EditorHostKind::Browser));
        EXPECT_FALSE(notice.Take(EditorHostKind::Browser));
    }

    /// An `AssetPanelWidget` in an ImGui context with no backend, reading a config folder of its own
    class EditorHostPanelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CountingPanelModel::Reset();
            _dir = TestTempDirectory("editor_host");
            MakeEmptyDirectory(_dir);
            WriteFile(_dir / "HostPanel.xml", kUnofferedPanel);
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(1280.0f, 900.0f);
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

        void Start() {
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();
            _widget = New<AssetPanelWidget>();
            _widget->SetConfigManager(_manager);
            _widget->SetAsset(kConfigName);
            const UniquePtr<IDeserializer> deserializer = _manager.CreateDeserializer(kConfigName);
            ASSERT_TRUE(deserializer);
            ASSERT_TRUE(_widget->Initialize(*deserializer));
            Frame();
        }

        void Frame() {
            ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            _widget->Update();
            ImGui::Render();
        }

        static bool IsWindowDrawn() {
            const ImGuiWindow* window = ImGui::FindWindowByName("###HostPanel");
            return window != nullptr && window->Active;
        }

        Path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        IntrusivePtr<AssetPanelWidget> _widget;
    };

    TEST_F(EditorHostPanelTest, APanelWhoseCommandTheHostDoesNotOfferStaysClosedAndMakesNoModel) {
        const ScopedEditorHost host(EditorHostKind::Browser);
        ASSERT_NO_FATAL_FAILURE(Start());

        EditorCommandEvent::Emit("view.fileChanges"_intern);
        Frame();

        EXPECT_FALSE(_widget->IsOpen());
        EXPECT_FALSE(IsWindowDrawn());
        EXPECT_EQ(CountingPanelModel::created, 0);
    }

    TEST_F(EditorHostPanelTest, TheSamePanelIsHostedOnADesktop) {
        const ScopedEditorHost host(EditorHostKind::Desktop);
        ASSERT_NO_FATAL_FAILURE(Start());

        EXPECT_TRUE(_widget->IsOpen());
        EXPECT_TRUE(IsWindowDrawn());
        EXPECT_EQ(CountingPanelModel::created, 1);
    }

}  // namespace BECore::Tests
