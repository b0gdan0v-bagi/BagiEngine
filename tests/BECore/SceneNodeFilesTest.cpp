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

    TEST_F(SceneNodeFilesTest, ReferenceWithAnExtraAttributeIsAnError) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node name="A"/><node ref="nodes/B" name="Other"/><node name="C"/>)"), doc);

        ExpectOneError(result, "nodes/B");
        EXPECT_TRUE(Mentions(result.errors[0], "'name'")) << result.errors[0].c_str();
        EXPECT_EQ(NodeNames(doc.document_element(), "Main"), (std::vector<std::string>{"A", "C"}));
    }

    TEST_F(SceneNodeFilesTest, ReferenceWithAChildElementIsAnError) {
        pugi::xml_document doc;
        const auto result = Expand(SceneConfigWith(R"(<node name="A"/><node ref="nodes/B"><components/></node><node name="C"/>)"), doc);

        ExpectOneError(result, "nodes/B");
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
        WriteFile(engineRoot / "config" / "nodes" / "Shared.xml", R"(<node name="FromEngine"/>)");
        WriteFile(gameRoot / "config" / "nodes" / "Shared.xml", R"(<node name="FromGame"/>)");
        WriteFile(engineRoot / "config" / "nodes" / "Only.xml", R"(<node name="OnlyEngine"/>)");

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

}  // namespace BECore::Tests
