#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ContentVersion.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/ConfigEditor/ConfigEditorPanelModel.h>
#include <Widgets/EditorLayout/EditorDocumentRows.h>
#include <Widgets/SceneEditor/SceneEditorDocuments.h>
#include <Widgets/SceneEditor/UINodeFileSession.h>

namespace BECore::Tests {

    namespace {

        const PoolString kSceneConfig = "SceneConfig"_intern;
        const PoolString kPanelConfig = "PanelConfig"_intern;
        constexpr StringView kPanelValueKey = "root@value";

        /// The files below carry `@VER@` where the content version the loader checks goes (WithVersion)
        constexpr const char* kSceneText = R"(<SceneManager contentVersion="@VER@"><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node ref="nodes/Panel"/>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr const char* kTwoInstancesText = R"(<SceneManager contentVersion="@VER@"><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node ref="nodes/Panel"/>
    <node ref="nodes/Panel" name="PanelB"/>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr const char* kPanelFile = R"(<node name="Panel" contentVersion="@VER@"><components>
  <component type="TransformComponent" x="10" y="20" width="200" height="100"/>
  <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
    <elements>
      <element type="UIElement" name="Title" x="8" y="8" width="184" height="30"/>
    </elements>
  </component>
</components></node>)";

        constexpr const char* kPanelConfigText = "<PanelConfig value=\"one\"/>\n";

        String WithVersion(StringView text) {
            String result(text);
            result.replace(result.find("@VER@"), 5, Format("{}", kContentVersion).c_str());
            return result;
        }

        const EditorDocumentState* Find(const Vector<EditorDocumentState>& rows, StringView id) {
            for (const EditorDocumentState& row : rows) {
                if (row.id == id) {
                    return &row;
                }
            }
            return nullptr;
        }

    }  // namespace

