#include <BECore/Config/ContentVersion.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIControl.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/TempPath.h>
#include <Common/UILayoutMatrix.h>
#include <Common/UITreeCompare.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // A scene "Main" of three nodes, written inline and as references. Hud (layer 10) and Menu (layer 5) are UI canvases; Menu holds a button of 20 units, below the touch size, so the
        // validator has something to find on the matrix. Board is a plain node with a transform and a quad.
        constexpr const char* kBoardNode = R"(<node name="Board"><components>
  <component type="TransformComponent" x="3" y="4" width="50" height="60"/>
  <component type="QuadRendererComponent"/>
</components></node>)";

        constexpr const char* kHudNode = R"(<node name="Hud" layer="10"><components>
  <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
    <elements>
      <element type="UIPanel" name="Plate" x="10" y="10" width="200" height="100" color="10,20,30,200">
        <children>
          <element type="UILabel" name="Title" x="8" y="8" width="184" height="30" text="Score: {splitScore}" fontSize="24"/>
          <element type="UIButton" name="Ok" anchor="BottomRight" x="-8" y="-8" width="80" height="40" text="OK" action="SceneSplitOk" color="40,90,60,255"/>
        </children>
      </element>
    </elements>
  </component>
</components></node>)";

        constexpr const char* kMenuNode = R"(<node name="Menu" layer="5"><components>
  <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
    <elements>
      <element type="UIButton" name="Tiny" x="400" y="10" width="20" height="20" text="T" action="SceneSplitTiny" color="40,90,60,255"/>
      <element type="UILabel" name="Note" x="300" y="300" width="80" height="40" text="Note"/>
    </elements>
  </component>
