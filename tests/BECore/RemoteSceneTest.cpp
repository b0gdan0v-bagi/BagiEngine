// The data the editor makes of another program's `schema` and `scene_tree`, from the answers of the real methods and from hostile ones.

#include <BECore/Control/ReflectionControl.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneControl.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodePath.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Widgets/RemoteEditor/RemoteSchema.h>
#include <Widgets/RemoteEditor/RemoteTree.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Names that need escapes (one ends in a backslash, so the next separator follows two of them), a node with no name and namesakes
        constexpr StringView kNodes = R"(
<node name="Board" layer="2"><components>
  <component type="TransformComponent"/>
  <component type="QuadRendererComponent"/>
</components><children>
  <node name="Cell"/>
  <node name="Cell" layer="-1"><children><node name="Gem"/></children></node>
</children></node>
<node name="a/b"><children><node name="end\"><children><node name="c[1]"/></children></node></children></node>
<node name=""><children><node name="Under"/></children></node>
<node name="Two"><components>
  <component type="TransformComponent"/>
  <component type="TransformComponent"/>
</components></node>)";

        class RemoteSceneTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                CoreManager::GetRendererManager().SetRenderer(New<RecordingRenderer>());
                XmlDeserializer deserializer;
                const String xml = SceneConfigText(kNodes);
                ASSERT_TRUE(deserializer.LoadFromString(xml.c_str()));
                _manager = New<SceneManager>();
                _manager->Deserialize(deserializer);
                ASSERT_FALSE(deserializer.HasErrors());
                _manager->Initialize();
                _scene = _manager->GetSceneByName("Main"_intern);
                ASSERT_NE(_scene, nullptr);
                RegisterSceneControl(_service, [this] { return _scene; });
                RegisterReflectionControl(_service);
            }

            void TearDown() override {
                _manager.Reset();
                CoreManager::GetRendererManager().SetRenderer({});
            }

            /// The text of the result of @p method
            String Result(StringView method, StringView params = {}) {
                const ControlReply reply = CallControlText(_service, 1, method, params);
                EXPECT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
                return String(reply.Result().Raw());
            }

            ControlService _service;
            IntrusivePtr<SceneManager> _manager;
            Scene* _scene = nullptr;
        };

        /// Each node as `path|name|layer|components`, its children one level deeper, in the order the tree holds them
        void Describe(const RemoteTree& tree, const Vector<uint32_t>& nodes, size_t depth, Vector<String>& out) {
            for (const uint32_t index : nodes) {
                const RemoteTreeNode& node = tree.GetNodes()[index];
                String line(depth, '.');
                AppendFormat(line, "{}|{}|{}|", node.path, node.name, node.layer);
                for (const String& component : node.components) {
                    AppendFormat(line, "{};", component);
                }
                out.push_back(std::move(line));
                Describe(tree, node.children, depth + 1, out);
            }
        }

        Vector<String> Describe(const RemoteTree& tree) {
            Vector<String> out;
            Describe(tree, tree.GetTopNodes(), 0, out);
            return out;
        }

    }  // namespace

    TEST_F(RemoteSceneTest, TheTreeHasTheNodesOfTheSceneUnderTheirParents) {
        RemoteTree tree;

        ASSERT_TRUE(tree.Load(Result("scene_tree")));

        const Vector<String> expected{"Board|Board|2|TransformComponent;QuadRendererComponent;",
                                      ".Board/Cell|Cell|0|",
                                      ".Board/Cell[1]|Cell|-1|",
                                      "..Board/Cell[1]/Gem|Gem|0|",
                                      R"(a\/b|a/b|0|)",
                                      R"(.a\/b/end\\|end\|0|)",
                                      R"(..a\/b/end\\/c\[1]|c[1]|0|)",
                                      "||0|",
                                      "./Under|Under|0|",
                                      "Two|Two|0|TransformComponent;TransformComponent;"};
        EXPECT_EQ(Describe(tree), expected);
        EXPECT_EQ(tree.GetSceneName(), "Main");
        EXPECT_FALSE(tree.IsTruncated());
        // Every path the tree holds names the node of that name in the scene
        for (const RemoteTreeNode& node : tree.GetNodes()) {
            const auto found = FindSceneNodeByPath(_scene->GetRootNode(), node.path);
            ASSERT_TRUE(found.has_value() && *found != nullptr) << node.path.c_str();
            EXPECT_EQ((*found)->GetName().ToStringView(), StringView(node.name));
        }
        ASSERT_NE(tree.Find("Board/Cell[1]"), nullptr);
        EXPECT_EQ(tree.Find("Board/Cell[1]")->layer, -1);
        EXPECT_EQ(tree.Find("Board/None"), nullptr);
    }

    TEST_F(RemoteSceneTest, TheTreeIsRebuiltOnlyForAnotherTextAndSaysWhenItIsCut) {
        RemoteTree tree;
        const String whole = Result("scene_tree");
        ASSERT_TRUE(tree.Load(whole));
        const RemoteTreeNode* first = tree.GetNodes().data();

        EXPECT_FALSE(tree.Load(whole));
        EXPECT_EQ(tree.GetNodes().data(), first);

        ASSERT_TRUE(tree.Load(Result("scene_tree", R"({"limit":3})")));
        EXPECT_EQ(tree.GetNodes().size(), 3u);
        EXPECT_TRUE(tree.IsTruncated());
    }

    TEST_F(RemoteSceneTest, TheSchemaKnowsTheTypesTheirChainsAndTheirFactories) {
        RemoteSchema schema;
        const String text = Result("schema");

        ASSERT_TRUE(schema.Load(text));
        EXPECT_FALSE(schema.Load(text));

        const RemoteType* transform = schema.FindType("TransformComponent");
        ASSERT_NE(transform, nullptr);
        const RemoteTypeChain chain = schema.CollectChain(*transform);
        ASSERT_EQ(chain.size(), 2u);
        EXPECT_EQ(chain[0]->name, "IComponent");
        EXPECT_EQ(chain[1], transform);
        ASSERT_FALSE(transform->fields.empty());
        EXPECT_EQ(transform->fields[0].name, "x");
        EXPECT_EQ(transform->fields[0].type.kind, TypeKind::Float);
        EXPECT_TRUE(schema.IsKindOf(*transform, "IComponent"));
        EXPECT_FALSE(schema.IsKindOf(*transform, "UIElement"));
        const RemoteType* panel = schema.FindType("UIPanel");
        ASSERT_NE(panel, nullptr);
        EXPECT_TRUE(schema.IsKindOf(*panel, "UIElement"));
        EXPECT_EQ(schema.FindType("NoSuchType"), nullptr);
    }

    TEST_F(RemoteSceneTest, AHostileTreeLeavesOutWhatItCannotPlace) {
        RemoteTree tree;

        ASSERT_TRUE(
            tree.Load(R"({"nodes":[1,{},{"path":5},{"path":"Lost/Child"},{"path":"A","layer":99999999999,"name":7,"components":[1,"X"]},{"path":"B/C"},{"path":"A/D","layer":"1"}],"scene":[]})"));

        EXPECT_EQ(Describe(tree), (Vector<String>{"A||0|X;", ".A/D||0|"}));
        for (const StringView text : {"", "[", "null", "[]", R"({"nodes":{}})", R"({"nodes":"x","truncated":3})"}) {
            tree.Load(text);
            EXPECT_TRUE(tree.GetNodes().empty()) << String(text).c_str();
            EXPECT_TRUE(tree.GetTopNodes().empty());
        }

        String deep = R"({"nodes":[{"path":"n"})";
        String path = "n";
        for (size_t i = 0; i < 2 * SceneNodePathLimits::kMaxSegments; ++i) {
            path.append("/n");
            AppendFormat(deep, R"(,{{"path":"{}"}})", path);
        }
        deep.append("]}");
        ASSERT_TRUE(tree.Load(deep));
        EXPECT_EQ(tree.GetNodes().size(), SceneNodePathLimits::kMaxSegments);
    }

    TEST_F(RemoteSceneTest, AHostileSchemaEndsItsCyclesAndItsDepth) {
        RemoteSchema schema;

        ASSERT_TRUE(schema.Load(R"({"types":[3,{"fields":[]},{"name":"A","parent":"B","fields":[1,{"name":"f"},{"type":{}},{"name":"g","type":7,"attributes":[]}]},)"
                                R"({"name":"B","parent":"A","fields":{}},{"name":"A","parent":""},{"name":"Self","parent":"Self"}],"factories":[4,{"base":"A","derived":7}]})"));

        const RemoteType* a = schema.FindType("A");
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(a->parent, "B");
        EXPECT_EQ(schema.CollectChain(*a).size(), 2u);
        ASSERT_EQ(a->fields.size(), 1u);
        EXPECT_EQ(a->fields[0].type.kind, TypeKind::Unknown);
        EXPECT_EQ(schema.CollectChain(Require(schema.FindType("Self"))).size(), 1u);
        EXPECT_FALSE(schema.IsKindOf(*a, "C"));

        constexpr size_t kNesting = 100;
        String deep = R"({"types":[{"name":"D","fields":[{"name":"f","type":)";
        for (size_t i = 0; i < kNesting; ++i) {
            deep.append(R"({"kind":"list","element":)");
        }
        deep.append(R"({"kind":"bool"})");
        deep.append(kNesting, '}');
        deep.append("}]}]}");
        ASSERT_TRUE(schema.Load(deep));
        size_t depth = 0;
        for (const RemoteTypeDesc* type = &At(Require(schema.FindType("D")).fields, 0).type; type != nullptr; type = type->element.get()) {
            ++depth;
        }
        EXPECT_LE(depth, RemoteSchemaLimits::kMaxTypeDepth + 1);

        for (const StringView text : {"", "{", "[]", R"({"types":5,"factories":"x"})"}) {
            schema.Load(text);
            EXPECT_EQ(schema.FindType("D"), nullptr);
        }
    }

}  // namespace BECore::Tests
