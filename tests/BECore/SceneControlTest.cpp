// The control methods that reach the nodes of a scene by path: `scene_tree`, `scene_get` and `scene_set`, over a scene loaded from a string.

#include <BECore/Audio/AudioSourceComponent.h>
#include <BECore/AudioTestSupport.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneControl.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodePath.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Events/SceneEvents.h>
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
                Load(kNodes);
            }

            /// The scene `Main` of @p nodes, given to the registered methods
            void Load(StringView nodes) {
                XmlDeserializer deserializer;
                const String xml = SceneConfigText(nodes);
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

    namespace {

        constexpr const char* kAudioMount = "test_scene_control";
        constexpr StringView kClip = "test_scene_control/stereo-tone.ogg";

        // Fields of every kind, a component that keeps a loaded texture, one that keeps a loaded clip and a canvas
        constexpr StringView kWritableNodes = R"(
<node name="Thing"><components>
  <component type="TransformComponent" x="1" y="2" width="3" height="4"/>
  <component type="QuadRendererComponent"><color r="10" g="20" b="30" a="255"/></component>
  <component type="TextComponent" text="before"/>
  <component type="ParticleEmitterComponent"/>
  <component type="ClickableComponent" actionName="SceneControlKnown"/>
  <component type="SpriteRendererComponent"/>
  <component type="AudioSourceComponent" clip="test_scene_control/stereo-tone.ogg" volume="0.5"/>
  <component type="UICanvasComponent"><elements><element type="UIPanel" name="Plate"/></elements></component>
</components></node>)";

        class NodeEdits : public SubscriptionHolder {
        public:
            NodeEdits() {
                Subscribe<SceneEvents::SceneNodeEditedEvent, &NodeEdits::OnEdited>(this);
            }

            Vector<Pair<const Scene*, const SceneNode*>> edits;

        private:
            void OnEdited(const SceneEvents::SceneNodeEditedEvent& event) {
                edits.push_back({event.scene, event.node});
            }
        };

        class SceneControlSetTest : public SceneControlTest {
        protected:
            void SetUp() override {
                ASSERT_TRUE(CoreManager::GetFileSystem().Mount(PoolString::Intern(kAudioMount), AudioFixtureFolder()));
                CoreManager::GetRendererManager().SetRenderer(New<RecordingRenderer>());
                Load(kWritableNodes);
            }

            void TearDown() override {
                _manager.Reset();
                CoreManager::GetRendererManager().SetRenderer({});
                CoreManager::GetResourceManager().ClearCache();
                CoreManager::GetFileSystem().Unmount(PoolString::Intern(kAudioMount));
            }

            /// `scene_set` of `Thing`; @p value is a JSON value
            ControlReply Set(StringView component, StringView path, StringView value) {
                return Call("scene_set", Format(R"({{"node":"Thing","component":"{}","path":"{}","value":{}}})", component, path, value));
            }

            /// The text of the `value` that `scene_get` reads
            String Read(StringView component, StringView path) {
                return RawOf(Get("Thing", component, Format(",\"path\":\"{}\"", path)).Result(), "value");
            }

            template <typename T>
            T& Component() {
                return Require(Node("Thing").GetComponent<T>());
            }
        };

    }  // namespace

    TEST_F(SceneControlSetTest, EachKindOfValueIsWrittenAndAnsweredAsTheFieldHoldsIt) {
        struct Write {
            StringView component;
            StringView path;
            StringView value;
            StringView held;
        };
        for (const Write& write : {Write{"ParticleEmitterComponent", "playOnStart", "false", "false"}, Write{"ParticleEmitterComponent", "effect.burstCount", "-3", "-3"},
                                   Write{"ParticleEmitterComponent", "effect.seed", "4000000000", "4000000000"}, Write{"TransformComponent", "x", "-1.5", "-1.5"},
                                   Write{"QuadRendererComponent", "color.r", "200", "200"}, Write{"TextComponent", "text", R"("a text nothing has seen")", R"("a text nothing has seen")"},
                                   Write{"QuadRendererComponent", "blend", R"("Add")", R"("Add")"}, Write{"QuadRendererComponent", "blend", "2", R"("Multiply")"}}) {
            const ControlReply reply = Set(write.component, write.path, write.value);

            ASSERT_TRUE(reply.IsOk()) << String(write.path).c_str() << ": " << reply.ErrorMessage().c_str();
            EXPECT_EQ(RawOf(reply.Result(), "value"), write.held) << String(write.path).c_str();
            EXPECT_EQ(Read(write.component, write.path), write.held) << String(write.path).c_str();
        }
    }

    TEST_F(SceneControlSetTest, ARefusedWriteChangesNothingAndReportsNothing) {
        const NodeEdits edits;

        EXPECT_EQ(Set("AudioSourceComponent", "volume", "2").ErrorCode(), "out_of_range");
        EXPECT_EQ(Set("TransformComponent", "x", R"("wide")").ErrorCode(), "type_mismatch");
        EXPECT_EQ(Set("QuadRendererComponent", "color.r", "300").ErrorCode(), "out_of_range");
        EXPECT_EQ(Set("QuadRendererComponent", "color", "1").ErrorCode(), "not_a_value");
        EXPECT_EQ(Set("QuadRendererComponent", "colour", "1").ErrorCode(), "not_found");
        EXPECT_EQ(Set("TransformComponent", "x", "[1]").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("scene_set", R"({"node":"Thing","component":"TransformComponent","value":1})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("scene_set", R"({"node":"Thing","component":"TransformComponent","path":"x"})").ErrorCode(), "invalid_params");

        EXPECT_EQ(Read("AudioSourceComponent", "volume"), "0.5");
        EXPECT_EQ(Read("TransformComponent", "x"), "1.0");
        EXPECT_EQ(Read("QuadRendererComponent", "color.r"), "10");
        EXPECT_TRUE(edits.edits.empty());
    }

    TEST_F(SceneControlSetTest, ANameFieldTakesOnlyATextThePoolHolds) {
        const String unknown = FreshText("sceneAction");

        const ControlReply refused = Set("ClickableComponent", "actionName", Format("\"{}\"", unknown));

        EXPECT_EQ(refused.ErrorCode(), "interning");
        EXPECT_FALSE(PoolString::Find(StringView(unknown)).has_value());
        EXPECT_EQ(Read("ClickableComponent", "actionName"), R"("SceneControlKnown")");

        const PoolString known = PoolString::Intern(StringView(unknown));
        EXPECT_TRUE(Set("ClickableComponent", "actionName", Format("\"{}\"", unknown)).IsOk());
        EXPECT_EQ(Component<ClickableComponent>()._actionName, known);
        EXPECT_TRUE(Set("ClickableComponent", "actionName", R"("")").IsOk());
        EXPECT_TRUE(Component<ClickableComponent>()._actionName.Empty());
    }

    TEST_F(SceneControlSetTest, ASpriteReloadsItsTextureForTheFieldsThatNameItOnly) {
        auto& sprite = Component<SpriteRendererComponent>();
        constexpr uint8_t kPixels[2 * 2 * 4] = {};
        sprite.SetTexture(CoreManager::GetRenderer()->CreateTexture(kPixels, 2, 2));
        const ITexture* const own = sprite.GetTexture().Get();
        ASSERT_NE(own, nullptr);

        ASSERT_TRUE(Set("SpriteRendererComponent", "flipX", "true").IsOk());
        EXPECT_EQ(sprite.GetTexture().Get(), own);

        // No path and no sprite: the reload leaves the component with no texture
        ASSERT_TRUE(Set("SpriteRendererComponent", "texturePath", R"("")").IsOk());
        EXPECT_EQ(sprite.GetTexture().Get(), nullptr);
    }

    TEST_F(SceneControlSetTest, AnAudioSourceLoadsTheClipThatWasWritten) {
        const auto& source = Component<AudioSourceComponent>();
        ASSERT_NE(source.GetLoadedClip(), nullptr);

        ASSERT_TRUE(Set("AudioSourceComponent", "clip", R"("")").IsOk());
        EXPECT_EQ(source.GetLoadedClip(), nullptr);

        ASSERT_TRUE(Set("AudioSourceComponent", "clip", Format("\"{}\"", kClip)).IsOk());
        ASSERT_NE(source.GetLoadedClip(), nullptr);
        EXPECT_EQ(source.GetLoadedClip()->GetState(), ResourceState::Loaded);
    }

    TEST_F(SceneControlSetTest, AWriteIntoTheElementsOfACanvasMakesItLayOutAgain) {
        UICanvasLayout& layout = Component<UICanvasComponent>().GetLayout();
        UILayoutContext context;
        context.outputWidth = 800.0f;
        context.outputHeight = 600.0f;
        layout.Update(context);
        ASSERT_TRUE(layout.IsValid());

        ASSERT_TRUE(Set("UICanvasComponent", "elements[0].blocksInput", "true").IsOk());

        EXPECT_FALSE(layout.IsValid());
    }

    TEST_F(SceneControlSetTest, AWriteIsAnEditOfTheNode) {
        const NodeEdits edits;
        const SceneNode& node = Node("Thing");

        ASSERT_TRUE(Set("QuadRendererComponent", "color.g", "5").IsOk());

        ASSERT_EQ(edits.edits.size(), 1u);
        EXPECT_EQ(edits.edits[0].first, _scene);
        EXPECT_EQ(edits.edits[0].second, &node);
    }

}  // namespace BECore::Tests
