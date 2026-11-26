// The control methods that reach the nodes of a scene by path: `scene_tree` and `scene_get`, over a scene loaded from a string.

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneControl.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodePath.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Namesakes, names that need escapes, a node with no name, a component listed twice by the file and a component of nested fields
        constexpr StringView kNodes = R"(
<node name="Board" layer="2"><components>
  <component type="TransformComponent" x="1" y="2" width="3" height="4"/>
  <component type="QuadRendererComponent"><color r="10" g="20" b="30" a="255"/></component>
</components><children>
  <node name="Cell"/>
  <node name="Other"/>
  <node name="Cell" layer="-1"><children><node name="Gem"/></children></node>
</children></node>
<node name="a/b"><children><node name="c[1]"/><node name="back\slash"/><node name="c[1]"/></children></node>
<node name=""><children><node name="Under"/></children></node>
<node name="Two"><components>
  <component type="TransformComponent" x="5"/>
  <component type="TransformComponent" x="6"/>
</components></node>)";

        class SceneControlTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                XmlDeserializer deserializer;
                const String xml = SceneConfigText(kNodes);
                ASSERT_TRUE(deserializer.LoadFromString(xml.c_str()));
                _manager = New<SceneManager>();
                _manager->Deserialize(deserializer);
                ASSERT_FALSE(deserializer.HasErrors());
                _manager->Initialize();
                _scene = _manager->GetSceneByName("Main"_intern);
                ASSERT_NE(_scene, nullptr);
                RegisterSceneControl(_service, [this] { return _active; });
                _active = _scene;
            }

            ControlReply Call(StringView method, StringView params = {}) {
                return CallControlText(_service, 1, method, params);
            }

            /// The request of `scene_get` for @p node and @p component; @p rest is more members, with the leading comma
            ControlReply Get(StringView node, StringView component, StringView rest = {}) {
                String params;
                JsonWriter writer(params);
                writer.BeginObject();
                writer.Member("node", node);
                writer.Member("component", component);
                writer.EndObject();
                params.pop_back();
                params.append(rest.data(), rest.size());
                params.push_back('}');
                return Call("scene_get", params);
            }

            /// The `path` of every node `scene_tree` lists for @p params, in its order
            Vector<String> TreePaths(StringView params = {}) {
                const ControlReply reply = Call("scene_tree", params);
                EXPECT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
                Vector<String> paths;
                for (const JsonView node : MemberOf(reply.Result(), "nodes").Items()) {
                    paths.push_back(StringOf(node, "path"));
                }
                return paths;
            }

            SceneNode& Node(StringView path) {
                const auto found = FindSceneNodeByPath(_scene->GetRootNode(), path);
                return Require(found.has_value() ? *found : nullptr);
            }

            ControlService _service;
            IntrusivePtr<SceneManager> _manager;
            Scene* _scene = nullptr;
            Scene* _active = nullptr;  // what the provider gives
        };

    }  // namespace

    TEST_F(SceneControlTest, TheTreeListsEveryNodeDepthFirstWithItsLayerAndComponents) {
        const ControlReply reply = Call("scene_tree");

        ASSERT_TRUE(reply.IsOk()) << reply.ErrorMessage().c_str();
        EXPECT_EQ(StringOf(reply.Result(), "scene"), "Main");
        EXPECT_EQ(BoolOf(reply.Result(), "truncated"), false);
        EXPECT_EQ(TreePaths(), (Vector<String>{"Board", "Board/Cell", "Board/Other", "Board/Cell[1]", "Board/Cell[1]/Gem", R"(a\/b)", R"(a\/b/c\[1])", R"(a\/b/back\\slash)", R"(a\/b/c\[1][1])", "",
                                               "/Under", "Two"}));
        const auto nodes = MemberOf(reply.Result(), "nodes").Items();
        ASSERT_TRUE(nodes.begin() != nodes.end());
        const JsonView first = *nodes.begin();
        EXPECT_EQ(StringOf(first, "name"), "Board");
        EXPECT_EQ(IntOf(first, "layer"), 2);
        EXPECT_EQ(RawOf(first, "components"), R"(["TransformComponent","QuadRendererComponent"])");
    }

    TEST_F(SceneControlTest, EveryPathOfTheTreeReadsBackToItsNode) {
        const Vector<String> paths = TreePaths();

        // The tree is listed depth first: the same walk of the scene gives the node each path has to name
        Vector<const SceneNode*> nodes;
        const auto collect = [&nodes](const auto& self, const SceneNode& node) -> void {
            for (const auto& child : node.GetChildren()) {
                nodes.push_back(child.Get());
                self(self, *child);
            }
        };
        collect(collect, _scene->GetRootNode());
        ASSERT_EQ(paths.size(), nodes.size());
        for (size_t i = 0; i < paths.size(); ++i) {
            EXPECT_EQ(&Node(paths[i]), nodes[i]) << paths[i].c_str();
        }
    }

    TEST_F(SceneControlTest, ANodeNarrowsTheTreeToItAndItsDescendantsWithFullPaths) {
        // `[0]` names the first namesake, which the listing writes without it
        EXPECT_EQ(TreePaths(R"({"node":"Board[0]/Cell[1]"})"), (Vector<String>{"Board/Cell[1]", "Board/Cell[1]/Gem"}));
    }

    TEST_F(SceneControlTest, TheLimitCutsTheTreeAndSaysSo) {
        const ControlReply cut = Call("scene_tree", R"({"limit":3})");
        const ControlReply whole = Call("scene_tree", R"({"node":"Board","limit":5})");

        EXPECT_EQ(MemberOf(cut.Result(), "nodes").Size(), 3u);
        EXPECT_EQ(BoolOf(cut.Result(), "truncated"), true);
        EXPECT_EQ(MemberOf(whole.Result(), "nodes").Size(), 5u);
        EXPECT_EQ(BoolOf(whole.Result(), "truncated"), false);
    }

    TEST_F(SceneControlTest, ANodeNoPathCanReadBackIsLeftOutAndTheTreeIsTruncated) {
        SceneNode* parent = &Node("Two");
        for (size_t depth = 1; depth <= SceneNodePathLimits::kMaxSegments; ++depth) {
            parent = _scene->CreateNode("d"_intern, parent).Get();
        }

        const ControlReply reply = Call("scene_tree", R"({"node":"Two"})");

        EXPECT_EQ(MemberOf(reply.Result(), "nodes").Size(), SceneNodePathLimits::kMaxSegments);
        EXPECT_EQ(BoolOf(reply.Result(), "truncated"), true);
    }

    TEST_F(SceneControlTest, GetReadsAllFieldsOrOneByItsPath) {
        const ControlReply all = Get("Board", "QuadRendererComponent");
        const ControlReply nested = Get("Board", "QuadRendererComponent", R"(,"path":"color.g")");

        ASSERT_TRUE(all.IsOk()) << all.ErrorMessage().c_str();
        EXPECT_EQ(StringOf(all.Result(), "node"), "Board");
        EXPECT_EQ(StringOf(all.Result(), "component"), "QuadRendererComponent");
        EXPECT_EQ(IntOf(MemberOf(MemberOf(all.Result(), "fields"), "color"), "g"), 20);
        EXPECT_EQ(nested.Text(), R"({"id":1,"ok":true,"result":{"component":"QuadRendererComponent","node":"Board","path":"color.g","value":20}})");
    }

    TEST_F(SceneControlTest, TheIndexPicksAmongTheComponentsOfOneType) {
        EXPECT_EQ(NumberOf(Get("Two", "TransformComponent", R"(,"path":"x")").Result(), "value"), 5.0);
        EXPECT_EQ(NumberOf(Get("Two", "TransformComponent", R"(,"index":1,"path":"x")").Result(), "value"), 6.0);
        EXPECT_EQ(Get("Two", "TransformComponent", R"(,"index":2)").ErrorCode(), "component_not_found");
    }

    TEST_F(SceneControlTest, ARequestThatNamesNothingIsRefusedWithItsCode) {
        EXPECT_EQ(Get("Nowhere", "TransformComponent").ErrorCode(), "node_not_found");
        EXPECT_EQ(Get("Board/Cell[2]", "TransformComponent").ErrorCode(), "node_not_found");
        EXPECT_EQ(Call("scene_tree", R"({"node":"Nowhere"})").ErrorCode(), "node_not_found");
        EXPECT_EQ(Get("Board", "SpriteRendererComponent").ErrorCode(), "component_not_found");
        EXPECT_EQ(Get("Board", "TransformComponent", R"(,"path":"z")").ErrorCode(), "not_found");
        EXPECT_EQ(Get("Board", "TransformComponent", R"(,"path":"x..y")").ErrorCode(), "invalid_path");
        EXPECT_EQ(Get("Board", "QuadRendererComponent", R"(,"path":"color")").ErrorCode(), "not_a_value");
    }

    TEST_F(SceneControlTest, ParamsOfTheWrongShapeAreInvalid) {
        const String longPath(SceneNodePathLimits::kMaxPathBytes + 1, 'a');
        String deepPath = "a";
        for (size_t i = 0; i < SceneNodePathLimits::kMaxSegments; ++i) {
            deepPath += "/a";
        }
        for (const StringView node : {StringView(R"(Board\)"), StringView(R"(Board\x)"), StringView("Board[99999999999]"), StringView(longPath), StringView(deepPath)}) {
            EXPECT_EQ(Get(node, "TransformComponent").ErrorCode(), "invalid_params") << String(node).c_str();
        }
        EXPECT_EQ(Call("scene_get", R"({"component":"TransformComponent"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("scene_get", R"({"node":"Board"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("scene_get", R"({"node":1,"component":"TransformComponent"})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Get("Board", "TransformComponent", R"(,"index":-1)").ErrorCode(), "invalid_params");
        EXPECT_EQ(Get("Board", "TransformComponent", R"(,"path":3)").ErrorCode(), "invalid_params");
        for (const StringView params : {StringView(R"({"limit":0})"), StringView(R"({"limit":1.5})"), StringView(R"({"node":false})")}) {
            EXPECT_EQ(Call("scene_tree", params).ErrorCode(), "invalid_params") << String(params).c_str();
        }
        EXPECT_EQ(Call("scene_tree", Format("{{\"limit\":{}}}", SceneControlLimits::kMaxTreeNodes + 1)).ErrorCode(), "invalid_params");
    }

    TEST_F(SceneControlTest, WithNoActiveSceneTheMethodsSaySo) {
        _active = nullptr;

        EXPECT_EQ(Call("scene_tree").ErrorCode(), "no_scene");
        EXPECT_EQ(Get("Board", "TransformComponent").ErrorCode(), "no_scene");
    }

    TEST_F(SceneControlTest, NoTextOfARequestIsInterned) {
        const String node = FreshText("sceneNode");
        const String component = FreshText("sceneComponent");
        const String field = FreshText("sceneField");

        EXPECT_EQ(Call("scene_tree", Format("{{\"node\":\"{}\"}}", node)).ErrorCode(), "node_not_found");
        EXPECT_EQ(Get(node, component).ErrorCode(), "node_not_found");
        EXPECT_EQ(Get("Board", component).ErrorCode(), "component_not_found");
        EXPECT_EQ(Get("Board", "TransformComponent", Format(",\"path\":\"{}\"", field)).ErrorCode(), "not_found");

        for (const String& text : {node, component, field}) {
            EXPECT_FALSE(PoolString::Find(StringView(text)).has_value()) << text.c_str();
        }
    }

}  // namespace BECore::Tests