    /// The documents the panels report, over a content root and a content overlay in a temporary folder: nothing of the engine's content is read or written
    class EditorDocumentsProviderTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _base = TestTempDirectory("edoc");
            MakeEmptyDirectory(_base);
            _root = _base / "root";
            _overlay = _base / "overlay";
            WriteFile(_root / "config" / "nodes" / "Panel.xml", WithVersion(kPanelFile));
            WriteFile(_root / "config" / "PanelConfig.xml", kPanelConfigText);
            MakeDirectory(_root / "assets");
        }

        void TearDown() override {
            _scenes.Reset();
            RemoveDirectory(_base);
        }

        /// Starts the file system (with the overlay when asked), the manager and the scenes of @p sceneText
        void Start(StringView sceneText, bool withOverlay = true) {
            WriteFile(_root / "config" / "SceneConfig.xml", WithVersion(sceneText));
            _fileSystem.AddContentRoot(_root);
            if (withOverlay) {
                ASSERT_TRUE(_fileSystem.SetContentOverlay(_overlay));
            }
            _fileSystem.Initialize();
            _manager.Initialize();
            _scenes = LoadSceneManagerFromConfig(_manager, _fileSystem);
            ASSERT_TRUE(_scenes);
            _scenes->Initialize();
        }

        Scene& MainScene() {
            return Require(_scenes->GetSceneByName("Main"_intern));
        }

        IntrusivePtr<UIElement> Title() {
            UICanvasComponent& canvas = Require(MainScene().GetRootNode().GetChildren()[0]->GetComponent<UICanvasComponent>());
            return canvas._elements[0];
        }

        Vector<EditorDocumentState> SceneRows(bool dirty, const UINodeFileSession& session) {
            Vector<EditorDocumentState> rows;
            AppendSceneDocuments(rows, _manager, MainScene(), dirty, session);
            return rows;
        }

        Path _base;
        Path _root;
        Path _overlay;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        IntrusivePtr<SceneManager> _scenes;
    };

    TEST_F(EditorDocumentsProviderTest, ACleanSceneIsOneRowWhoseFileIsTheOverlaysBeforeAnySave) {
        ASSERT_NO_FATAL_FAILURE(Start(kSceneText));
        const UINodeFileSession session;

        const Vector<EditorDocumentState> rows = SceneRows(false, session);

        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].id, "scene:Main");
        EXPECT_EQ(rows[0].kind, EditorDocumentKind::Scene);
        EXPECT_FALSE(rows[0].dirty);
        EXPECT_EQ(rows[0].path, "config/SceneConfig.xml");
        EXPECT_EQ(Path(rows[0].file), _overlay / "config" / "SceneConfig.xml");
        EXPECT_FALSE(PathExists(_overlay / "config" / "SceneConfig.xml"));
    }

    TEST_F(EditorDocumentsProviderTest, WithoutAnOverlayTheFileIsTheOneTheConfigWasLoadedFrom) {
        ASSERT_NO_FATAL_FAILURE(Start(kSceneText, false));
        const UINodeFileSession session;

        const Vector<EditorDocumentState> rows = SceneRows(true, session);

        ASSERT_EQ(rows.size(), 1u);
        EXPECT_TRUE(rows[0].dirty);
        EXPECT_EQ(Path(rows[0].file), AbsoluteNormalPath(_root / "config" / "SceneConfig.xml"));
    }

    TEST_F(EditorDocumentsProviderTest, PendingUIEditsAddTheirNodeFileAndASaveTakesItAway) {
        ASSERT_NO_FATAL_FAILURE(Start(kSceneText));
        UINodeFileSession session;
        const IntrusivePtr<UIElement> title = Title();
        title->style.rect.x = UILength::Px(33.0f);
        session.Note(title);

        const Vector<EditorDocumentState> pending = SceneRows(false, session);

        ASSERT_EQ(pending.size(), 2u);
        const EditorDocumentState& nodeFile = Require(Find(pending, "nodeFile:nodes/Panel"));
        EXPECT_EQ(nodeFile.kind, EditorDocumentKind::NodeFile);
        EXPECT_TRUE(nodeFile.dirty);
        EXPECT_EQ(nodeFile.path, "config/nodes/Panel.xml");
        EXPECT_EQ(Path(nodeFile.file), _overlay / "config" / "nodes" / "Panel.xml");

        ASSERT_EQ(session.Save(MainScene(), _fileSystem).filesWritten, 1u);

        EXPECT_TRUE(PathExists(_overlay / "config" / "nodes" / "Panel.xml"));
        EXPECT_EQ(SceneRows(false, session).size(), 1u);
    }

    TEST_F(EditorDocumentsProviderTest, ANodeFileWithTwoInstancesIsNotListedBecauseASaveDoesNotWriteIt) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoInstancesText));
        UINodeFileSession session;
        session.Note(Title());

        EXPECT_EQ(SceneRows(false, session).size(), 1u);
    }

    TEST_F(EditorDocumentsProviderTest, ASceneSaveIntoTheOverlayKeepsPathAndFile) {
        ASSERT_NO_FATAL_FAILURE(Start(kSceneText));
        const UINodeFileSession session;
        const Vector<EditorDocumentState> before = SceneRows(true, session);

        ASSERT_TRUE(_manager.SaveConfig(kSceneConfig));
        const Vector<EditorDocumentState> after = SceneRows(false, session);

        ASSERT_EQ(before.size(), 1u);
        ASSERT_EQ(after.size(), 1u);
        EXPECT_TRUE(PathExists(_overlay / "config" / "SceneConfig.xml"));
        EXPECT_EQ(after[0].path, before[0].path);
        EXPECT_EQ(after[0].file, before[0].file);
    }

    TEST_F(EditorDocumentsProviderTest, AConfigThatIsNotLoadedHasNoPathAndNoFile) {
        ASSERT_NO_FATAL_FAILURE(Start(kSceneText));

        const EditorDocumentState row = MakeConfigDocument(_manager, EditorDocumentKind::Library, "sprites", true, "NoSuchConfig"_intern);

        EXPECT_EQ(row.id, "library:sprites");
        EXPECT_TRUE(row.dirty);
        EXPECT_TRUE(row.path.empty());
        EXPECT_TRUE(row.file.empty());
    }

    TEST_F(EditorDocumentsProviderTest, TheConfigEditorReportsTheSelectedConfigDirtyWhileEditsAreStaged) {
        ASSERT_NO_FATAL_FAILURE(Start(kSceneText));
        const IntrusivePtr<ConfigEditorPanelModel> model = New<ConfigEditorPanelModel>();
        model->Initialize();
        model->UseConfigManager(&_manager);
        Vector<EditorDocumentState> rows;
        model->AppendDocuments(rows);
        EXPECT_TRUE(rows.empty()) << "nothing is selected";

        model->SelectConfig(kPanelConfig);
        const XmlNode root = _manager.GetConfig(kPanelConfig);
        model->GetStagedChanges().Set(String(kPanelValueKey), root.GetPugiNode().attribute("value"), "two");
        model->AppendDocuments(rows);

        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].id, "config:PanelConfig");
        EXPECT_EQ(rows[0].kind, EditorDocumentKind::Config);
        EXPECT_TRUE(rows[0].dirty);
        EXPECT_EQ(rows[0].path, "config/PanelConfig.xml");
        EXPECT_EQ(Path(rows[0].file), _overlay / "config" / "PanelConfig.xml");

        model->SaveSelectedConfig();
        rows.clear();
        model->AppendDocuments(rows);

        ASSERT_EQ(rows.size(), 1u);
        EXPECT_FALSE(rows[0].dirty);
        EXPECT_EQ(Path(rows[0].file), _overlay / "config" / "PanelConfig.xml");
        EXPECT_NE(ReadFile(_overlay / "config" / "PanelConfig.xml").find("two"), String::npos);
        EXPECT_EQ(ReadFile(_root / "config" / "PanelConfig.xml"), String(kPanelConfigText));
    }

}  // namespace BECore::Tests
