#include <BECore/Config/ContentVersion.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kHudNode = R"(<node name="Hud" layer="10"><components>
  <component type="TransformComponent" x="1" y="2" width="3" height="4"/>
  <component type="QuadRendererComponent"><color r="9" g="8" b="7" a="255"/></component>
</components></node>)";

        constexpr const char* kPlainNode = R"(<node name="B"><components>
  <component type="TransformComponent" x="5" y="6" width="7" height="8"/>
</components></node>)";

        void WriteFile(const std::filesystem::path& path, const std::string& text) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path) << text;
        }

        pugi::xml_document Parse(const char* text) {
            pugi::xml_document doc;
            const pugi::xml_parse_result result = doc.load_string(text);
            EXPECT_TRUE(result) << result.description();
            return doc;
        }

        std::string Saved(const pugi::xml_node& node) {
            std::ostringstream out;
            node.print(out, "  ", pugi::format_indent);
            return out.str();
        }

        /// The names of the `<node>` elements of the scene `sceneName`, in order
        std::vector<std::string> NodeNames(pugi::xml_node root, const char* sceneName) {
            std::vector<std::string> names;
            for (pugi::xml_node scene : root.child("scenes").children("scene")) {
                if (std::string_view{scene.attribute("name").as_string()} != sceneName) {
                    continue;
                }
                for (pugi::xml_node node : scene.child("nodes").children("node")) {
                    names.emplace_back(node.attribute("name").as_string());
                }
            }
            return names;
        }

        std::string SceneConfigWith(const std::string& nodesXml) {
            return "<SceneManager><scenes><scene type=\"Scene\" name=\"Main\"><nodes>" + nodesXml + "</nodes></scene></scenes></SceneManager>";
        }

        /// A node file at the current content version, as the file-system loader wants it
        std::string CurrentNode(const char* name) {
            return std::string("<node name=\"") + name + "\" contentVersion=\"" + std::to_string(kContentVersion) + "\"/>";
        }

        /// The text of a file, to see that a node file is not changed by the instances made of it
        std::string ReadFile(const std::filesystem::path& path) {
            std::ifstream in(path);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        /// The first `<node>` of the first scene
        pugi::xml_node FirstNode(const pugi::xml_document& doc) {
            return doc.document_element().child("scenes").child("scene").child("nodes").child("node");
        }

        /// The `<component type="..">` of a `<node>`
        pugi::xml_node ComponentOf(pugi::xml_node node, const char* type) {
            for (pugi::xml_node component : node.child("components").children("component")) {
                if (std::string_view{component.attribute("type").as_string()} == type) {
                    return component;
                }
            }
            return {};
        }

        size_t Count(pugi::xml_node node, const char* childName) {
            return static_cast<size_t>(std::distance(node.children(childName).begin(), node.children(childName).end()));
        }

        /// References to `nodes/Hud` that are invalid, each for another reason (the file has no `Nothing` component)
        constexpr const char* kInvalidReferences[] = {
            R"(<node ref="nodes/Hud" foo="1"/>)",
            R"(<node ref="nodes/Hud"><children/></node>)",
            R"(<node ref="nodes/Hud"><components/><components/></node>)",
            R"(<node ref="nodes/Hud"><components><other/></components></node>)",
            R"(<node ref="nodes/Hud"><components><component x="1"/></components></node>)",
            R"(<node ref="nodes/Hud"><components><component type="" x="1"/></components></node>)",
            R"(<node ref="nodes/Hud"><components><component type="TransformComponent" x="1"/><component type="TransformComponent" y="1"/></components></node>)",
            R"(<node ref="nodes/Hud"><components><component type="Nothing" x="1"/></components></node>)",
            R"(<node ref="nodes/Hud" name="N"><components><component type="TransformComponent" x="1"/><component type="Nothing"/></components></node>)",
            R"(<node ref="nodes/Hud"><components><component type="QuadRendererComponent" color="1,2,3,4"><color r="1"/></component></components></node>)",
        };

        bool Mentions(const eastl::string& error, const char* part) {
            return std::string_view{error.c_str()}.find(part) != std::string_view::npos;
        }

    }  // namespace

    /// Node files in a temp folder, loaded through the directory loader; no engine services.
    class SceneNodeFilesTest : public ::testing::Test {
    protected:
        void SetUp() override {
            // One folder per test and per process (ctest runs the tests in parallel processes)
            _dir = TestTempDirectory("BagiSceneNodeFilesTest");
            std::filesystem::remove_all(_dir);
            std::filesystem::create_directories(_dir);
            WriteFile(_dir / "nodes" / "Hud.xml", kHudNode);
            WriteFile(_dir / "nodes" / "B.xml", kPlainNode);
        }

        void TearDown() override {
            std::error_code ignored;
            std::filesystem::remove_all(_dir, ignored);
        }

        /// Expands @p sceneXml (a SceneConfig text) against the folder
        NodeExpansion Expand(const std::string& sceneXml, pugi::xml_document& doc) {
            const pugi::xml_parse_result parsed = doc.load_string(sceneXml.c_str());
            EXPECT_TRUE(parsed) << parsed.description();
            return ExpandNodeReferences(doc.document_element(), MakeDirectoryNodeLoader(_dir));
        }

        /// An expansion that failed once: one error that names the scene and the reference
        void ExpectOneError(const NodeExpansion& result, const char* reference) {
            EXPECT_FALSE(result.Ok());
            EXPECT_EQ(result.expanded, 0u);
            ASSERT_EQ(result.errors.size(), 1u);
            EXPECT_TRUE(Mentions(result.errors[0], "'Main'")) << result.errors[0].c_str();
            EXPECT_TRUE(Mentions(result.errors[0], reference)) << result.errors[0].c_str();
        }

        std::filesystem::path _dir;
    };

    TEST(SceneNodeFilesRefTest, ValidReferences) {
        EXPECT_TRUE(IsValidNodeRef("a"));
        EXPECT_TRUE(IsValidNodeRef("nodes/MahjongHud"));
        EXPECT_TRUE(IsValidNodeRef("a-b_c/D1"));
        EXPECT_TRUE(IsValidNodeRef("a/b/c/d"));
    }

    TEST(SceneNodeFilesRefTest, InvalidReferences) {
        for (const char* ref : {"", "/", "/a", "a/", "a//b", "../a", "a/../b", "./a", "a/./b", "a\\b", "a.xml", "a b", "config/a.xml", "a:b"}) {
            EXPECT_FALSE(IsValidNodeRef(ref)) << "'" << ref << "'";
        }
    }

    TEST_F(SceneNodeFilesTest, ReferenceIsReplacedInPlace) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node name="A"/><node ref="nodes/Hud"/><node name="C"/>)"), doc);

        EXPECT_TRUE(result.Ok());
        EXPECT_EQ(result.expanded, 1u);
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "Hud", "C"}));

        pugi::xml_node hud = doc.document_element().child("scenes").child("scene").child("nodes").child("node").next_sibling("node");
        EXPECT_STREQ(hud.attribute("layer").as_string(), "10");
        EXPECT_FALSE(hud.attribute("ref"));
        pugi::xml_node components = hud.child("components");
        ASSERT_TRUE(components);
        EXPECT_STREQ(components.first_child().attribute("type").as_string(), "TransformComponent");
        EXPECT_STREQ(components.first_child().attribute("width").as_string(), "3");
        EXPECT_STREQ(components.first_child().next_sibling().child("color").attribute("r").as_string(), "9");
    }

    TEST_F(SceneNodeFilesTest, ReferencesKeepTheOrderOfTheReferences) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node ref="nodes/B"/><node name="A"/><node ref="nodes/Hud"/><node ref="nodes/B"/>)"), doc);

        EXPECT_TRUE(result.Ok());
        EXPECT_EQ(result.expanded, 3u);
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"B", "A", "Hud", "B"}));
    }

    TEST_F(SceneNodeFilesTest, ReferencesInTwoScenes) {
        pugi::xml_document doc;
        const auto result = Expand(
            R"(<SceneManager><scenes>
  <scene type="Scene" name="One"><nodes><node ref="nodes/Hud"/><node name="X"/></nodes></scene>
  <scene type="Scene" name="Two"><nodes><node name="Y"/><node ref="nodes/B"/></nodes></scene>
</scenes></SceneManager>)",
            doc);

        EXPECT_TRUE(result.Ok());
        EXPECT_EQ(result.expanded, 2u);
        EXPECT_EQ(NodeNames(doc.document_element(), "One"), (std::vector<std::string>{"Hud", "X"}));
        EXPECT_EQ(NodeNames(doc.document_element(), "Two"), (std::vector<std::string>{"Y", "B"}));
    }

    TEST_F(SceneNodeFilesTest, DocumentWithoutReferencesIsUnchanged) {
        const char* text = R"(<SceneManager><scenes>
  <scene type="Scene" name="Main" viewWidth="10" viewHeight="20"><nodes>
    <node name="A" layer="2"><components><component type="TransformComponent" x="1" y="2" width="3" height="4"/></components></node>
    <node name="B"/>
  </nodes></scene>
</scenes></SceneManager>)";
        pugi::xml_document doc;
        ASSERT_TRUE(doc.load_string(text));
        const std::string before = Saved(doc);

        const auto result = ExpandNodeReferences(doc.document_element(), MakeDirectoryNodeLoader(_dir));

        EXPECT_TRUE(result.Ok());
        EXPECT_EQ(result.expanded, 0u);
        EXPECT_EQ(Saved(doc), before);
    }

    TEST_F(SceneNodeFilesTest, ConfigWithoutScenesIsHarmless) {
        pugi::xml_document doc;
        ASSERT_TRUE(doc.load_string("<SceneManager/>"));
        const auto result = ExpandNodeReferences(doc.document_element(), MakeDirectoryNodeLoader(_dir));
        EXPECT_TRUE(result.Ok());
        EXPECT_EQ(result.expanded, 0u);

        // An empty node (no document element at all) is not a crash either
        EXPECT_TRUE(ExpandNodeReferences(pugi::xml_node{}, MakeDirectoryNodeLoader(_dir)).Ok());
    }

    TEST_F(SceneNodeFilesTest, MissingFileIsOneErrorAndTheNeighboursStay) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node name="A"/><node ref="nodes/Nothing"/><node name="C"/>)"), doc);

        ExpectOneError(result, "nodes/Nothing");
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "C"}));
    }

    TEST_F(SceneNodeFilesTest, DocumentElementThatIsNotANodeIsAnError) {
        WriteFile(_dir / "nodes" / "NotNode.xml", R"(<scene name="X"/>)");
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node name="A"/><node ref="nodes/NotNode"/><node name="C"/>)"), doc);

        ExpectOneError(result, "nodes/NotNode");
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "C"}));
    }

    TEST_F(SceneNodeFilesTest, NodeThatIsAReferenceItselfIsAnError) {
        WriteFile(_dir / "nodes" / "Nested.xml", R"(<node ref="nodes/B"/>)");
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node name="A"/><node ref="nodes/Nested"/><node name="C"/>)"), doc);

        ExpectOneError(result, "nodes/Nested");
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "C"}));
    }

    TEST_F(SceneNodeFilesTest, SelfReferenceCannotLoop) {
        WriteFile(_dir / "nodes" / "Self.xml", R"(<node ref="nodes/Self"/>)");
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node ref="nodes/Self"/>)"), doc);

        ExpectOneError(result, "nodes/Self");
        EXPECT_TRUE(NodeNames(doc.document_element(), "Main").empty());
    }

    TEST_F(SceneNodeFilesTest, FileThatDoesNotParseIsAnError) {
        WriteFile(_dir / "nodes" / "Broken.xml", "<node name=\"Broken\"><components></node>");
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node name="A"/><node ref="nodes/Broken"/><node name="C"/>)"), doc);

        ExpectOneError(result, "nodes/Broken");
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "C"}));
    }

    TEST_F(SceneNodeFilesTest, InvalidReferenceIsAnError) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node name="A"/><node ref="../nodes/B"/><node name="C"/>)"), doc);

        ExpectOneError(result, "../nodes/B");
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "C"}));
    }

    TEST_F(SceneNodeFilesTest, ReferenceWithAnUnsupportedAttributeIsAnError) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node name="A"/><node ref="nodes/B" foo="1"/><node name="C"/>)"), doc);

        ExpectOneError(result, "nodes/B");
        EXPECT_TRUE(Mentions(result.errors[0], "'foo'")) << result.errors[0].c_str();
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "C"}));
    }

    TEST_F(SceneNodeFilesTest, ReferenceWithAChildThatIsNotComponentsIsAnError) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node name="A"/><node ref="nodes/B"><children/></node><node name="C"/>)"), doc);

        ExpectOneError(result, "nodes/B");
        EXPECT_TRUE(Mentions(result.errors[0], "'children'")) << result.errors[0].c_str();
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "C"}));
    }

    TEST_F(SceneNodeFilesTest, ErrorsAndExpansionsAreCountedTogether) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node ref="nodes/Hud"/><node ref="nodes/Nothing"/><node ref="nodes/B"/><node ref="bad ref"/>)"), doc);

        EXPECT_FALSE(result.Ok());
        EXPECT_EQ(result.expanded, 2u);
        EXPECT_EQ(result.errors.size(), 2u);
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"Hud", "B"}));
    }

    TEST_F(SceneNodeFilesTest, CustomLoaderIsAsked) {
        eastl::vector<eastl::string> asked;
        const NodeFileLoader loader = [&asked](eastl::string_view ref, pugi::xml_document& out, eastl::string& error) {
            asked.emplace_back(ref);
            if (ref == "known") {
                return static_cast<bool>(out.load_string(R"(<node name="Known"/>)"));
            }
            error = "unknown";
            return false;
        };
        pugi::xml_document doc = Parse(SceneConfigWith(R"(<node ref="known"/><node ref="other"/>)").c_str());

        const auto result = ExpandNodeReferences(doc.document_element(), loader);

        EXPECT_EQ(asked.size(), 2u);
        EXPECT_EQ(result.expanded, 1u);
        ASSERT_EQ(result.errors.size(), 1u);
        EXPECT_TRUE(Mentions(result.errors[0], "unknown"));
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"Known"}));
    }

    TEST_F(SceneNodeFilesTest, DirectoryLoaderResolvesASubFolder) {
        WriteFile(_dir / "deep" / "er" / "Leaf.xml", R"(<node name="Leaf"/>)");
        const NodeFileLoader loader = MakeDirectoryNodeLoader(_dir);

        pugi::xml_document out;
        eastl::string error;
        EXPECT_TRUE(loader("deep/er/Leaf", out, error)) << error.c_str();
        EXPECT_STREQ(out.document_element().attribute("name").as_string(), "Leaf");

        pugi::xml_document missing;
        EXPECT_FALSE(loader("deep/er/Nothing", missing, error));
        EXPECT_FALSE(error.empty());

        // An invalid reference is refused before any path is built
        error.clear();
        pugi::xml_document escaped;
        EXPECT_FALSE(loader("../Leaf", escaped, error));
        EXPECT_FALSE(error.empty());
    }

    TEST_F(SceneNodeFilesTest, FileSystemLoaderUsesTheContentLayers) {
        const auto engineRoot = _dir / "engineRoot";
        const auto gameRoot = _dir / "gameRoot";
        WriteFile(engineRoot / "config" / "nodes" / "Shared.xml", CurrentNode("FromEngine"));
        WriteFile(gameRoot / "config" / "nodes" / "Shared.xml", CurrentNode("FromGame"));
        WriteFile(engineRoot / "config" / "nodes" / "Only.xml", CurrentNode("OnlyEngine"));

        FileSystem fs;
        fs.AddContentRoot(engineRoot);
        fs.AddContentRoot(gameRoot);
        fs.Initialize();
        const NodeFileLoader loader = MakeFileSystemNodeLoader(fs);

        pugi::xml_document shared;
        pugi::xml_document only;
        eastl::string error;
        ASSERT_TRUE(loader("nodes/Shared", shared, error)) << error.c_str();
        ASSERT_TRUE(loader("nodes/Only", only, error)) << error.c_str();
        EXPECT_STREQ(shared.document_element().attribute("name").as_string(), "FromGame");
        EXPECT_STREQ(only.document_element().attribute("name").as_string(), "OnlyEngine");

        pugi::xml_document missing;
        EXPECT_FALSE(loader("nodes/Nothing", missing, error));
        EXPECT_FALSE(loader("../nodes/Only", missing, error));
    }

    TEST_F(SceneNodeFilesTest, LoadSceneDocumentExpandsNextToTheFile) {
        WriteFile(_dir / "SceneConfig.xml", SceneConfigWith(R"(<node name="A"/><node ref="nodes/Hud"/><node ref="nodes/B"/>)"));

        pugi::xml_document doc;
        const auto result = LoadSceneDocument(_dir / "SceneConfig.xml", doc);

        EXPECT_TRUE(result.Ok());
        EXPECT_EQ(result.expanded, 2u);
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "Hud", "B"}));
    }

    TEST_F(SceneNodeFilesTest, LoadSceneDocumentReportsABadReferenceAndKeepsTheScene) {
        WriteFile(_dir / "SceneConfig.xml", SceneConfigWith(R"(<node name="A"/><node ref="nodes/Nothing"/>)"));

        pugi::xml_document doc;
        const auto result = LoadSceneDocument(_dir / "SceneConfig.xml", doc);

        ExpectOneError(result, "nodes/Nothing");
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A"}));
    }

    TEST_F(SceneNodeFilesTest, LoadSceneDocumentOfAMissingFileIsAnError) {
        pugi::xml_document doc;
        const auto result = LoadSceneDocument(_dir / "NoSuchSceneConfig.xml", doc);

        EXPECT_FALSE(result.Ok());
        EXPECT_EQ(result.errors.size(), 1u);
        EXPECT_FALSE(doc.document_element());
    }

    TEST_F(SceneNodeFilesTest, LoadSceneDocumentOfABrokenFileIsAnError) {
        WriteFile(_dir / "Broken.xml", "<SceneManager><scenes>");

        pugi::xml_document doc;
        const auto result = LoadSceneDocument(_dir / "Broken.xml", doc);

        EXPECT_FALSE(result.Ok());
        EXPECT_EQ(result.errors.size(), 1u);
        EXPECT_FALSE(doc.document_element());
    }

    // ---- content version --------------------------------------------------------------------------------------------------

    /// The file-system loader checks the content version of the node file it reads: the reason names the file and the versions
    TEST_F(SceneNodeFilesTest, FileSystemLoaderRefusesAnOlderAndANewerNodeFile) {
        ASSERT_GT(kContentVersion, 0u);
        const auto root = _dir / "root";
        WriteFile(root / "config" / "nodes" / "Old.xml", "<node name=\"Old\" contentVersion=\"" + std::to_string(kContentVersion - 1) + "\"/>");
        WriteFile(root / "config" / "nodes" / "New.xml", "<node name=\"New\" contentVersion=\"" + std::to_string(kContentVersion + 1) + "\"/>");
        WriteFile(root / "config" / "nodes" / "Bad.xml", R"(<node name="Bad" contentVersion="two"/>)");
        WriteFile(root / "config" / "nodes" / "Now.xml", CurrentNode("Now"));
        FileSystem fs;
        fs.AddContentRoot(root);
        fs.Initialize();
        const NodeFileLoader loader = MakeFileSystemNodeLoader(fs);

        pugi::xml_document doc;
        eastl::string error;
        EXPECT_FALSE(loader("nodes/Old", doc, error));
        EXPECT_TRUE(Mentions(error, "config/nodes/Old.xml: content version")) << error.c_str();
        EXPECT_TRUE(Mentions(error, "migrate.py")) << error.c_str();
        error.clear();
        EXPECT_FALSE(loader("nodes/New", doc, error));
        EXPECT_TRUE(Mentions(error, "newer than this engine reads")) << error.c_str();
        error.clear();
        EXPECT_FALSE(loader("nodes/Bad", doc, error));
        EXPECT_TRUE(Mentions(error, "is not a whole number")) << error.c_str();
        error.clear();
        EXPECT_TRUE(loader("nodes/Now", doc, error)) << error.c_str();
    }

    TEST_F(SceneNodeFilesTest, AnOlderNodeFileIsADroppedReferenceInTheExpansion) {
        ASSERT_GT(kContentVersion, 0u);
        const auto root = _dir / "root";
        WriteFile(root / "config" / "nodes" / "Old.xml", "<node name=\"Old\" contentVersion=\"" + std::to_string(kContentVersion - 1) + "\"/>");
        WriteFile(root / "config" / "nodes" / "Now.xml", CurrentNode("Now"));
        FileSystem fs;
        fs.AddContentRoot(root);
        fs.Initialize();

        pugi::xml_document doc;
        ASSERT_TRUE(doc.load_string(SceneConfigWith(R"(<node ref="nodes/Now"/><node ref="nodes/Old"/><node name="Inline"/>)").c_str()));
        const NodeExpansion result = ExpandNodeReferences(doc.document_element(), MakeFileSystemNodeLoader(fs));

        EXPECT_EQ(result.expanded, 1u);
        ASSERT_EQ(result.errors.size(), 1u);
        EXPECT_TRUE(Mentions(result.errors[0], "'Main'")) << result.errors[0].c_str();
        EXPECT_TRUE(Mentions(result.errors[0], "nodes/Old")) << result.errors[0].c_str();
        EXPECT_TRUE(Mentions(result.errors[0], "content version")) << result.errors[0].c_str();
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"Now", "Inline"}));
    }

    TEST_F(SceneNodeFilesTest, DirectoryLoaderReadsAnyVersion) {
        WriteFile(_dir / "nodes" / "Zero.xml", R"(<node name="Zero" contentVersion="0"/>)");
        WriteFile(_dir / "nodes" / "Far.xml", R"(<node name="Far" contentVersion="999"/>)");
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node ref="nodes/Zero"/><node ref="nodes/Far"/>)"), doc);

        EXPECT_TRUE(result.Ok());
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"Zero", "Far"}));
    }

    TEST_F(SceneNodeFilesTest, TheExpandedCopyHasNoContentVersion) {
        WriteFile(_dir / "nodes" / "Stamped.xml", R"(<node name="Stamped" layer="3" contentVersion="2"/>)");
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node ref="nodes/Stamped"/>)"), doc);

        ASSERT_TRUE(result.Ok());
        const pugi::xml_node node = doc.document_element().child("scenes").child("scene").child("nodes").child("node");
        EXPECT_STREQ(node.attribute("name").as_string(), "Stamped");
        EXPECT_STREQ(node.attribute("layer").as_string(), "3");
        EXPECT_FALSE(node.attribute("contentVersion"));
    }

    // ---- overrides ---------------------------------------------------------------------------------------------------------

    TEST_F(SceneNodeFilesTest, NameAndLayerOverrideTheNodes) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node ref="nodes/Hud" name="Left" layer="3"/><node ref="nodes/B" layer="5"/>)"), doc);

        ASSERT_TRUE(result.Ok()) << (result.errors.empty() ? "" : result.errors[0].c_str());
        EXPECT_EQ(result.expanded, 2u);
        const pugi::xml_node hud = FirstNode(doc);
        EXPECT_STREQ(hud.attribute("name").as_string(), "Left");
        EXPECT_STREQ(hud.attribute("layer").as_string(), "3");
        EXPECT_FALSE(hud.attribute("ref"));
        // A node file without a layer gets one
        const pugi::xml_node plain = hud.next_sibling("node");
        EXPECT_STREQ(plain.attribute("name").as_string(), "B");
        EXPECT_STREQ(plain.attribute("layer").as_string(), "5");
    }

    TEST_F(SceneNodeFilesTest, NameOnlyKeepsTheLayerOfTheFile) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node ref="nodes/Hud" name="Left"/>)"), doc);

        ASSERT_TRUE(result.Ok());
        EXPECT_STREQ(FirstNode(doc).attribute("name").as_string(), "Left");
        EXPECT_STREQ(FirstNode(doc).attribute("layer").as_string(), "10");
    }

    TEST_F(SceneNodeFilesTest, ComponentAttributeOverrideChangesOneValueAndAddsNewOnes) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node ref="nodes/Hud"><components>
  <component type="TransformComponent" x="40" rotation="15"/>
