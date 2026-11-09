#include <BECore/Config/XmlDocument.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/SceneEditor/SceneConfigWriter.h>

namespace BECore::Tests {

    namespace {

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
                if (eastl::string_view{el.attribute("name").as_string()} == name) {
                    return el;
                }
            }
            return {};
        }

        /// The `name` of every `<scene>` of @p configRoot in file order, joined with commas ("Menu,Game,Credits")
        eastl::string SceneOrder(pugi::xml_node configRoot) {
            eastl::string order;
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

        eastl::string Print(pugi::xml_node node) {
            std::ostringstream stream;
            node.print(stream, "  ");
            const std::string text = stream.str();
            return eastl::string{text.c_str(), text.size()};
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

        /// Puts `<node ref="nodes/Extra"/>` between the nodes of the `<scene>` element called @p sceneName
        void AddNodeReference(pugi::xml_node configRoot, const char* sceneName) {
            pugi::xml_node nodes = FindSceneElement(configRoot, sceneName).child("nodes");
            nodes.insert_child_after("node", nodes.first_child()).append_attribute("ref").set_value("nodes/Extra");
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

        /// A scene of the reloaded manager, which the test has checked is there
        Scene& Reloaded(const char* name) const {
            return *_reloaded->GetSceneByName(PoolString::Intern(name));
        }

        XmlDocument _document;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtrAtomic<SceneManager> _reloaded;

    private:
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

    TEST_F(SceneEditorSaveTest, TheOtherScenesAreKeptVerbatim) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        const eastl::string menuBefore = Print(FindSceneElement(ConfigRoot(), "Menu"));
        const eastl::string creditsBefore = Print(FindSceneElement(ConfigRoot(), "Credits"));
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
        const eastl::string menuBefore = Print(FindSceneElement(ConfigRoot(), "Menu"));
        const eastl::string creditsBefore = Print(FindSceneElement(ConfigRoot(), "Credits"));

        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), *_manager->GetActiveScene()));

        EXPECT_EQ(SceneOrder(ConfigRoot()), "Menu,Credits,Game");
        EXPECT_EQ(Print(FindSceneElement(ConfigRoot(), "Menu")), menuBefore);
        EXPECT_EQ(Print(FindSceneElement(ConfigRoot(), "Credits")), creditsBefore);
        EXPECT_EQ(eastl::string_view{scenes.last_child().attribute("name").as_string()}, "Game");

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
        EXPECT_EQ(eastl::string_view{saved.attribute("type").as_string()}, game.GetTypeMeta().typeName);

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
        const eastl::string once = Print(ConfigRoot());
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), game));

        EXPECT_EQ(Print(ConfigRoot()), once);
        EXPECT_EQ(CountScenes(ConfigRoot()), 3u);
    }

    TEST_F(SceneEditorSaveTest, ASceneWithANodeReferenceIsRefused) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        AddNodeReference(ConfigRoot(), "Game");
        const eastl::string before = Print(ConfigRoot());

        // The live tree is flat: saving it would inline the reference. Nothing is written, not even the other scenes
        _manager->SetActiveScene("Game"_intern);
        FindTopLevelNode(*_manager->GetActiveScene(), "Piece"_intern)->SetName("Pawn"_intern);
        EXPECT_FALSE(WriteSceneToConfig(ConfigRoot(), *_manager->GetActiveScene()));
        EXPECT_EQ(Print(ConfigRoot()), before);
        EXPECT_EQ(SceneOrder(ConfigRoot()), "Menu,Game,Credits");
        EXPECT_TRUE(FindSceneElement(ConfigRoot(), "Game").child("nodes").child("node").next_sibling("node").attribute("ref"));
    }

    TEST_F(SceneEditorSaveTest, ASceneWithoutReferencesIsStillWritten) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        AddNodeReference(ConfigRoot(), "Game");
        const eastl::string gameBefore = Print(FindSceneElement(ConfigRoot(), "Game"));

        _manager->SetActiveScene("Menu"_intern);
        FindTopLevelNode(*_manager->GetActiveScene(), "Title"_intern)->SetName("Heading"_intern);
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), *_manager->GetActiveScene()));

        // Menu is written, the scene with the reference is left as it was
        EXPECT_STREQ(FindSceneElement(ConfigRoot(), "Menu").child("nodes").first_child().attribute("name").as_string(), "Heading");
        EXPECT_EQ(Print(FindSceneElement(ConfigRoot(), "Game")), gameBefore);
        EXPECT_EQ(SceneOrder(ConfigRoot()), "Menu,Game,Credits");
    }

    TEST_F(SceneEditorSaveTest, AReferenceInAnotherSceneDoesNotBlock) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        AddNodeReference(ConfigRoot(), "Credits");

        _manager->SetActiveScene("Game"_intern);
        FindTopLevelNode(*_manager->GetActiveScene(), "Piece"_intern)->SetName("Pawn"_intern);
        ASSERT_TRUE(WriteSceneToConfig(ConfigRoot(), *_manager->GetActiveScene()));

        const pugi::xml_node game = FindSceneElement(ConfigRoot(), "Game");
        EXPECT_STREQ(game.child("nodes").first_child().next_sibling().attribute("name").as_string(), "Pawn");
        EXPECT_TRUE(FindSceneElement(ConfigRoot(), "Credits").child("nodes").first_child().next_sibling().attribute("ref"));
        EXPECT_EQ(SceneOrder(ConfigRoot()), "Menu,Game,Credits");
    }

    TEST_F(SceneEditorSaveTest, AnInvalidRootIsRejected) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes());
        _manager->SetActiveScene("Game"_intern);

        EXPECT_FALSE(WriteSceneToConfig(pugi::xml_node{}, *_manager->GetActiveScene()));
    }

}  // namespace BECore::Tests
