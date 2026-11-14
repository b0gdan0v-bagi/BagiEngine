#include <BECore/Config/XmlDocument.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/TempPath.h>
#include <Widgets/SceneEditor/SceneConfigWriter.h>

namespace BECore::Tests {

    namespace {

        constexpr float kSavedRotation = 37.5f;
        constexpr float kSavedPivotX = 0.25f;
        constexpr float kSavedPivotY = 0.75f;

        // Three scenes in the order Menu, Game, Credits. Menu has no view size, Game has two nodes (one on layer -1) and a
        // TransformComponent whose width is 0 (not the default 1), Credits has a node of its own.
        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Menu"><nodes>
    <node name="Title"><components>
      <component type="TransformComponent" x="10" y="20" width="300" height="50"/>
      <component type="QuadRendererComponent"><color r="10" g="20" b="30" a="255"/></component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Game" viewWidth="840" viewHeight="800"><nodes>
    <node name="Board" layer="-1"><components>
      <component type="TransformComponent" x="100" y="80" width="0" height="640"/>
      <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="255"/></component>
    </components></node>
    <node name="Piece"><components>
      <component type="TransformComponent" x="5" y="6" width="7" height="8"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Credits"><nodes>
    <node name="Names"><components>
      <component type="TransformComponent" x="1" y="2" width="3" height="4"/>
      <component type="QuadRendererComponent"><color r="200" g="100" b="50" a="128"/></component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr const char* kGameComment = " The game itself ";
        constexpr const char* kCreditsComment = " Credits come last ";

        constexpr float kMovedX = 123.5f;
        constexpr int32_t kPieceLayer = 2;
        constexpr float kViewWidth = 840.0f;
        constexpr float kViewHeight = 800.0f;

        /// The `<scene>` of @p configRoot whose `name` is @p name, or an invalid node
        pugi::xml_node FindSceneElement(pugi::xml_node configRoot, const char* name) {
            for (pugi::xml_node el = configRoot.child("scenes").child("scene"); el; el = el.next_sibling("scene")) {
                if (StringView{el.attribute("name").as_string()} == name) {
                    return el;
                }
            }
            return {};
        }

        /// The `name` of every `<scene>` of @p configRoot in file order, joined with commas ("Menu,Game,Credits")
        String SceneOrder(pugi::xml_node configRoot) {
            String order;
            for (pugi::xml_node el = configRoot.child("scenes").child("scene"); el; el = el.next_sibling("scene")) {
                if (!order.empty()) {
                    order.append(",");
                }
                order.append(el.attribute("name").as_string());
            }
            return order;
        }

        size_t CountScenes(pugi::xml_node configRoot) {
            size_t count = 0;
            for (pugi::xml_node el = configRoot.child("scenes").child("scene"); el; el = el.next_sibling("scene")) {
                ++count;
            }
            return count;
        }

        String Print(pugi::xml_node node) {
            std::ostringstream stream;
            node.print(stream, "  ");
            const std::string text = stream.str();
            return String{text.c_str(), text.size()};
        }

        /// True when @p node or something below it has an attribute called @p name
        bool HasAttributeBelow(pugi::xml_node node, const char* name) {
            if (node.attribute(name)) {
                return true;
            }
            for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
                if (HasAttributeBelow(child, name)) {
                    return true;
                }
            }
            return false;
        }

        constexpr const char* kBoxFile = R"(<node name="Box" layer="2"><components>
  <component type="TransformComponent" x="1" y="2" width="30" height="40"/>
  <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="255"/></component>
</components></node>)";

        constexpr const char* kPanelFile = R"(<node name="Panel"><components>
  <component type="TransformComponent" x="10" y="20" width="200" height="100"/>
  <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
    <elements>
      <element type="UIElement" name="Title" x="8" y="8" width="184" height="30" fontSize="24">
        <components>
          <component type="UITextComponent" text="Base"/>
        </components>
      </element>
    </elements>
  </component>
</components></node>)";

        // Scene Main: an instance, an inline node, an instance with overrides (written the way BuildNodeReference writes them), an instance of a second file.
        // Scene Other holds one instance, saved by nobody.
        constexpr const char* kBoxOverrideRef = R"(<node ref="nodes/Box" name="BoxB" layer="3"><components><component type="TransformComponent" x="100"/></components></node>)";
        constexpr const char* kSplitScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node ref="nodes/Box"/>
    <node name="Plain"><components>
      <component type="TransformComponent" x="5" y="6" width="7" height="8"/>
    </components></node>
    <node ref="nodes/Box" name="BoxB" layer="3"><components><component type="TransformComponent" x="100"/></components></node>
    <node ref="nodes/Panel"/>
  </nodes></scene>
  <scene type="Scene" name="Other"><nodes>
    <node ref="nodes/Box" name="OtherBox"/>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr float kEditedX = 77.0f;
        constexpr int32_t kEditedLayer = 5;

        void WriteFile(const std::filesystem::path& path, const char* text) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path) << text;
        }

        /// The element in the compact form, for comparing a node with the text it should have
        String Raw(pugi::xml_node node) {
            std::ostringstream stream;
            node.print(stream, "", pugi::format_raw);
            const std::string text = stream.str();
            return String{text.c_str(), text.size()};
        }

        /// The components of @p node as text: type and serialized fields, in order
        eastl::vector<String> ComponentTexts(const SceneNode& node) {
            eastl::vector<String> texts;
            for (const auto& component : node.GetComponents()) {
                XmlSerializer serializer;
                component->Serialize(serializer);
                String text = component->GetTypeMeta().typeName.data();
                text.append(":");
                text.append(Raw(serializer.GetRootNode()));
                texts.push_back(text);
            }
            return texts;
        }

        /// The i-th `<node>` of the `<nodes>` of the `<scene>` called @p sceneName
        pugi::xml_node SavedNode(pugi::xml_node configRoot, const char* sceneName, size_t index) {
            pugi::xml_node node = FindSceneElement(configRoot, sceneName).child("nodes").first_child();
            for (size_t i = 0; i < index && node; ++i) {
                node = node.next_sibling();
            }
            return node;
        }

        constexpr const char* kNestedScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Left"><children>
      <node name="Inner" layer="2"><components>
        <component type="TransformComponent" x="123.5" y="6" width="7" height="8"/>
      </components><children>
        <node name="Deep"/>
      </children></node>
      <node name="Last"/>
    </children></node>
    <node name="Right"/>
  </nodes></scene>