</components></node>)"),
                                   doc);

        ASSERT_TRUE(result.Ok());
        const pugi::xml_node transform = ComponentOf(FirstNode(doc), "TransformComponent");
        EXPECT_STREQ(transform.attribute("x").as_string(), "40");
        EXPECT_STREQ(transform.attribute("y").as_string(), "2");
        EXPECT_STREQ(transform.attribute("width").as_string(), "3");
        EXPECT_STREQ(transform.attribute("height").as_string(), "4");
        EXPECT_STREQ(transform.attribute("rotation").as_string(), "15");
        EXPECT_STREQ(transform.attribute("type").as_string(), "TransformComponent");
    }

    TEST_F(SceneNodeFilesTest, ChildElementOverrideReplacesTheFilesElementWhole) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node ref="nodes/Hud"><components>
  <component type="QuadRendererComponent"><color r="10" g="20"/></component>
</components></node>)"),
                                   doc);

        ASSERT_TRUE(result.Ok());
        const pugi::xml_node quad = ComponentOf(FirstNode(doc), "QuadRendererComponent");
        EXPECT_EQ(Count(quad, "color"), 1u);
        const pugi::xml_node color = quad.child("color");
        EXPECT_STREQ(color.attribute("r").as_string(), "10");
        EXPECT_STREQ(color.attribute("g").as_string(), "20");
        EXPECT_FALSE(color.attribute("b"));
        EXPECT_FALSE(color.attribute("a"));
    }

    /// A field is overridden whole in either spelling: exactly one `color` is left, in the spelling of the override
    TEST_F(SceneNodeFilesTest, AFieldIsReplacedWholeInEitherSpelling) {
        WriteFile(_dir / "nodes" / "AsAttribute.xml", R"(<node name="N"><components>
  <component type="QuadRendererComponent" color="1,2,3,4" other="x"/>
</components></node>)");
        pugi::xml_document doc;
        ASSERT_TRUE(Expand(SceneConfigWith(R"(<node ref="nodes/AsAttribute"><components>
  <component type="QuadRendererComponent"><color r="9" g="8"/></component>
</components></node><node ref="nodes/Hud"><components>
  <component type="QuadRendererComponent" color="5,6,7,8"/>
</components></node>)"),
                           doc)
                        .Ok());

        // An element over a file's attribute
        const pugi::xml_node first = ComponentOf(FirstNode(doc), "QuadRendererComponent");
        EXPECT_FALSE(first.attribute("color"));
        EXPECT_EQ(Count(first, "color"), 1u);
        EXPECT_STREQ(first.child("color").attribute("r").as_string(), "9");
        EXPECT_STREQ(first.attribute("other").as_string(), "x");

        // An attribute over a file's element
        const pugi::xml_node second = ComponentOf(FirstNode(doc).next_sibling("node"), "QuadRendererComponent");
        EXPECT_STREQ(second.attribute("color").as_string(), "5,6,7,8");
        EXPECT_EQ(Count(second, "color"), 0u);
    }

    TEST_F(SceneNodeFilesTest, ElementOverrideKeepsThePlaceOfTheFilesElementAndAppendsAMissingOne) {
        WriteFile(_dir / "nodes" / "Canvas.xml", R"(<node name="Menu"><components>
  <component type="UICanvasComponent" scale="1">
    <before/>
    <elements><element name="Play"/><element name="Quit"/></elements>
    <after/>
  </component>
</components></node>)");
        pugi::xml_document doc;
        ASSERT_TRUE(Expand(SceneConfigWith(R"(<node ref="nodes/Canvas"><components>
  <component type="UICanvasComponent"><elements><element name="Only"/></elements><extra v="1"/></component>
</components></node>)"),
                           doc)
                        .Ok());

        const pugi::xml_node canvas = ComponentOf(FirstNode(doc), "UICanvasComponent");
        // The whole list is replaced, not merged by name
        ASSERT_EQ(Count(canvas.child("elements"), "element"), 1u);
        EXPECT_STREQ(canvas.child("elements").child("element").attribute("name").as_string(), "Only");
        EXPECT_STREQ(canvas.attribute("scale").as_string(), "1");
        // Same position: between `before` and `after`; the new one is appended
        EXPECT_STREQ(canvas.first_child().name(), "before");
        EXPECT_STREQ(canvas.first_child().next_sibling().name(), "elements");
        EXPECT_STREQ(canvas.first_child().next_sibling().next_sibling().name(), "after");
        EXPECT_STREQ(canvas.last_child().name(), "extra");
    }

    TEST_F(SceneNodeFilesTest, InstancesOfOneFileDoNotInfluenceEachOther) {
        const std::string before = ReadFile(_dir / "nodes" / "Hud.xml");
        pugi::xml_document doc;
        ASSERT_TRUE(Expand(SceneConfigWith(R"(<node ref="nodes/Hud" name="One"><components>
  <component type="TransformComponent" x="100"/>
</components></node><node ref="nodes/Hud" name="Two" layer="4"><components>
  <component type="TransformComponent" y="200"/><component type="QuadRendererComponent"><color r="1"/></component>
</components></node><node ref="nodes/Hud"/>)"),
                           doc)
                        .Ok());

        const pugi::xml_node one = FirstNode(doc);
        const pugi::xml_node two = one.next_sibling("node");
        const pugi::xml_node three = two.next_sibling("node");
        EXPECT_STREQ(ComponentOf(one, "TransformComponent").attribute("x").as_string(), "100");
        EXPECT_STREQ(ComponentOf(one, "TransformComponent").attribute("y").as_string(), "2");
        EXPECT_STREQ(one.attribute("layer").as_string(), "10");
        EXPECT_STREQ(ComponentOf(two, "TransformComponent").attribute("x").as_string(), "1");
        EXPECT_STREQ(ComponentOf(two, "TransformComponent").attribute("y").as_string(), "200");
        EXPECT_STREQ(two.attribute("layer").as_string(), "4");
        EXPECT_STREQ(ComponentOf(one, "QuadRendererComponent").child("color").attribute("r").as_string(), "9");
        // The third is the file as it is
        pugi::xml_document plain;
        ASSERT_TRUE(plain.load_string(kHudNode));
        EXPECT_EQ(Saved(three), Saved(plain.document_element()));
        EXPECT_EQ(ReadFile(_dir / "nodes" / "Hud.xml"), before);
    }

    TEST_F(SceneNodeFilesTest, OverridesOfTwoComponentsOfOneReferenceAndTheRestIsTheFiles) {
        pugi::xml_document doc;
        ASSERT_TRUE(Expand(SceneConfigWith(R"(<node name="A"/><node ref="nodes/Hud" name="H"><components>
  <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="4"/></component>
  <component type="TransformComponent" width="30"/>
</components></node><node name="C"/>)"),
                           doc)
                        .Ok());

        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "H", "C"}));
        const pugi::xml_node hud = FirstNode(doc).next_sibling("node");
        EXPECT_STREQ(ComponentOf(hud, "TransformComponent").attribute("width").as_string(), "30");
        EXPECT_STREQ(ComponentOf(hud, "QuadRendererComponent").child("color").attribute("a").as_string(), "4");
        // The order of the components is the file's, not the reference's
        EXPECT_STREQ(hud.child("components").first_child().attribute("type").as_string(), "TransformComponent");

        // A node of the file that is not overridden is saved as the file has it
        WriteFile(_dir / "nodes" / "Two.xml", R"(<node name="T"><components><component type="TransformComponent" x="1"/><component type="QuadRendererComponent" size="3"/></components></node>)");
        pugi::xml_document second;
        ASSERT_TRUE(Expand(SceneConfigWith(R"(<node ref="nodes/Two"><components><component type="TransformComponent" x="9"/></components></node>)"), second).Ok());
        pugi::xml_document file;
        ASSERT_TRUE(file.load_file((_dir / "nodes" / "Two.xml").c_str()));
        EXPECT_EQ(Saved(ComponentOf(FirstNode(second), "QuadRendererComponent")), Saved(ComponentOf(file.document_element(), "QuadRendererComponent")));
    }

    TEST_F(SceneNodeFilesTest, EmptyComponentsAndNoAttributesIsAPlainReference) {
        pugi::xml_document plain;
        ASSERT_TRUE(Expand(SceneConfigWith(R"(<node ref="nodes/Hud"/>)"), plain).Ok());
        pugi::xml_document empty;
        ASSERT_TRUE(Expand(SceneConfigWith(R"(<node ref="nodes/Hud"><components/></node>)"), empty).Ok());
        pugi::xml_document commented;
        ASSERT_TRUE(Expand(SceneConfigWith(R"(<node ref="nodes/Hud"> text <!-- a comment --><components> <!-- c --> </components></node>)"), commented).Ok());

        EXPECT_EQ(Saved(FirstNode(empty)), Saved(FirstNode(plain)));
        EXPECT_EQ(Saved(FirstNode(commented)), Saved(FirstNode(plain)));
    }

    /// Every way an override is invalid: one error naming the scene and the reference, the reference dropped, the neighbours kept
    TEST_F(SceneNodeFilesTest, AnInvalidOverrideDropsTheWholeReference) {
        for (const char* reference : kInvalidReferences) {
            SCOPED_TRACE(reference);
            pugi::xml_document doc;
            const auto result = Expand(SceneConfigWith(std::string(R"(<node name="A"/>)") + reference + R"(<node name="C"/>)"), doc);

            ExpectOneError(result, "nodes/Hud");
            EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "C"}));
        }
    }

    TEST_F(SceneNodeFilesTest, AnUnknownComponentNamesTheTypeAndAppliesNothingBeforeIt) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node ref="nodes/Hud"><components>
  <component type="TransformComponent" x="99"/>
  <component type="NoSuchComponent" x="1"/>