</components></node>)";

        // Read through the file-system loader, which refuses a node file that is not at the current content version
        eastl::string CurrentNode(const char* name) {
            return Format("<node name=\"{}\" contentVersion=\"{}\"/>", name, kContentVersion);
        }

        const char* const kNodeNames[] = {"Board", "Hud", "Menu"};
        const char* const kNodeTexts[] = {kBoardNode, kHudNode, kMenuNode};

        std::string Scene(const std::string& nodesXml) {
            return "<SceneManager><scenes><scene type=\"Scene\" name=\"Main\" viewWidth=\"800\" viewHeight=\"600\"><nodes>" + nodesXml + "</nodes></scene></scenes></SceneManager>";
        }

        /// The inline scene: the nodes in the order of @p order (indices into kNodeTexts)
        std::string InlineScene(std::initializer_list<int> order) {
            std::string nodes;
            for (int index : order) {
                nodes += kNodeTexts[index];
            }
            return Scene(nodes);
        }

        /// The same scene as references
        std::string SplitScene(std::initializer_list<int> order) {
            std::string nodes;
            for (int index : order) {
                nodes += std::string("<node ref=\"nodes/") + kNodeNames[index] + "\"/>";
            }
            return Scene(nodes);
        }

        void WriteFile(const std::filesystem::path& path, const std::string& text) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path) << text;
        }

        std::string Printed(const pugi::xml_node& node) {
            std::ostringstream out;
            node.print(out, "", pugi::format_raw);
            return out.str();
        }

        std::string Text(const eastl::string& text) {
            return std::string(text.c_str(), text.size());
        }

        const Json* Member(const Json& object, eastl::string_view key) {
            return FindJsonMember(object, key);
        }

        /// GetDrawRank of every child of the scene root, in scene order
        eastl::vector<int32_t> DrawRanks(BECore::Scene& scene) {
            eastl::vector<int32_t> ranks;
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                ranks.push_back(scene.GetDrawRank(*node));
            }
            return ranks;
        }

        /// Two scenes hold the same nodes in the same order: name, layer, the types of the components, the transform, the elements of every canvas
        void ExpectSameScene(BECore::Scene& expected, BECore::Scene& actual) {
            const auto& expectedNodes = expected.GetRootNode().GetChildren();
            const auto& actualNodes = actual.GetRootNode().GetChildren();
            ASSERT_EQ(expectedNodes.size(), actualNodes.size());
            for (size_t i = 0; i < expectedNodes.size(); ++i) {
                const SceneNode& a = *expectedNodes[i];
                const SceneNode& b = *actualNodes[i];
                EXPECT_EQ(a.GetName(), b.GetName());
                EXPECT_EQ(a.GetLayer(), b.GetLayer()) << a.GetName().CStr();
                ASSERT_EQ(a.GetComponents().size(), b.GetComponents().size()) << a.GetName().CStr();
                for (size_t c = 0; c < a.GetComponents().size(); ++c) {
                    EXPECT_TRUE(a.GetComponents()[c]->GetTypeMeta() == b.GetComponents()[c]->GetTypeMeta()) << a.GetName().CStr() << " component " << c;
                }
                const auto transformA = a.GetComponent<TransformComponent>();
                const auto transformB = b.GetComponent<TransformComponent>();
                ASSERT_EQ(static_cast<bool>(transformA), static_cast<bool>(transformB));
                if (transformA) {
                    EXPECT_EQ(transformA->_x, transformB->_x);
                    EXPECT_EQ(transformA->_y, transformB->_y);
                    EXPECT_EQ(transformA->_width, transformB->_width);
                    EXPECT_EQ(transformA->_height, transformB->_height);
                }
                const auto canvasA = a.GetComponent<UICanvasComponent>();
                const auto canvasB = b.GetComponent<UICanvasComponent>();
                ASSERT_EQ(static_cast<bool>(canvasA), static_cast<bool>(canvasB));
                if (canvasA) {
                    ExpectSameElements(canvasA->_elements, canvasB->_elements);
                }
            }
            EXPECT_EQ(DrawRanks(expected), DrawRanks(actual));
        }

        std::vector<std::string> NodeNamesOf(BECore::Scene& scene) {
            std::vector<std::string> names;
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                names.emplace_back(node->GetName().CStr());
            }
            return names;
        }

    }  // namespace

    /// A scene split into node files is, once loaded, the same scene as the inline one. Engine services, node files in a temp folder.
    class SceneSplitTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("BagiSceneSplitTest");
            std::filesystem::remove_all(_dir);
            std::filesystem::create_directories(_dir);
            for (int i = 0; i < 3; ++i) {
                WriteFile(_dir / "nodes" / (std::string(kNodeNames[i]) + ".xml"), kNodeTexts[i]);
            }
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            RegisterUIControl(_service);
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            EngineEnvironment::UseSceneManager({});
            CoreManager::GetRendererManager().SetRenderer({});
            std::error_code ignored;
            std::filesystem::remove_all(_dir, ignored);
        }

        /// Loads a SceneConfig text through LoadSceneManager with the node files of the temp folder; the document is kept alive in @p doc
        IntrusivePtrAtomic<SceneManager> Load(const std::string& text, pugi::xml_document& doc) {
            const pugi::xml_parse_result parsed = doc.load_string(text.c_str());
            EXPECT_TRUE(parsed) << parsed.description();
            auto manager = LoadSceneManager(XmlNode{doc.document_element()}, MakeDirectoryNodeLoader(_dir));
            manager->Initialize();
            return manager;
        }

        static BECore::Scene& Main(const IntrusivePtrAtomic<SceneManager>& manager) {
            BECore::Scene* scene = manager->GetSceneByName(PoolString::Intern("Main"));
            EXPECT_NE(scene, nullptr);
            return *scene;
        }

        /// The `ui_tree` answer of the active scene "Main" after a draw, as text
        std::string UiTree(const IntrusivePtrAtomic<SceneManager>& manager) {
            EngineEnvironment::UseSceneManager(manager);
            manager->SetActiveScene(PoolString::Intern("Main"));
            _renderer->ClearCalls();
            manager->DrawAll();

            Json request = Json::object();
            request["id"] = 1;
            request["method"] = "ui_tree";
            std::string answer;
            _service.Post(DumpJson(request), [&answer](eastl::string text) { answer = Text(text); });
            _service.Drain();
            EngineEnvironment::UseSceneManager({});
            return answer;
        }

        /// What the validator finds on the matrix for every canvas of "Main", as one text
        static std::string ValidatorFindings(const IntrusivePtrAtomic<SceneManager>& manager) {
            std::string all;
            UILayoutValidatorOptions options;
            options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            for (const auto& node : Main(manager).GetRootNode().GetChildren()) {
                if (const auto canvas = node->GetComponent<UICanvasComponent>()) {
                    all += std::string(node->GetName().CStr()) + ":" + Text(DescribeMatrixFailures(ValidateOnMatrix(canvas->GetLayout(), options), 100)) + "\n";
                }
            }
            return all;
        }

        std::filesystem::path _dir;
        ControlService _service;
        IntrusivePtr<RecordingRenderer> _renderer;
    };

    TEST_F(SceneSplitTest, ASplitSceneLoadsAsTheInlineOne) {
        pugi::xml_document inlineDoc;
        pugi::xml_document splitDoc;
        const auto inlined = Load(InlineScene({0, 1, 2}), inlineDoc);
        const auto split = Load(SplitScene({0, 1, 2}), splitDoc);

        EXPECT_EQ(NodeNamesOf(Main(split)), (std::vector<std::string>{"Board", "Hud", "Menu"}));
        ExpectSameScene(Main(inlined), Main(split));
        // The draw order is by layer, then scene order: Board (0) first, Menu (5), Hud (10) last; the ranks count the root node too
        EXPECT_EQ(DrawRanks(Main(split)), (eastl::vector<int32_t>{1, 3, 2}));
    }

    TEST_F(SceneSplitTest, TheUiTreeIsTheSame) {
        pugi::xml_document inlineDoc;
        pugi::xml_document splitDoc;
        const auto inlined = Load(InlineScene({0, 1, 2}), inlineDoc);
        const auto split = Load(SplitScene({0, 1, 2}), splitDoc);

        const std::string expected = UiTree(inlined);
        const std::string actual = UiTree(split);

        EXPECT_NE(expected.find("\"Plate\""), std::string::npos) << expected;
        EXPECT_NE(expected.find("\"Tiny\""), std::string::npos) << expected;
        EXPECT_EQ(expected, actual);
    }

    TEST_F(SceneSplitTest, TheValidatorFindsTheSame) {
        pugi::xml_document inlineDoc;
        pugi::xml_document splitDoc;
        const auto inlined = Load(InlineScene({0, 1, 2}), inlineDoc);
        const auto split = Load(SplitScene({0, 1, 2}), splitDoc);

        const std::string expected = ValidatorFindings(inlined);
        const std::string actual = ValidatorFindings(split);

        // Not vacuous: the 20 unit button of Menu is under the touch size somewhere on the matrix
        EXPECT_NE(expected.find("Tiny"), std::string::npos) << expected;
        EXPECT_EQ(expected, actual);
    }

    TEST_F(SceneSplitTest, ReferencesInAnotherOrderLoadInThatOrder) {
        pugi::xml_document inlineDoc;
        pugi::xml_document splitDoc;
        const auto inlined = Load(InlineScene({2, 0, 1}), inlineDoc);
        const auto split = Load(SplitScene({2, 0, 1}), splitDoc);

        EXPECT_EQ(NodeNamesOf(Main(split)), (std::vector<std::string>{"Menu", "Board", "Hud"}));
        ExpectSameScene(Main(inlined), Main(split));
    }

    TEST_F(SceneSplitTest, ReferencesMixedWithInlineNodesKeepTheirPlace) {
        pugi::xml_document inlineDoc;
        pugi::xml_document splitDoc;
        const auto inlined = Load(Scene(std::string(kBoardNode) + kHudNode + "<node name=\"Tail\"/>"), inlineDoc);
        const auto split = Load(Scene("<node ref=\"nodes/Board\"/>" + std::string(kHudNode) + "<node name=\"Tail\"/>"), splitDoc);

        EXPECT_EQ(NodeNamesOf(Main(split)), (std::vector<std::string>{"Board", "Hud", "Tail"}));
        ExpectSameScene(Main(inlined), Main(split));
    }

    TEST_F(SceneSplitTest, AMissingReferenceLeavesTheOtherNodesInOrder) {
        pugi::xml_document doc;
        const auto manager = Load(Scene(std::string("<node ref=\"nodes/Board\"/><node ref=\"nodes/Nowhere\"/>") + "<node ref=\"nodes/Menu\"/>"), doc);

        EXPECT_EQ(NodeNamesOf(Main(manager)), (std::vector<std::string>{"Board", "Menu"}));
    }

    TEST_F(SceneSplitTest, LoadSceneManagerLeavesTheGivenNodeAsItWas) {
        pugi::xml_document doc;
        ASSERT_TRUE(doc.load_string(SplitScene({0, 1, 2}).c_str()));
        const std::string before = Printed(doc.document_element());

        const auto manager = LoadSceneManager(XmlNode{doc.document_element()}, MakeDirectoryNodeLoader(_dir));

        EXPECT_EQ(NodeNamesOf(Main(manager)), (std::vector<std::string>{"Board", "Hud", "Menu"}));
        EXPECT_EQ(Printed(doc.document_element()), before);
        EXPECT_NE(before.find("ref=\"nodes/Hud\""), std::string::npos);
    }

    TEST_F(SceneSplitTest, ADocumentWithoutReferencesIsADeserializeAway) {
        pugi::xml_document doc;
        ASSERT_TRUE(doc.load_string(InlineScene({0, 1, 2}).c_str()));
        const auto loaded = LoadSceneManager(XmlNode{doc.document_element()}, MakeDirectoryNodeLoader(_dir));
        loaded->Initialize();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(InlineScene({0, 1, 2}).c_str()));
        auto plain = New<SceneManager>();
        plain->Deserialize(deserializer);
        ASSERT_FALSE(deserializer.HasErrors());
        plain->Initialize();

        ExpectSameScene(Main(plain), Main(loaded));
    }

    TEST_F(SceneSplitTest, AnInvalidRootGivesAnEmptyManager) {
        const auto manager = LoadSceneManager(XmlNode{}, MakeDirectoryNodeLoader(_dir));

        ASSERT_TRUE(manager);
        EXPECT_EQ(manager->GetActiveScene(), nullptr);
        EXPECT_EQ(manager->GetSceneByName(PoolString::Intern("Main")), nullptr);
    }

    TEST_F(SceneSplitTest, LoadSceneDocumentOfASplitFileEqualsTheInlineDocument) {
        WriteFile(_dir / "SceneConfig.xml", SplitScene({0, 1, 2}));

        pugi::xml_document split;
        const NodeExpansion result = LoadSceneDocument(_dir / "SceneConfig.xml", split);

        pugi::xml_document inlined;
        ASSERT_TRUE(inlined.load_string(InlineScene({0, 1, 2}).c_str()));
        EXPECT_TRUE(result.Ok());
        EXPECT_EQ(result.expanded, 3u);
        EXPECT_EQ(Printed(split.document_element()), Printed(inlined.document_element()));
    }

    // ---- layering ---------------------------------------------------------------------------------------------------------

    /// Two content roots, an engine and a game on top of it, both with config/nodes/Shared.xml; only the engine has Only.xml
    TEST(SceneSplitLayerTest, AGameNodeFileReplacesTheEngineOne) {
        const std::filesystem::path base = TestTempDirectory("BagiSceneSplitLayer");
        std::filesystem::remove_all(base);
        const std::filesystem::path engine = base / "engine";
        const std::filesystem::path game = base / "game";
        WriteFile(engine / "config" / "nodes" / "Shared.xml", CurrentNode("FromEngine").c_str());
        WriteFile(game / "config" / "nodes" / "Shared.xml", CurrentNode("FromGame").c_str());
        WriteFile(engine / "config" / "nodes" / "Only.xml", CurrentNode("Only").c_str());

        FileSystem fs;
        fs.AddContentRoot(engine);
        fs.AddContentRoot(game);
        fs.Initialize();
        const NodeFileLoader loader = MakeFileSystemNodeLoader(fs);

        pugi::xml_document doc;
        ASSERT_TRUE(doc.load_string(Scene("<node ref=\"nodes/Shared\"/><node ref=\"nodes/Only\"/>").c_str()));
        const NodeExpansion result = ExpandNodeReferences(doc.document_element(), loader);

        EXPECT_TRUE(result.Ok());
        EXPECT_EQ(result.expanded, 2u);
        pugi::xml_node first = doc.document_element().child("scenes").child("scene").child("nodes").child("node");
        EXPECT_STREQ(first.attribute("name").as_string(), "FromGame");
        EXPECT_STREQ(first.next_sibling("node").attribute("name").as_string(), "Only");

        std::error_code ignored;
        std::filesystem::remove_all(base, ignored);
    }

}  // namespace BECore::Tests
