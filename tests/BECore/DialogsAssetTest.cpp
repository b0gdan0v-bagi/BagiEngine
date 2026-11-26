#include <BECore/Config/ConfigManager.h>
#include <BECore/EditorLayoutHostProbe.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <Widgets/SceneEditor/SceneDialogEvents.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr StringView kSaveLayoutAsset = "EditorDialogsPanel";
        constexpr StringView kSceneDialogsAsset = "SceneDialogsPanel";
        constexpr StringView kTypedName = "Wide";
        constexpr StringView kSceneNodes = R"(<node name="Parent"/>)";
        constexpr StringView kComponentSuffix = "Component";
        constexpr StringView kDialogTitle = "Add Component";
        constexpr Array<StringView, 2> kShippedAssets = {kSaveLayoutAsset, kSceneDialogsAsset};

        Path ShippedFile(StringView asset) {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / Path(Format("{}.xml", asset));
        }

        bool Contains(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

    }  // namespace

    /// The shipped dialog assets (`window="Dialogs"`) hosted by a real `AssetPanelWidget` over their real models and a fake editor host, in an ImGui context with no backend,
    /// found and driven by the labels of `ImGuiItemIndex` with the mouse and the keyboard of ImGui
    class DialogsAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("dialogs_asset_config");
            _settingsDir = TestTempDirectory("dialogs_asset_settings");
            MakeEmptyDirectory(_dir);
            MakeEmptyDirectory(_settingsDir);
            // The manager lists the folder once, so the files are there before it starts
            for (const StringView asset : kShippedAssets) {
                WriteFile(_dir / Path(Format("{}.xml", asset)), ReadFile(ShippedFile(asset)));
            }
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
        }

        void TearDown() override {
            _widgets.clear();
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
            _hostHandle.Reset();
            _host.reset();
            EngineEnvironment::UseSceneManager({});
            _sceneManager.Reset();
            RemoveDirectory(_dir);
            RemoveDirectory(_settingsDir);
        }

        /// A scene `Main` with one node `Parent`, made the active scene of `CoreManager`
        void UseScene() {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(SceneConfigText(kSceneNodes)));
            _sceneManager = New<SceneManager>();
            _sceneManager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _sceneManager->Initialize();
            EngineEnvironment::UseSceneManager(_sceneManager);
            _sceneManager->SetActiveScene("Main"_intern);
            _scene = &Require(_sceneManager->GetActiveScene());
            _parent = &Require(FindNodeBelow(_scene->GetRootNode(), "Parent"));
        }

        /// Hosts the shipped asset @p asset, copied into the config folder of the test
        void Start(StringView asset) {
            IntrusivePtr<AssetPanelWidget> widget = New<AssetPanelWidget>();
            widget->SetConfigManager(_manager);
            widget->SetAsset(asset);
            const UniquePtr<IDeserializer> deserializer = _manager.CreateDeserializer(PoolString::Intern(asset));
            ASSERT_TRUE(deserializer);
            ASSERT_TRUE(widget->Initialize(*deserializer));
            ASSERT_TRUE(widget->GetHolder().Issues().empty());
            _widgets.push_back(widget);
            Settle();
        }

        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            for (const IntrusivePtr<AssetPanelWidget>& widget : _widgets) {
                widget->Update();
            }
            ImGui::Render();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        const ImGuiItemRecord* Find(StringView part) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && Contains(item.label, part)) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// The first row of the Add Component list in view: a class name ends in `Component`, the dialog's own title is the only other label with it
        const ImGuiItemRecord* FindFirstComponentRow() {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && Contains(item.label, kComponentSuffix) && !Contains(item.label, kDialogTitle)) {
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

        void Type(const char* text) {
            ImGui::GetIO().AddInputCharactersUTF8(text);
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
        Vector<IntrusivePtr<AssetPanelWidget>> _widgets;
        IntrusivePtr<SceneManager> _sceneManager;
        Scene* _scene = nullptr;
        SceneNode* _parent = nullptr;
    };

    TEST_F(DialogsAssetTest, TheSaveLayoutCommandOpensTheDialogAndTheSaveButtonStoresTheTypedName) {
        ASSERT_NO_FATAL_FAILURE(Start(kSaveLayoutAsset));
        EXPECT_EQ(Find("Cancel"), nullptr) << "the host draws no window of its own and no dialog while nothing opened it";

        EditorCommandEvent::Emit(EditorCommandIds::kViewSaveLayout);
        Settle();
        ASSERT_NE(Find("Cancel"), nullptr);
        const ImGuiItemRecord* save = Find("Save");
        ASSERT_NE(save, nullptr);

        // The field has the focus at once: typing needs no click, and an empty name cannot be saved yet
        Type(kTypedName.data());
        ASSERT_NO_FATAL_FAILURE(Click(Find("Save")));

        EXPECT_TRUE(_host->layouts->Exists(kTypedName));
        EXPECT_EQ(Find("Cancel"), nullptr) << "a saved dialog closes";
    }

    TEST_F(DialogsAssetTest, EnterSavesAndEscClosesAndTheDialogOpensAgainAfterBoth) {
        ASSERT_NO_FATAL_FAILURE(Start(kSaveLayoutAsset));

        EditorCommandEvent::Emit(EditorCommandIds::kViewSaveLayout);
        Settle();
        PressKey(ImGuiKey_Escape);
        EXPECT_EQ(Find("Cancel"), nullptr);
        EXPECT_TRUE(_host->layouts->List().empty());

        EditorCommandEvent::Emit(EditorCommandIds::kViewSaveLayout);
        Settle();
        ASSERT_NE(Find("Cancel"), nullptr);
        Type(kTypedName.data());
        PressKey(ImGuiKey_Enter);

        EXPECT_TRUE(_host->layouts->Exists(kTypedName));
        EXPECT_EQ(Find("Cancel"), nullptr);
    }

    TEST_F(DialogsAssetTest, AnExistingNameShowsOverwriteAndEnterDoesNotReplaceIt) {
        ASSERT_TRUE(_host->layouts->Save(kTypedName, "an older layout"));
        ASSERT_NO_FATAL_FAILURE(Start(kSaveLayoutAsset));
        EditorCommandEvent::Emit(EditorCommandIds::kViewSaveLayout);
        Settle();
        Type(kTypedName.data());

        EXPECT_NE(Find("Overwrite"), nullptr);
        PressKey(ImGuiKey_Enter);
        EXPECT_NE(Find("Cancel"), nullptr);
        EXPECT_EQ(_host->layouts->Load(kTypedName).value(), "an older layout");

        ASSERT_NO_FATAL_FAILURE(Click(Find("Overwrite")));
        EXPECT_EQ(Find("Cancel"), nullptr);
        EXPECT_EQ(_host->layouts->Load(kTypedName).value(), kProbeLayoutText);
    }

    TEST_F(DialogsAssetTest, TheAddNodeRequestOpensTheDialogAndOkAddsTheNodeUnderTheParent) {
        ASSERT_NO_FATAL_FAILURE(UseScene());
        ASSERT_NO_FATAL_FAILURE(Start(kSceneDialogsAsset));
        EXPECT_EQ(Find("Cancel"), nullptr);

        SceneAddNodeRequestEvent::Emit(_scene, _parent);
        Settle();
        ASSERT_NE(Find("Cancel"), nullptr);
        Type(kTypedName.data());
        ASSERT_NO_FATAL_FAILURE(Click(Find("OK")));

        ASSERT_EQ(_parent->GetChildren().size(), 1u);
        EXPECT_TRUE(Contains(_parent->GetChildren()[0]->GetName().ToStringView(), kTypedName)) << "typed into the field that held the default name";
        EXPECT_EQ(Find("Cancel"), nullptr);
    }

    TEST_F(DialogsAssetTest, AClickOnAComponentRowAddsOneComponentToTheNode) {
        ASSERT_NO_FATAL_FAILURE(UseScene());
        ASSERT_NO_FATAL_FAILURE(Start(kSceneDialogsAsset));

        SceneAddComponentRequestEvent::Emit(_scene, _parent);
        Settle();
        ASSERT_NO_FATAL_FAILURE(Click(FindFirstComponentRow()));

        EXPECT_EQ(_parent->GetComponents().size(), 1u);
        EXPECT_EQ(Find("Cancel"), nullptr);
    }

    TEST_F(DialogsAssetTest, EscClosesTheAddComponentDialogAndItOpensAgain) {
        ASSERT_NO_FATAL_FAILURE(UseScene());
        ASSERT_NO_FATAL_FAILURE(Start(kSceneDialogsAsset));

        SceneAddComponentRequestEvent::Emit(_scene, _parent);
        Settle();
        ASSERT_NE(Find("Cancel"), nullptr);
        PressKey(ImGuiKey_Escape);
        EXPECT_EQ(Find("Cancel"), nullptr);

        SceneAddComponentRequestEvent::Emit(_scene, _parent);
        Settle();
        EXPECT_NE(Find("Cancel"), nullptr);
    }

}  // namespace BECore::Tests
