#include <BECore/Config/ConfigManager.h>
#include <BECore/Cook/ContentCook.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorPanelRegistry.h>
#include <Widgets/EditorLayout/Workspaces.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <gtest/gtest.h>
#include <imgui.h>

namespace BECore::Tests {

    namespace {

        constexpr Array<StringView, 4> kWindowKinds{"Docked", "Floating", "Dialogs", "Bar"};

        /// `{}` slot: the window kind
        constexpr const char* kAsset = R"(<PanelAsset id="Registry" title="Registry" window="{}" command="view.fileChanges" open="false">
  <elements>
    <element type="PanelButton" id="go" text="Go" command="fileChanges.rescan"/>
  </elements>
</PanelAsset>)";

        class FlagPanel : public IEditorPanel {
        public:
            [[nodiscard]] bool IsOpen() const override {
                return open;
            }
            void SetOpen(bool value) override {
                open = value;
            }

            bool open = false;
        };

    }  // namespace

    class EditorPanelRegistryTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("panel_registry");
            MakeEmptyDirectory(_dir);
            // One asset of each window kind, in the folder before the config manager scans it
            for (const StringView kind : kWindowKinds) {
                WriteFile(_dir / Format("{}Panel.xml", kind), Format(kAsset, kind));
            }
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
        }

        void TearDown() override {
            _widget.Reset();
            ImGui::DestroyContext();
            RemoveDirectory(_dir);
        }

        /// A widget over an asset of the window kind @p kind
        void StartWidget(StringView kind) {
            const String config = Format("{}Panel", kind);
            _widget = New<AssetPanelWidget>();
            _widget->SetConfigManager(_manager);
            _widget->SetAsset(config);
            const UniquePtr<IDeserializer> deserializer = _manager.CreateDeserializer(PoolString::Intern(StringView(config)));
            ASSERT_TRUE(deserializer);
            ASSERT_TRUE(_widget->Initialize(*deserializer));
        }

        [[nodiscard]] static IEditorPanel* Registered() {
            return EditorPanelRegistry::GetInstance().Find("Registry"_intern);
        }

        Path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        IntrusivePtr<AssetPanelWidget> _widget;
    };

    TEST_F(EditorPanelRegistryTest, ADockedAssetPanelRegistersUnderItsIdAndOpensAndClosesThroughIt) {
        ASSERT_NO_FATAL_FAILURE(StartWidget("Docked"));
        IEditorPanel* const panel = Registered();
        ASSERT_NE(panel, nullptr);
        EXPECT_FALSE(panel->IsOpen());

        panel->SetOpen(true);
        EXPECT_TRUE(_widget->IsOpen());
        panel->SetOpen(false);
        EXPECT_FALSE(_widget->IsOpen());
    }

    TEST_F(EditorPanelRegistryTest, TheRegistrationEndsWithTheWidget) {
        ASSERT_NO_FATAL_FAILURE(StartWidget("Docked"));
        _widget.Reset();

        EXPECT_EQ(Registered(), nullptr);
    }

    TEST_F(EditorPanelRegistryTest, AFloatingPanelADialogHostAndABarDoNotRegister) {
        for (const StringView kind : kWindowKinds) {
            if (kind == "Docked") {
                continue;
            }
            ASSERT_NO_FATAL_FAILURE(StartWidget(kind));
            EXPECT_EQ(Registered(), nullptr) << String(kind).c_str();
            _widget.Reset();
        }
    }

    TEST_F(EditorPanelRegistryTest, OfTwoPanelsOfOneIdTheFirstIsFoundUntilItEnds) {
        EditorPanelRegistry& registry = EditorPanelRegistry::GetInstance();
        const PoolString id = "Twice"_intern;
        FlagPanel first;
        FlagPanel second;
        EditorPanelHandle firstHandle = registry.Register(id, first);
        const EditorPanelHandle secondHandle = registry.Register(id, second);
        EXPECT_EQ(registry.Find(id), &first);

        EditorPanelHandle moved = std::move(firstHandle);
        EXPECT_EQ(registry.Find(id), &first);
        moved.Reset();
        EXPECT_EQ(registry.Find(id), &second);
    }

    /// A docked panel the table lacks would stay open in every workspace and float in every ready layout
    TEST_F(EditorPanelRegistryTest, EveryShippedDockedPanelIsInTheWorkspaceTableUnderItsWindowName) {
        const Path folder = Path(__FILE__).Parent().Parent().Parent() / "config" / "panels";
        String error;
        const Vector<Path> files = ListContentFiles(folder, error);
        ASSERT_TRUE(error.empty()) << error.c_str();
        const auto knownCommand = [](StringView name) { return FindEditorCommand(name) != nullptr; };
        const auto knownModel = [](StringView name) { return IsKnownPanelModel(name); };
        PanelValidationContext context;
        context.isKnownCommand = knownCommand;
        context.isKnownModel = knownModel;
        size_t docked = 0;
        for (const Path& file : files) {
            const PanelLoadResult loaded = LoadPanelAsset(ReadFile(folder / file.View()), context);
            ASSERT_TRUE(loaded.asset);
            if (loaded.asset->window != PanelWindowKind::Docked) {
                continue;
            }
            ++docked;
            const String window = Format("{}###{}", loaded.asset->title, loaded.asset->id);
            bool listed = false;
            for (const WorkspacePanel& panel : GetWorkspacePanels()) {
                listed = listed || (panel.id.ToStringView() == StringView(loaded.asset->id) && panel.window == StringView(window));
            }
            EXPECT_TRUE(listed) << window.c_str();
        }
        // The two panels written in C++ have no asset
        EXPECT_EQ(docked + 2, GetWorkspacePanels().size());
    }

}  // namespace BECore::Tests