</components></node>)"),
                                   doc);

        ExpectOneError(result, "nodes/Hud");
        EXPECT_TRUE(Mentions(result.errors[0], "no component 'NoSuchComponent'")) << result.errors[0].c_str();
        EXPECT_TRUE(NodeNames(doc.document_element(), "Main").empty());
    }

    TEST_F(SceneNodeFilesTest, ApplyNodeOverridesMergesIntoTheNode) {
        pugi::xml_document file = Parse(kHudNode);
        pugi::xml_document reference = Parse(R"(<node ref="nodes/Hud" name="Right" layer="2"><components>
  <component type="TransformComponent" x="7"/>
</components></node>)");
        eastl::string error;

        ASSERT_TRUE(ApplyNodeOverrides(file.document_element(), reference.document_element(), error)) << error.c_str();

        EXPECT_TRUE(error.empty());
        EXPECT_STREQ(file.document_element().attribute("name").as_string(), "Right");
        EXPECT_STREQ(file.document_element().attribute("layer").as_string(), "2");
        EXPECT_STREQ(ComponentOf(file.document_element(), "TransformComponent").attribute("x").as_string(), "7");
        // The reference itself is not touched
        EXPECT_STREQ(reference.document_element().attribute("ref").as_string(), "nodes/Hud");
    }

    /// False and the node as it was, for every invalid reference, also when a valid component comes before the bad one
    TEST_F(SceneNodeFilesTest, ApplyNodeOverridesLeavesTheNodeUntouchedOnAnError) {
        for (const char* text : kInvalidReferences) {
            SCOPED_TRACE(text);
            pugi::xml_document file = Parse(kHudNode);
            const std::string before = Saved(file);
            pugi::xml_document reference = Parse(text);
            eastl::string error;

            EXPECT_FALSE(ApplyNodeOverrides(file.document_element(), reference.document_element(), error));
            EXPECT_FALSE(error.empty());
            EXPECT_EQ(Saved(file), before);
        }

        pugi::xml_document file = Parse(kHudNode);
        const std::string before = Saved(file);
        pugi::xml_document reference = Parse(R"(<node ref="nodes/Hud" name="Late" layer="1"><components>
  <component type="TransformComponent" x="99"/><component type="Nothing"/>
</components></node>)");
        eastl::string error;
        EXPECT_FALSE(ApplyNodeOverrides(file.document_element(), reference.document_element(), error));
        EXPECT_EQ(Saved(file), before);
    }

    TEST_F(SceneNodeFilesTest, NodeFileWithoutComponentsHasNoComponentToOverride) {
        pugi::xml_document file = Parse(R"(<node name="Bare"/>)");
        pugi::xml_document reference = Parse(R"(<node ref="x"><components><component type="TransformComponent" x="1"/></components></node>)");
        eastl::string error;

        EXPECT_FALSE(ApplyNodeOverrides(file.document_element(), reference.document_element(), error));
        EXPECT_TRUE(Mentions(error, "no component 'TransformComponent'")) << error.c_str();
    }

    TEST_F(SceneNodeFilesTest, LoadSceneDocumentAppliesOverridesNextToTheFile) {
        WriteFile(_dir / "SceneConfig.xml", SceneConfigWith(R"(<node name="A"/><node ref="nodes/Hud" name="Left" layer="1"><components>
  <component type="TransformComponent" x="40"/>
</components></node><node ref="nodes/Hud" name="Right"/>)"));

        pugi::xml_document doc;
        const auto result = LoadSceneDocument(_dir / "SceneConfig.xml", doc);

        EXPECT_TRUE(result.Ok());
        EXPECT_EQ(result.expanded, 2u);
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "Left", "Right"}));
        const pugi::xml_node left = FirstNode(doc).next_sibling("node");
        EXPECT_STREQ(left.attribute("layer").as_string(), "1");
        EXPECT_STREQ(ComponentOf(left, "TransformComponent").attribute("x").as_string(), "40");
        EXPECT_STREQ(ComponentOf(left.next_sibling("node"), "TransformComponent").attribute("x").as_string(), "1");
    }

}  // namespace BECore::Tests
