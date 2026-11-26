#include <BECore/Audio/AudioSourceComponent.h>
#include <BECore/Config/XmlDocument.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Text/TextComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/SceneEditor/SceneAssetDrop.h>
#include <Widgets/SceneEditor/SceneConfigWriter.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kSceneXml = R"(<SceneManager><scenes><scene type="Scene" name="Main"><nodes/></scene></scenes></SceneManager>)";
        constexpr const char* kBoxFile = R"(<node name="Box"><components>
  <component type="TransformComponent" x="7" y="8" width="40" height="20"/>
</components></node>)";
        constexpr const char* kSoundOnlyFile = R"(<node name="Ambience"><components><component type="AudioSourceComponent"/></components></node>)";
        constexpr Vec2 kAt{300.0f, 200.0f};
        constexpr Vec2 kPicture{64.0f, 32.0f};

        SceneAssetDrop DropOf(AssetKind kind, PoolString path) {
            return SceneAssetDrop{kind, path, kAt, kind == AssetKind::Texture ? kPicture : Vec2{}};
        }

        void ExpectCentred(const SceneNode& node) {
            const IntrusivePtr<TransformComponent> transform = node.GetComponent<TransformComponent>();
            ASSERT_TRUE(transform);
            EXPECT_FLOAT_EQ(transform->_x + transform->_width * 0.5f, kAt.x);
            EXPECT_FLOAT_EQ(transform->_y + transform->_height * 0.5f, kAt.y);
        }

    }  // namespace

    /// An empty scene and a folder of node files that stands for the `config` mount
    class SceneAssetDropTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("scene_asset_drop");
            MakeEmptyDirectory(_dir);
            WriteFile(_dir / "nodes" / "Box.xml", kBoxFile);
            WriteFile(_dir / "nodes" / "Ambience.xml", kSoundOnlyFile);
            _loader = MakeDirectoryNodeLoader(_dir);
            ASSERT_TRUE(_document.LoadFromString(kSceneXml));
            _manager = LoadSceneManager(_document.GetRoot(), _loader);
            _manager->Initialize();
            _manager->SetActiveScene("Main"_intern);
        }

        void TearDown() override {
            _manager.Reset();
            _reloaded.Reset();
            RemoveDirectory(_dir);
        }

        Scene& Main(const IntrusivePtr<SceneManager>& manager) const {
            return *manager->GetSceneByName("Main"_intern);
        }

        std::expected<SceneNode*, String> Add(const SceneAssetDrop& drop) {
            return AddSceneNodeOfAsset(Main(_manager), drop, _loader);
        }

        size_t NodeCount() const {
            return Main(_manager).GetRootNode().GetChildren().size();
        }

        /// Writes the scene as the editor's Save does and loads the file the way a game does, node files included
        void SaveAndReload() {
            Vector<String> notes;
            ASSERT_TRUE(WriteSceneToConfig(_document.GetRoot().GetPugiNode(), Main(_manager), &_loader, &notes));
            EXPECT_TRUE(notes.empty());
            ASSERT_TRUE(_document.SaveToFile(_dir / "SceneConfig.xml"));
            ASSERT_TRUE(_saved.LoadFromFile(_dir / "SceneConfig.xml"));
            _reloaded = LoadSceneManager(_saved.GetRoot(), _loader);
            _reloaded->Initialize();
        }

        Path _dir;
        NodeFileLoader _loader;
        XmlDocument _document;
        XmlDocument _saved;
        IntrusivePtr<SceneManager> _manager;
        IntrusivePtr<SceneManager> _reloaded;
    };

    TEST_F(SceneAssetDropTest, ADroppedPictureBecomesASpriteOfItsSizeThatSurvivesASave) {
        const auto added = Add(DropOf(AssetKind::Texture, "assets/pieces/drop_probe_rook.png"_intern));
        ASSERT_TRUE(added) << added.error().c_str();
        ASSERT_NO_FATAL_FAILURE(ExpectCentred(**added));

        ASSERT_NO_FATAL_FAILURE(SaveAndReload());
        const auto& nodes = Main(_reloaded).GetRootNode().GetChildren();
        ASSERT_EQ(nodes.size(), 1u);
        EXPECT_EQ(nodes[0]->GetName(), "drop_probe_rook"_intern);
        const IntrusivePtr<TransformComponent> transform = nodes[0]->GetComponent<TransformComponent>();
        const IntrusivePtr<SpriteRendererComponent> sprite = nodes[0]->GetComponent<SpriteRendererComponent>();
        ASSERT_TRUE(transform);
        ASSERT_TRUE(sprite);
        EXPECT_FLOAT_EQ(transform->_width, kPicture.x);
        EXPECT_FLOAT_EQ(transform->_height, kPicture.y);
        EXPECT_EQ(sprite->_texturePath, "assets/pieces/drop_probe_rook.png"_intern);
        ASSERT_NO_FATAL_FAILURE(ExpectCentred(*nodes[0]));
    }

    TEST_F(SceneAssetDropTest, ASecondDropOfAFileGetsAFreeName) {
        const SceneAssetDrop drop = DropOf(AssetKind::Sound, "assets/drop_probe_click.wav"_intern);
        const auto first = Add(drop);
        const auto second = Add(drop);
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);
        EXPECT_NE((*first)->GetName(), (*second)->GetName());
        EXPECT_EQ(NodeCount(), 2u);
    }

    TEST_F(SceneAssetDropTest, ADroppedFontShowsItsNameAndADroppedSoundPlaysItsClip) {
        const auto font = Add(DropOf(AssetKind::Font, "assets/fonts/drop_probe_serif.ttf"_intern));
        const auto sound = Add(DropOf(AssetKind::Sound, "assets/drop_probe_click.wav"_intern));
        ASSERT_TRUE(font) << font.error().c_str();
        ASSERT_TRUE(sound) << sound.error().c_str();

        ASSERT_NO_FATAL_FAILURE(ExpectCentred(**font));
        const IntrusivePtr<TextComponent> text = (*font)->GetComponent<TextComponent>();
        ASSERT_TRUE(text);
        EXPECT_EQ(text->_text, "drop_probe_serif");
        EXPECT_EQ(text->_fontPath, "assets/fonts/drop_probe_serif.ttf"_intern);
        const IntrusivePtr<AudioSourceComponent> audio = (*sound)->GetComponent<AudioSourceComponent>();
        ASSERT_TRUE(audio);
        EXPECT_EQ(audio->_clip, "assets/drop_probe_click.wav"_intern);
    }

    TEST_F(SceneAssetDropTest, ADroppedNodeFileIsSavedAsAReferenceCentredOnThePointer) {
        const auto added = Add(DropOf(AssetKind::Node, "config/nodes/Box.xml"_intern));
        ASSERT_TRUE(added) << added.error().c_str();
        EXPECT_EQ((*added)->GetSourceRef(), "nodes/Box"_intern);
        ASSERT_NO_FATAL_FAILURE(ExpectCentred(**added));

        ASSERT_NO_FATAL_FAILURE(SaveAndReload());
        const pugi::xml_node saved = _saved.GetRoot().GetPugiNode().child("scenes").child("scene").child("nodes").child("node");
        EXPECT_STREQ(saved.attribute("ref").as_string(), "nodes/Box");
        const auto& nodes = Main(_reloaded).GetRootNode().GetChildren();
        ASSERT_EQ(nodes.size(), 1u);
        EXPECT_EQ(nodes[0]->GetName(), "Box"_intern);
        ASSERT_NO_FATAL_FAILURE(ExpectCentred(*nodes[0]));
    }

    TEST_F(SceneAssetDropTest, ANodeFileWithNoTransformIsAddedAsItIs) {
        const auto added = Add(DropOf(AssetKind::Node, "config/nodes/Ambience.xml"_intern));
        ASSERT_TRUE(added) << added.error().c_str();
        EXPECT_FALSE((*added)->IsGenerated());
        EXPECT_FALSE((*added)->GetComponent<TransformComponent>());
    }

    TEST_F(SceneAssetDropTest, ADropThatCannotBeANodeAddsNothingAndSaysWhy) {
        const SceneAssetDrop refused[] = {
            DropOf(AssetKind::Node, "config/nodes/Missing.xml"_intern), DropOf(AssetKind::Node, "config/nodes/not a ref.xml"_intern),
            DropOf(AssetKind::Node, "assets/nodes/Box.xml"_intern),     SceneAssetDrop{AssetKind::Texture, "assets/drop_probe_broken.png"_intern, kAt, Vec2{}},
            DropOf(AssetKind::Scene, "config/SceneConfig.xml"_intern),  DropOf(AssetKind::UiDocument, "assets/ui/drop_probe.ui.xml"_intern),
        };
        for (const SceneAssetDrop& drop : refused) {
            const auto added = Add(drop);
            ASSERT_FALSE(added) << drop.path.CStr();
            EXPECT_FALSE(added.error().empty()) << drop.path.CStr();
        }
        EXPECT_EQ(NodeCount(), 0u);
    }

}  // namespace BECore::Tests