</scenes></SceneManager>)";

        /// The names of the tree below @p node, depth first, each followed by one dot per level ("Left Inner. Deep.. Last. Right")
        void AppendTree(const SceneNode& node, size_t depth, String& out) {
            for (const auto& child : node.GetChildren()) {
                if (!out.empty()) {
                    out.append(" ");
                }
                out.append(child->GetName().CStr());
                out.append(depth, '.');
                AppendTree(*child, depth + 1, out);
            }
        }

        String TreeOf(const SceneNode& root) {
            String out;
            AppendTree(root, 0, out);
            return out;
        }

        const SceneNode* FindNodeByName(const SceneNode& from, PoolString name) {
            for (const auto& child : from.GetChildren()) {
                if (child->GetName() == name) {
                    return child.Get();
                }
                if (const SceneNode* found = FindNodeByName(*child, name)) {
                    return found;
                }
            }
            return nullptr;
        }

        SceneNode* FindTopLevelNode(Scene& scene, PoolString name) {
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                if (node->GetName() == name) {
                    return node.Get();
                }
            }
            return nullptr;
        }

    }  // namespace

    /**
     * The save path of the Scene Editor without ImGui: WriteSceneToConfig writes the active scene into a SceneConfig
     * document, XmlDocument::SaveToFile writes the file and a second SceneManager loads it back. The test owns the document
     * (loaded from a string) and its file in a temporary directory. ConfigManager::SaveConfig is not used: it would overwrite
     * the SceneConfig.xml of the repository.
     */
    class SceneEditorSaveTest : public ::testing::Test {
    protected:
        void SetUp() override {
            // One directory per test: ctest runs the tests in parallel processes, and SetUp clears the directory
            _dir = TestTempDirectory("BagiEngineSceneEditorSaveTest");
            std::filesystem::remove_all(_dir);
            std::filesystem::create_directories(_dir);
        }

        void TearDown() override {
            // Destroying the scenes drops the components' event connections
            _manager.Reset();
            _reloaded.Reset();
            std::error_code ignored;
            std::filesystem::remove_all(_dir, ignored);
        }

        std::filesystem::path SavedPath() const {
            return _dir / "SceneConfig.xml";
        }

        /// Loads the document and builds the scenes from the same text, the way the game does from SceneConfig. The XML
        /// loader drops comments, so the ones a hand-written file carries are put into the document afterwards, before
        /// Game and before Credits. Use through ASSERT_NO_FATAL_FAILURE.
        void LoadScenes() {
            ASSERT_TRUE(_document.LoadFromString(kScenesXml));

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();

            pugi::xml_node scenes = ConfigRoot().child("scenes");
            scenes.insert_child_before(pugi::node_comment, FindSceneElement(ConfigRoot(), "Game")).set_value(kGameComment);
            scenes.insert_child_before(pugi::node_comment, FindSceneElement(ConfigRoot(), "Credits")).set_value(kCreditsComment);
        }

        /// The document element of the config, `<SceneManager>`: what ConfigManager::GetConfig("SceneConfig") hands to the editor
        pugi::xml_node ConfigRoot() const {
            return _document.GetRoot().GetPugiNode();
        }

        Scene& GetScene(const char* name) const {
            return *_manager->GetSceneByName(PoolString::Intern(name));
        }

        /// Saves the document to the temporary file and loads the file into a new SceneManager (_reloaded). Use through ASSERT_NO_FATAL_FAILURE.
        void SaveAndReload() {
            ASSERT_TRUE(_document.SaveToFile(SavedPath()));

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromFile(SavedPath()));
            _reloaded = New<SceneManager>();
            _reloaded->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _reloaded->Initialize();
        }

        /// Like LoadScenes() for a document of the test's own (no comments are put in). Use through ASSERT_NO_FATAL_FAILURE.
        void LoadOwnScenes(const char* xml) {
            ASSERT_TRUE(_document.LoadFromString(xml));

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(xml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        /// A scene of the reloaded manager, which the test has checked is there
        Scene& Reloaded(const char* name) const {
            return *_reloaded->GetSceneByName(PoolString::Intern(name));
        }

        XmlDocument _document;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtrAtomic<SceneManager> _reloaded;
        std::filesystem::path _dir;
    };

    TEST_F(SceneEditorSaveTest, OnlyTheActiveSceneIsReplaced) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        _manager->SetActiveScene("Game"_intern);
        Scene& game = *_manager->GetActiveScene();
        ASSERT_EQ(game.GetName(), "Game"_intern);

        // The edits of a user in the editor: rename a node, change its layer, change a field, add a node with a component
        SceneNode* piece = FindTopLevelNode(game, "Piece"_intern);
        ASSERT_NE(piece, nullptr);
        piece->SetName("Pawn"_intern);
        piece->SetLayer(kPieceLayer);
        SceneNode* board = FindTopLevelNode(game, "Board"_intern);
        ASSERT_NE(board, nullptr);
        auto boardTransform = board->GetComponent<TransformComponent>();
        ASSERT_TRUE(boardTransform);
        boardTransform->_x = kMovedX;
        auto banner = game.GetRootNode().AddChild("Banner"_intern);
        auto bannerQuad = New<QuadRendererComponent>();
        bannerQuad->_color = Color{9, 8, 7, 6};
        banner->AddComponent(bannerQuad);

        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), game));
        ASSERT_NO_FATAL_FAILURE(SaveAndReload());

        EXPECT_EQ(SceneOrder(ConfigRoot()), "Menu,Game,Credits");
        ASSERT_NE(_reloaded->GetSceneByName("Menu"_intern), nullptr);
        ASSERT_NE(_reloaded->GetSceneByName("Game"_intern), nullptr);
        ASSERT_NE(_reloaded->GetSceneByName("Credits"_intern), nullptr);

        // Game has the edits, the untouched node as it was, the width 0 and the layer -1 among them
        const auto& gameNodes = Reloaded("Game").GetRootNode().GetChildren();
        ASSERT_EQ(gameNodes.size(), 3u);
        EXPECT_EQ(gameNodes[0]->GetName(), "Board"_intern);
        EXPECT_EQ(gameNodes[0]->GetLayer(), -1);
        auto savedBoard = gameNodes[0]->GetComponent<TransformComponent>();
        ASSERT_TRUE(savedBoard);
        EXPECT_FLOAT_EQ(savedBoard->_x, kMovedX);
        EXPECT_FLOAT_EQ(savedBoard->_y, 80.0f);
        EXPECT_FLOAT_EQ(savedBoard->_width, 0.0f);
        EXPECT_FLOAT_EQ(savedBoard->_height, 640.0f);
        auto savedBoardQuad = gameNodes[0]->GetComponent<QuadRendererComponent>();
        ASSERT_TRUE(savedBoardQuad);
        EXPECT_EQ(savedBoardQuad->_color.g, 2);
        EXPECT_EQ(gameNodes[1]->GetName(), "Pawn"_intern);
        EXPECT_EQ(gameNodes[1]->GetLayer(), kPieceLayer);
        EXPECT_EQ(gameNodes[2]->GetName(), "Banner"_intern);
        EXPECT_EQ(gameNodes[2]->GetLayer(), 0);
        auto savedBannerQuad = gameNodes[2]->GetComponent<QuadRendererComponent>();
        ASSERT_TRUE(savedBannerQuad);
        EXPECT_EQ(savedBannerQuad->_color.r, 9);
        EXPECT_EQ(savedBannerQuad->_color.a, 6);

        // Menu and Credits have the nodes and values they had
        const auto& menuNodes = Reloaded("Menu").GetRootNode().GetChildren();
        ASSERT_EQ(menuNodes.size(), 1u);
        EXPECT_EQ(menuNodes[0]->GetName(), "Title"_intern);
        auto title = menuNodes[0]->GetComponent<TransformComponent>();
        ASSERT_TRUE(title);
        EXPECT_FLOAT_EQ(title->_x, 10.0f);
        EXPECT_FLOAT_EQ(title->_width, 300.0f);

        const auto& creditsNodes = Reloaded("Credits").GetRootNode().GetChildren();
        ASSERT_EQ(creditsNodes.size(), 1u);
        EXPECT_EQ(creditsNodes[0]->GetName(), "Names"_intern);
        auto names = creditsNodes[0]->GetComponent<TransformComponent>();
        ASSERT_TRUE(names);
        EXPECT_FLOAT_EQ(names->_height, 4.0f);
        auto namesQuad = creditsNodes[0]->GetComponent<QuadRendererComponent>();
        ASSERT_TRUE(namesQuad);
        EXPECT_EQ(namesQuad->_color.r, 200);
        EXPECT_EQ(namesQuad->_color.a, 128);
    }

    TEST_F(SceneEditorSaveTest, TheRotationTheBlendModeAndTheFlipsSurviveASave) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        _manager->SetActiveScene("Game"_intern);
        Scene& game = *_manager->GetActiveScene();
        SceneNode* board = FindTopLevelNode(game, "Board"_intern);
        ASSERT_NE(board, nullptr);
        auto transform = board->GetComponent<TransformComponent>();
        ASSERT_TRUE(transform);
        transform->_rotation = kSavedRotation;
        transform->_pivotX = kSavedPivotX;
        transform->_pivotY = kSavedPivotY;
        board->GetComponent<QuadRendererComponent>()->_blend = BlendMode::Add;
        auto sprite = New<SpriteRendererComponent>();
        sprite->_blend = BlendMode::Multiply;
        sprite->_flipX = true;
        sprite->_flipY = true;
        board->AddComponent(sprite);

        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), game));
        ASSERT_NO_FATAL_FAILURE(SaveAndReload());

        SceneNode* saved = FindTopLevelNode(Reloaded("Game"), "Board"_intern);
        ASSERT_NE(saved, nullptr);
        auto savedTransform = saved->GetComponent<TransformComponent>();
        ASSERT_TRUE(savedTransform);
        EXPECT_FLOAT_EQ(savedTransform->_rotation, kSavedRotation);
        EXPECT_FLOAT_EQ(savedTransform->_pivotX, kSavedPivotX);
        EXPECT_FLOAT_EQ(savedTransform->_pivotY, kSavedPivotY);
        EXPECT_EQ(saved->GetComponent<QuadRendererComponent>()->_blend, BlendMode::Add);
        auto savedSprite = saved->GetComponent<SpriteRendererComponent>();
        ASSERT_TRUE(savedSprite);
        EXPECT_EQ(savedSprite->_blend, BlendMode::Multiply);
        EXPECT_TRUE(savedSprite->_flipX);
        EXPECT_TRUE(savedSprite->_flipY);

        // A node that did not set them keeps the defaults that mean no effect
        auto title = Reloaded("Menu").GetRootNode().GetChildren()[0]->GetComponent<TransformComponent>();
        ASSERT_TRUE(title);
        EXPECT_FLOAT_EQ(title->_rotation, 0.0f);
        EXPECT_FLOAT_EQ(title->_pivotX, 0.5f);
        EXPECT_FLOAT_EQ(title->_pivotY, 0.5f);
    }

    TEST_F(SceneEditorSaveTest, TheOtherScenesAreKeptVerbatim) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        const String menuBefore = Print(FindSceneElement(ConfigRoot(), "Menu"));
        const String creditsBefore = Print(FindSceneElement(ConfigRoot(), "Credits"));
        ASSERT_FALSE(menuBefore.empty());
        ASSERT_FALSE(creditsBefore.empty());

        _manager->SetActiveScene("Game"_intern);
        FindTopLevelNode(*_manager->GetActiveScene(), "Piece"_intern)->SetName("Pawn"_intern);
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), *_manager->GetActiveScene()));

        EXPECT_EQ(Print(FindSceneElement(ConfigRoot(), "Menu")), menuBefore);
        const pugi::xml_node credits = FindSceneElement(ConfigRoot(), "Credits");
        EXPECT_EQ(Print(credits), creditsBefore);

        // The comments sit where they were: before Game and before Credits
        const pugi::xml_node creditsComment = credits.previous_sibling();
        ASSERT_EQ(creditsComment.type(), pugi::node_comment);
        EXPECT_STREQ(creditsComment.value(), kCreditsComment);
        const pugi::xml_node game = FindSceneElement(ConfigRoot(), "Game");
        ASSERT_EQ(game.previous_sibling().type(), pugi::node_comment);
        EXPECT_STREQ(game.previous_sibling().value(), kGameComment);
        EXPECT_TRUE(game.next_sibling() == creditsComment);
    }

    TEST_F(SceneEditorSaveTest, TheSceneKeepsItsPlace) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());

        // The middle scene, then the first, then the last one
        for (const char* name : {"Game", "Menu", "Credits"}) {
            SCOPED_TRACE(name);
            _manager->SetActiveScene(PoolString::Intern(name));
            ASSERT_EQ(_manager->GetActiveScene(), &GetScene(name));
            ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), *_manager->GetActiveScene()));
            ASSERT_NO_FATAL_FAILURE(SaveAndReload());

            EXPECT_EQ(SceneOrder(ConfigRoot()), "Menu,Game,Credits");
            ASSERT_NE(_reloaded->GetSceneByName(PoolString::Intern(name)), nullptr);
        }

        // The first scene of the saved file is the one that is active after a load
        EXPECT_EQ(_reloaded->GetActiveScene(), &Reloaded("Menu"));
    }

    TEST_F(SceneEditorSaveTest, ASceneWithoutAnElementIsAppended) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        _manager->SetActiveScene("Game"_intern);

        pugi::xml_node scenes = ConfigRoot().child("scenes");
        ASSERT_TRUE(scenes.remove_child(FindSceneElement(ConfigRoot(), "Game")));
        ASSERT_EQ(SceneOrder(ConfigRoot()), "Menu,Credits");
        const String menuBefore = Print(FindSceneElement(ConfigRoot(), "Menu"));
        const String creditsBefore = Print(FindSceneElement(ConfigRoot(), "Credits"));

        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), *_manager->GetActiveScene()));

        EXPECT_EQ(SceneOrder(ConfigRoot()), "Menu,Credits,Game");
        EXPECT_EQ(Print(FindSceneElement(ConfigRoot(), "Menu")), menuBefore);
        EXPECT_EQ(Print(FindSceneElement(ConfigRoot(), "Credits")), creditsBefore);
        EXPECT_EQ(StringView{scenes.last_child().attribute("name").as_string()}, "Game");

        ASSERT_NO_FATAL_FAILURE(SaveAndReload());
        EXPECT_EQ(Reloaded("Game").GetRootNode().GetChildren().size(), 2u);
    }

    TEST_F(SceneEditorSaveTest, AMissingScenesElementIsCreated) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        _manager->SetActiveScene("Game"_intern);

        ASSERT_TRUE(ConfigRoot().remove_child("scenes"));
        ASSERT_FALSE(ConfigRoot().child("scenes"));

        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), *_manager->GetActiveScene()));

        EXPECT_TRUE(ConfigRoot().child("scenes"));
        EXPECT_EQ(SceneOrder(ConfigRoot()), "Game");
        EXPECT_EQ(CountScenes(ConfigRoot()), 1u);

        ASSERT_NO_FATAL_FAILURE(SaveAndReload());
        EXPECT_EQ(Reloaded("Game").GetRootNode().GetChildren().size(), 2u);
    }

    TEST_F(SceneEditorSaveTest, SceneAttributesComeFromSerialize) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        _manager->SetActiveScene("Game"_intern);
        Scene& game = *_manager->GetActiveScene();
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), game));

        // Every attribute the reflection writes for the scene is on the saved element with the same value. No field is
        // named here, so a new reflected field of Scene is covered by this case as well.
        XmlSerializer serialized;
        game.Serialize(serialized);
        const pugi::xml_node expected = serialized.GetRootNode();
        ASSERT_TRUE(expected.first_attribute());

        const pugi::xml_node saved = FindSceneElement(ConfigRoot(), "Game");
        ASSERT_TRUE(saved);
        for (pugi::xml_attribute attr = expected.first_attribute(); attr; attr = attr.next_attribute()) {
            SCOPED_TRACE(attr.name());
            ASSERT_TRUE(saved.attribute(attr.name()));
            EXPECT_STREQ(saved.attribute(attr.name()).value(), attr.value());
        }

        // The type is the class name of the scene, not a literal
        EXPECT_EQ(StringView{saved.attribute("type").as_string()}, game.GetTypeMeta().typeName);

        // A scene with a view keeps it, one without a view comes back without one
        ASSERT_NO_FATAL_FAILURE(SaveAndReload());
        EXPECT_FLOAT_EQ(Reloaded("Game").GetViewWidth(), kViewWidth);
        EXPECT_FLOAT_EQ(Reloaded("Game").GetViewHeight(), kViewHeight);

        _manager->SetActiveScene("Menu"_intern);
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), *_manager->GetActiveScene()));
        ASSERT_NO_FATAL_FAILURE(SaveAndReload());
        EXPECT_FLOAT_EQ(Reloaded("Menu").GetViewWidth(), 0.0f);
        EXPECT_FLOAT_EQ(Reloaded("Menu").GetViewHeight(), 0.0f);
        EXPECT_FLOAT_EQ(Reloaded("Game").GetViewWidth(), kViewWidth);
    }

    TEST_F(SceneEditorSaveTest, ViewVariantsRoundTripAndASceneWithoutThemSavesNone) {
        constexpr const char* xml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Responsive" viewWidth="1280" viewHeight="720">
    <viewVariants>
      <view name="Portrait" when="orientation:portrait" width="720" height="1280"/>
      <view when="minAspect:2;maxHeight:600" width="1600" height="500"/>
    </viewVariants>
    <nodes><node name="Piece"><components><component type="TransformComponent" x="5" y="6" width="7" height="8"/></components></node></nodes>
  </scene>
  <scene type="Scene" name="Plain" viewWidth="400" viewHeight="300"><nodes>
    <node name="Piece"><components><component type="TransformComponent" x="1" y="2" width="3" height="4"/></components></node>
  </nodes></scene>
</scenes></SceneManager>)";
        ASSERT_NO_FATAL_FAILURE(LoadOwnScenes(xml));
        _manager->SetActiveScene("Responsive"_intern);
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), *_manager->GetActiveScene()));

        const pugi::xml_node list = FindSceneElement(ConfigRoot(), "Responsive").child("viewVariants");
        ASSERT_TRUE(list);
        const pugi::xml_node portrait = list.first_child();
        EXPECT_STREQ(portrait.name(), "view");
        EXPECT_STREQ(portrait.attribute("name").as_string(), "Portrait");
        EXPECT_STREQ(portrait.attribute("when").as_string(), "orientation:portrait");
        EXPECT_FLOAT_EQ(portrait.attribute("width").as_float(), 720.0f);
        EXPECT_FLOAT_EQ(portrait.attribute("height").as_float(), 1280.0f);
        const pugi::xml_node wide = portrait.next_sibling();
        ASSERT_TRUE(wide);
        EXPECT_FALSE(wide.attribute("name"));
        EXPECT_STREQ(wide.attribute("when").as_string(), "minAspect:2;maxHeight:600");

        // The plain scene saves no list, not even an empty one
        _manager->SetActiveScene("Plain"_intern);
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), *_manager->GetActiveScene()));
        EXPECT_FALSE(FindSceneElement(ConfigRoot(), "Plain").child("viewVariants"));

        // Saving again gives the same document, and the file loads back with the same views
        const String once = Print(ConfigRoot());
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), GetScene("Responsive")));
        EXPECT_EQ(Print(ConfigRoot()), once);

        ASSERT_NO_FATAL_FAILURE(SaveAndReload());
        Scene& reloaded = Reloaded("Responsive");
        EXPECT_FLOAT_EQ(reloaded.GetViewWidth(), 1280.0f);
        auto renderer = BECore::New<RecordingRenderer>();
        CoreManager::GetRendererManager().SetRenderer(renderer);
        renderer->width = 600.0f;
        renderer->height = 900.0f;
        reloaded.Draw();
        EXPECT_EQ(reloaded.GetActiveView().name, "Portrait"_intern);
        EXPECT_FLOAT_EQ(reloaded.GetViewHeight(), 1280.0f);
        renderer->width = 1600.0f;
        renderer->height = 500.0f;
        reloaded.Draw();
        EXPECT_EQ(reloaded.GetActiveView().index, 1);
        EXPECT_FLOAT_EQ(reloaded.GetViewWidth(), 1600.0f);
        CoreManager::GetRendererManager().SetRenderer({});
        EXPECT_EQ(Reloaded("Plain").GetActiveView().index, -1);
    }

    TEST_F(SceneEditorSaveTest, TheNodesKeepTheirShortShape) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        _manager->SetActiveScene("Game"_intern);
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), *_manager->GetActiveScene()));

        // The nodes are not written through Serialize: that would add `enabled`, `type` and a `layer` to every node
        const pugi::xml_node saved = FindSceneElement(ConfigRoot(), "Game");
        ASSERT_TRUE(saved.child("nodes"));
        EXPECT_FALSE(HasAttributeBelow(saved.child("nodes"), "enabled"));

        const pugi::xml_node board = saved.child("nodes").first_child();
        ASSERT_TRUE(board);
        EXPECT_STREQ(board.attribute("name").as_string(), "Board");
        EXPECT_EQ(board.attribute("layer").as_int(), -1);
        EXPECT_FALSE(board.attribute("type"));
        const pugi::xml_node piece = board.next_sibling();
        ASSERT_TRUE(piece);
        EXPECT_FALSE(piece.attribute("layer"));
        EXPECT_STREQ(board.child("components").first_child().attribute("type").as_string(), "TransformComponent");
    }

    TEST_F(SceneEditorSaveTest, SavingTwiceGivesTheSameDocument) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        _manager->SetActiveScene("Game"_intern);
        Scene& game = *_manager->GetActiveScene();

        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), game));
        const String once = Print(ConfigRoot());
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), game));

        EXPECT_EQ(Print(ConfigRoot()), once);
        EXPECT_EQ(CountScenes(ConfigRoot()), 3u);
    }

    /**
     * A scene made of node files: the nodes of `nodes/Box` and `nodes/Panel` in a temporary folder, the scenes loaded with LoadSceneManager (so
     * the nodes remember their file) and the saved document loaded the same way.
     */
    class SceneEditorSaveReferencesTest : public SceneEditorSaveTest {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            SceneEditorSaveTest::SetUp();
            WriteFile(_dir / "nodes" / "Box.xml", kBoxFile);
            WriteFile(_dir / "nodes" / "Panel.xml", kPanelFile);
            _loader = MakeDirectoryNodeLoader(_dir);
        }

        /// Loads the document and the scenes (through the node files); the active scene is Main. Use through ASSERT_NO_FATAL_FAILURE.
        void LoadSplitScenes() {
            ASSERT_TRUE(_document.LoadFromString(kSplitScenesXml));
            _manager = LoadSceneManager(_document.GetRoot(), _loader);
            _manager->Initialize();
            ASSERT_NE(_manager->GetSceneByName("Main"_intern), nullptr);
            ASSERT_NE(_manager->GetSceneByName("Other"_intern), nullptr);
            _manager->SetActiveScene("Main"_intern);
        }

        /// Saves the document and loads the file the way a game does, node files included. Use through ASSERT_NO_FATAL_FAILURE.
        void SaveAndReloadSplit() {
            ASSERT_TRUE(_document.SaveToFile(SavedPath()));
            ASSERT_TRUE(_reloadedDocument.LoadFromFile(SavedPath()));
            _reloaded = LoadSceneManager(_reloadedDocument.GetRoot(), _loader);
            _reloaded->Initialize();
            ASSERT_NE(_reloaded->GetSceneByName("Main"_intern), nullptr);
        }

        bool Save(Scene& scene) {
            return WriteSceneToConfig(ConfigRoot(), scene, &_loader, &_notes);
        }

        /// The live scene and the one loaded from the saved file hold the same nodes: order, name, layer, node file, components
        void ExpectReloadedEqualsLive(const char* sceneName) {
            const auto& live = GetScene(sceneName).GetRootNode().GetChildren();
            const auto& loaded = Reloaded(sceneName).GetRootNode().GetChildren();
            ASSERT_EQ(live.size(), loaded.size());
            for (size_t i = 0; i < live.size(); ++i) {
                SCOPED_TRACE(live[i]->GetName().CStr());
                EXPECT_EQ(live[i]->GetName(), loaded[i]->GetName());
                EXPECT_EQ(live[i]->GetLayer(), loaded[i]->GetLayer());
                EXPECT_EQ(live[i]->GetSourceRef(), loaded[i]->GetSourceRef());
                EXPECT_EQ(ComponentTexts(*live[i]), ComponentTexts(*loaded[i]));
            }
        }

        SceneNode& Child(const char* sceneName, size_t index) {
            return *GetScene(sceneName).GetRootNode().GetChildren()[index];
        }

        NodeFileLoader _loader;
        XmlDocument _reloadedDocument;
        eastl::vector<String> _notes;
    };

    TEST_F(SceneEditorSaveReferencesTest, AnUntouchedSplitSceneKeepsItsReferences) {
        ASSERT_NO_FATAL_FAILURE(LoadSplitScenes());
        constexpr size_t kReferenceIndices[] = {0, 2, 3};
        eastl::vector<String> before;
        for (const size_t index : kReferenceIndices) {
            before.push_back(Raw(SavedNode(ConfigRoot(), "Main", index)));
        }

        ASSERT_TRUE(Save(GetScene("Main")));

        // Every reference has the text it had, a bare one stays bare, and the file does not learn the node file of a node
        for (size_t i = 0; i < before.size(); ++i) {
            EXPECT_EQ(Raw(SavedNode(ConfigRoot(), "Main", kReferenceIndices[i])), before[i]);
        }
        EXPECT_FALSE(HasAttributeBelow(ConfigRoot(), "sourceRef"));
        EXPECT_TRUE(_notes.empty());
        EXPECT_EQ(Raw(SavedNode(ConfigRoot(), "Main", 0)), "<node ref=\"nodes/Box\"/>");
        EXPECT_EQ(Raw(SavedNode(ConfigRoot(), "Main", 2)), kBoxOverrideRef);

        ASSERT_NO_FATAL_FAILURE(SaveAndReloadSplit());
        ASSERT_NO_FATAL_FAILURE(ExpectReloadedEqualsLive("Main"));
    }

    TEST_F(SceneEditorSaveReferencesTest, AnEditOfAnInstanceIsSavedAsTheOverrideOnly) {
        ASSERT_NO_FATAL_FAILURE(LoadSplitScenes());

        // A field of the first instance, the name and the layer of the third, the title of the canvas of the fourth
        Child("Main", 0).GetComponent<TransformComponent>()->_x = kEditedX;
        SceneNode& box = Child("Main", 2);
        box.SetName("Crate"_intern);
        box.SetLayer(kEditedLayer);
        Child("Main", 3).GetComponent<UICanvasComponent>()->_elements[0]->name = "Heading"_intern;

        ASSERT_TRUE(Save(GetScene("Main")));
        EXPECT_TRUE(_notes.empty());

        EXPECT_EQ(Raw(SavedNode(ConfigRoot(), "Main", 0)), "<node ref=\"nodes/Box\"><components><component type=\"TransformComponent\" x=\"77\"/></components></node>");
        const pugi::xml_node third = SavedNode(ConfigRoot(), "Main", 2);
        EXPECT_STREQ(third.attribute("ref").as_string(), "nodes/Box");
        EXPECT_STREQ(third.attribute("name").as_string(), "Crate");
        EXPECT_EQ(third.attribute("layer").as_int(), kEditedLayer);
        EXPECT_EQ(third.child("components").first_child().attribute("x").as_int(), 100);
        const pugi::xml_node fourth = SavedNode(ConfigRoot(), "Main", 3);
        EXPECT_STREQ(fourth.attribute("ref").as_string(), "nodes/Panel");
        EXPECT_FALSE(fourth.attribute("name"));
        EXPECT_TRUE(fourth.child("components").child("component").child("elements"));
        EXPECT_FALSE(fourth.child("components").child("component").attribute("space"));

        ASSERT_NO_FATAL_FAILURE(SaveAndReloadSplit());
        ASSERT_NO_FATAL_FAILURE(ExpectReloadedEqualsLive("Main"));
        EXPECT_EQ(Reloaded("Main").GetRootNode().GetChildren()[2]->GetName(), "Crate"_intern);
    }

    TEST_F(SceneEditorSaveReferencesTest, AnInlineNodeNextToInstancesStaysInline) {
        ASSERT_NO_FATAL_FAILURE(LoadSplitScenes());
        Child("Main", 1).GetComponent<TransformComponent>()->_x = kEditedX;

        ASSERT_TRUE(Save(GetScene("Main")));

        const pugi::xml_node plain = SavedNode(ConfigRoot(), "Main", 1);
        EXPECT_STREQ(plain.attribute("name").as_string(), "Plain");
        EXPECT_FALSE(plain.attribute("ref"));
        EXPECT_EQ(plain.child("components").first_child().attribute("x").as_int(), static_cast<int>(kEditedX));
        EXPECT_TRUE(_notes.empty());

        ASSERT_NO_FATAL_FAILURE(SaveAndReloadSplit());
        ASSERT_NO_FATAL_FAILURE(ExpectReloadedEqualsLive("Main"));
        EXPECT_TRUE(Reloaded("Main").GetRootNode().GetChildren()[1]->GetSourceRef().Empty());
    }

    TEST_F(SceneEditorSaveReferencesTest, ADeletedInstanceDisappearsAndANewOneTakesItsPlace) {
        ASSERT_NO_FATAL_FAILURE(LoadSplitScenes());
        Scene& main = GetScene("Main");
        main.GetRootNode().RemoveChild(&Child("Main", 0));
        ASSERT_EQ(main.GetRootNode().GetChildren().size(), 3u);

        // A node of the file put first, with an override, the way code would
        pugi::xml_document reference;
        pugi::xml_node referenceEl = reference.append_child("node");
        referenceEl.append_attribute("ref").set_value("nodes/Box");
        referenceEl.append_attribute("name").set_value("Added");
        ASSERT_TRUE(main.InstantiateNode(referenceEl, _loader, nullptr, 0));

        ASSERT_TRUE(Save(main));
        EXPECT_TRUE(_notes.empty());

        const pugi::xml_node nodes = FindSceneElement(ConfigRoot(), "Main").child("nodes");
        EXPECT_EQ(Raw(nodes.first_child()), "<node ref=\"nodes/Box\" name=\"Added\"/>");
        size_t count = 0;
        for (pugi::xml_node node = nodes.first_child(); node; node = node.next_sibling()) {
            ++count;
        }
        EXPECT_EQ(count, 4u);

        ASSERT_NO_FATAL_FAILURE(SaveAndReloadSplit());
        ASSERT_NO_FATAL_FAILURE(ExpectReloadedEqualsLive("Main"));
        EXPECT_EQ(Reloaded("Main").GetRootNode().GetChildren()[0]->GetSourceRef(), "nodes/Box"_intern);
    }

    TEST_F(SceneEditorSaveReferencesTest, AnInstanceThatGainedAComponentIsWrittenInlineWithANote) {
        ASSERT_NO_FATAL_FAILURE(LoadSplitScenes());
        SceneNode& box = Child("Main", 0);
        box.AddComponent(New<SpriteRendererComponent>());

        ASSERT_TRUE(Save(GetScene("Main")));

        ASSERT_EQ(_notes.size(), 1u);
        EXPECT_NE(_notes[0].find("node 'Box'"), String::npos) << _notes[0].c_str();
        EXPECT_NE(_notes[0].find("written inline"), String::npos) << _notes[0].c_str();
        const pugi::xml_node saved = SavedNode(ConfigRoot(), "Main", 0);
        EXPECT_FALSE(saved.attribute("ref"));
        EXPECT_STREQ(saved.attribute("name").as_string(), "Box");

        // The data is kept, the link to the file is not; the other instances are still references
        ASSERT_NO_FATAL_FAILURE(SaveAndReloadSplit());
        const auto& loaded = Reloaded("Main").GetRootNode().GetChildren();
        EXPECT_TRUE(loaded[0]->GetSourceRef().Empty());
        EXPECT_TRUE(loaded[0]->GetComponent<SpriteRendererComponent>());
        EXPECT_EQ(ComponentTexts(*loaded[0]), ComponentTexts(box));
        EXPECT_TRUE(SavedNode(ConfigRoot(), "Main", 2).attribute("ref"));
        EXPECT_EQ(loaded[2]->GetSourceRef(), "nodes/Box"_intern);
    }

    TEST_F(SceneEditorSaveReferencesTest, WithoutALoaderEveryInstanceIsWrittenInlineWithANote) {
        ASSERT_NO_FATAL_FAILURE(LoadSplitScenes());

        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), GetScene("Main"), nullptr, &_notes));

        EXPECT_EQ(_notes.size(), 3u);
        EXPECT_FALSE(HasAttributeBelow(FindSceneElement(ConfigRoot(), "Main"), "ref"));
        // Nothing of the file is lost, only the links
        ASSERT_NO_FATAL_FAILURE(SaveAndReloadSplit());
        const auto& live = GetScene("Main").GetRootNode().GetChildren();
        const auto& loaded = Reloaded("Main").GetRootNode().GetChildren();
        ASSERT_EQ(live.size(), loaded.size());
        for (size_t i = 0; i < live.size(); ++i) {
            EXPECT_TRUE(loaded[i]->GetSourceRef().Empty());
            EXPECT_EQ(ComponentTexts(*live[i]), ComponentTexts(*loaded[i]));
        }
    }

    TEST_F(SceneEditorSaveReferencesTest, ANodeFileThatIsGoneGivesAnInlineNodeAndANote) {
        ASSERT_NO_FATAL_FAILURE(LoadSplitScenes());
        std::filesystem::remove(_dir / "nodes" / "Panel.xml");

        ASSERT_TRUE(Save(GetScene("Main")));

        ASSERT_EQ(_notes.size(), 1u);
        EXPECT_NE(_notes[0].find("node 'Panel'"), String::npos) << _notes[0].c_str();
        const pugi::xml_node panel = SavedNode(ConfigRoot(), "Main", 3);
        EXPECT_FALSE(panel.attribute("ref"));
        EXPECT_TRUE(panel.child("components").find_child_by_attribute("type", "UICanvasComponent").child("elements"));
        EXPECT_TRUE(SavedNode(ConfigRoot(), "Main", 0).attribute("ref"));
    }

    TEST_F(SceneEditorSaveReferencesTest, TheOtherScenesKeepTheirReferencesVerbatim) {
        ASSERT_NO_FATAL_FAILURE(LoadSplitScenes());
        const String otherBefore = Print(FindSceneElement(ConfigRoot(), "Other"));
        const pugi::xml_node otherEl = FindSceneElement(ConfigRoot(), "Other");
        otherEl.parent().insert_child_before(pugi::node_comment, otherEl).set_value(kCreditsComment);

        ASSERT_TRUE(Save(GetScene("Main")));

        EXPECT_EQ(Print(FindSceneElement(ConfigRoot(), "Other")), otherBefore);
        EXPECT_EQ(SceneOrder(ConfigRoot()), "Main,Other");
        EXPECT_EQ(FindSceneElement(ConfigRoot(), "Other").previous_sibling().type(), pugi::node_comment);
    }

    TEST_F(SceneEditorSaveReferencesTest, SavingTwiceGivesTheSameDocument) {
        ASSERT_NO_FATAL_FAILURE(LoadSplitScenes());
        Child("Main", 0).GetComponent<TransformComponent>()->_x = kEditedX;

        ASSERT_TRUE(Save(GetScene("Main")));
        const String once = Print(ConfigRoot());
        ASSERT_TRUE(Save(GetScene("Main")));

        EXPECT_EQ(Print(ConfigRoot()), once);
        EXPECT_EQ(CountScenes(ConfigRoot()), 2u);
    }

    TEST_F(SceneEditorSaveTest, ChildNodesAreSavedNestedInFileOrder) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        _manager->SetActiveScene("Game"_intern);
        Scene& game = *_manager->GetActiveScene();
        SceneNode* board = FindTopLevelNode(game, "Board"_intern);
        ASSERT_NE(board, nullptr);

        // A runtime AddChild (the editor's "Add Child"), a grandchild with a component and a sibling after it
        auto first = board->AddChild("First"_intern);
        auto grandchild = first->AddChild("Grand"_intern);
        auto quad = New<QuadRendererComponent>();
        quad->_color = Color{4, 5, 6, 255};
        grandchild->AddComponent(quad);
        board->AddChild("Second"_intern)->SetLayer(kPieceLayer);

        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), game));

        const pugi::xml_node boardEl = SavedNode(ConfigRoot(), "Game", 0);
        const pugi::xml_node childrenEl = boardEl.child("children");
        ASSERT_TRUE(childrenEl);
        EXPECT_TRUE(boardEl.child("components"));
        EXPECT_TRUE(boardEl.child("components").next_sibling() == childrenEl);
        EXPECT_STREQ(childrenEl.first_child().attribute("name").as_string(), "First");
        EXPECT_STREQ(childrenEl.first_child().next_sibling().attribute("name").as_string(), "Second");
        EXPECT_EQ(childrenEl.first_child().next_sibling().attribute("layer").as_int(), kPieceLayer);
        EXPECT_FALSE(childrenEl.first_child().child("components"));
        EXPECT_STREQ(childrenEl.first_child().child("children").first_child().attribute("name").as_string(), "Grand");
        // A node without children writes no <children>
        EXPECT_FALSE(SavedNode(ConfigRoot(), "Game", 1).child("children"));

        ASSERT_NO_FATAL_FAILURE(SaveAndReload());
        SceneNode* savedBoard = FindTopLevelNode(Reloaded("Game"), "Board"_intern);
        ASSERT_NE(savedBoard, nullptr);
        ASSERT_EQ(savedBoard->GetChildren().size(), 2u);
        EXPECT_EQ(savedBoard->GetChildren()[0]->GetName(), "First"_intern);
        EXPECT_EQ(savedBoard->GetChildren()[1]->GetName(), "Second"_intern);
        EXPECT_EQ(savedBoard->GetChildren()[1]->GetLayer(), kPieceLayer);
        ASSERT_EQ(savedBoard->GetChildren()[0]->GetChildren().size(), 1u);
        const auto& savedGrand = savedBoard->GetChildren()[0]->GetChildren()[0];
        EXPECT_EQ(savedGrand->GetParent(), savedBoard->GetChildren()[0].Get());
        ASSERT_TRUE(savedGrand->GetComponent<QuadRendererComponent>());
        EXPECT_EQ(savedGrand->GetComponent<QuadRendererComponent>()->_color.g, 5);
    }

    TEST_F(SceneEditorSaveTest, ALoadedNestedTreeSurvivesASaveUnchanged) {
        ASSERT_NO_FATAL_FAILURE(LoadOwnScenes(kNestedScenesXml));
        Scene& main = GetScene("Main");

        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), main));
        ASSERT_NO_FATAL_FAILURE(SaveAndReload());

        EXPECT_EQ(TreeOf(Reloaded("Main").GetRootNode()), TreeOf(main.GetRootNode()));
        EXPECT_EQ(TreeOf(main.GetRootNode()), "Left Inner. Deep.. Last. Right");
        const SceneNode* inner = FindNodeByName(Reloaded("Main").GetRootNode(), "Inner"_intern);
        ASSERT_NE(inner, nullptr);
        EXPECT_EQ(inner->GetLayer(), kPieceLayer);
        ASSERT_TRUE(inner->GetComponent<TransformComponent>());
        EXPECT_FLOAT_EQ(inner->GetComponent<TransformComponent>()->_x, kMovedX);
        // A second save of the reloaded scene writes the same document
        const String once = Print(ConfigRoot());
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), main));
        EXPECT_EQ(Print(ConfigRoot()), once);
    }

    TEST_F(SceneEditorSaveTest, TheEditorFlagsAreNotSaved) {
        ASSERT_NO_FATAL_FAILURE(LoadOwnScenes(kNestedScenesXml));
        Scene& main = GetScene("Main");
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), main));
        const String plain = Print(ConfigRoot());

        // Tree: Left (Inner (Deep), Last), Right
        ASSERT_FALSE(main.GetRootNode().GetChildren().empty());
        SceneNode& left = *main.GetRootNode().GetChildren()[0];
        ASSERT_FALSE(left.GetChildren().empty());
        SceneNode* inner = left.GetChildren()[0].Get();
        ASSERT_EQ(inner->GetName(), "Inner"_intern);
        inner->SetEditorFlags(EditorFlags::Hidden | EditorFlags::Locked);
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), main));

        EXPECT_EQ(Print(ConfigRoot()), plain);
    }

    TEST_F(SceneEditorSaveReferencesTest, AnInstanceWithChildrenIsWrittenInlineWithANote) {
        ASSERT_NO_FATAL_FAILURE(LoadSplitScenes());
        SceneNode& box = Child("Main", 0);
        box.AddChild("Kid"_intern);

        ASSERT_TRUE(Save(GetScene("Main")));

        ASSERT_EQ(_notes.size(), 1u);
        EXPECT_NE(_notes[0].find("node 'Box'"), String::npos) << _notes[0].c_str();
        EXPECT_NE(_notes[0].find("it has child nodes"), String::npos) << _notes[0].c_str();
        const pugi::xml_node saved = SavedNode(ConfigRoot(), "Main", 0);
        EXPECT_FALSE(saved.attribute("ref"));
        EXPECT_STREQ(saved.child("children").first_child().attribute("name").as_string(), "Kid");
        // The other instances stay references
        EXPECT_TRUE(SavedNode(ConfigRoot(), "Main", 2).attribute("ref"));

        ASSERT_NO_FATAL_FAILURE(SaveAndReloadSplit());
        const auto& loaded = Reloaded("Main").GetRootNode().GetChildren();
        EXPECT_TRUE(loaded[0]->GetSourceRef().Empty());
        ASSERT_EQ(loaded[0]->GetChildren().size(), 1u);
        EXPECT_EQ(ComponentTexts(*loaded[0]), ComponentTexts(box));
    }

    TEST_F(SceneEditorSaveReferencesTest, ANestedInstanceIsWrittenInlineWithANote) {
        ASSERT_NO_FATAL_FAILURE(LoadSplitScenes());
        Scene& main = GetScene("Main");
        pugi::xml_document reference;
        reference.append_child("node").append_attribute("ref").set_value("nodes/Box");
        ASSERT_TRUE(main.InstantiateNode(reference.document_element(), _loader, &Child("Main", 1)));

        ASSERT_TRUE(Save(main));

        ASSERT_EQ(_notes.size(), 1u);
        EXPECT_NE(_notes[0].find("node 'Box'"), String::npos) << _notes[0].c_str();
        EXPECT_NE(_notes[0].find("nested"), String::npos) << _notes[0].c_str();
        const pugi::xml_node nested = SavedNode(ConfigRoot(), "Main", 1).child("children").first_child();
        EXPECT_FALSE(nested.attribute("ref"));
        EXPECT_STREQ(nested.attribute("name").as_string(), "Box");
        EXPECT_FALSE(HasAttributeBelow(ConfigRoot(), "sourceRef"));

        ASSERT_NO_FATAL_FAILURE(SaveAndReloadSplit());
        const auto& loadedNested = Reloaded("Main").GetRootNode().GetChildren()[1]->GetChildren();
        ASSERT_EQ(loadedNested.size(), 1u);
        EXPECT_TRUE(loadedNested[0]->GetSourceRef().Empty());
        EXPECT_EQ(ComponentTexts(*loadedNested[0]), ComponentTexts(*Child("Main", 1).GetChildren()[0]));
    }

    TEST_F(SceneEditorSaveTest, AnInvalidRootIsRejected) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        _manager->SetActiveScene("Game"_intern);

        EXPECT_FALSE(WriteSceneToConfig(pugi::xml_node{}, *_manager->GetActiveScene()));
    }

}  // namespace BECore::Tests
