// The control method that writes what a node holds itself, `scene_set_node`, over a scene loaded from a string.

#include <BECore/Control/ControlClient.h>
#include <BECore/Control/ControlTouches.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodeControl.h>
#include <BECore/Scene/SceneNodePath.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Events/SceneEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kLayeredNodes = R"(
<node name="Back"><components><component type="QuadRendererComponent"/></components></node>
<node name="Front"><components><component type="QuadRendererComponent"/></components></node>)";

        class SceneNodeControlTest : public ::testing::Test, public SubscriptionHolder {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                CoreManager::GetRendererManager().SetRenderer(New<RecordingRenderer>());
                XmlDeserializer deserializer;
                const String xml = SceneConfigText(kLayeredNodes);
                ASSERT_TRUE(deserializer.LoadFromString(xml.c_str()));
                _manager = New<SceneManager>();
                _manager->Deserialize(deserializer);
                ASSERT_FALSE(deserializer.HasErrors());
                _manager->Initialize();
                _scene = _manager->GetSceneByName("Main"_intern);
                ASSERT_NE(_scene, nullptr);
                RegisterSceneNodeControl(_service, [this] { return _active; });
                _active = _scene;
                Subscribe<SceneEvents::SceneNodeEditedEvent, &SceneNodeControlTest::OnEdited>(this);
            }

            void TearDown() override {
                _manager.Reset();
                CoreManager::GetRendererManager().SetRenderer({});
            }

            ControlReply Call(StringView params) {
                return CallControlText(_service, 1, "scene_set_node", params);
            }

            SceneNode& Node(StringView path) {
                const auto found = FindSceneNodeByPath(_scene->GetRootNode(), path);
                return Require(found.has_value() ? *found : nullptr);
            }

            ControlService _service;
            IntrusivePtr<SceneManager> _manager;
            Scene* _scene = nullptr;
            Scene* _active = nullptr;  // what the provider gives
            Vector<Pair<const Scene*, const SceneNode*>> _edits;

        private:
            void OnEdited(const SceneEvents::SceneNodeEditedEvent& event) {
                _edits.push_back({event.scene, event.node});
            }
        };

    }  // namespace

    TEST_F(SceneNodeControlTest, TheLayerIsWrittenAndTheDrawOrderFollowsIt) {
        SceneNode& back = Node("Back");
        SceneNode& front = Node("Front");
        // After a draw the ranks come from the draw list, which only SetLayer() has built again
        _scene->Draw();
        const int32_t backRank = _scene->GetDrawRank(back);
        ASSERT_LT(backRank, _scene->GetDrawRank(front));

        const ControlReply reply = Call(R"({"node":"Back","layer":5})");

        EXPECT_EQ(reply.Text(), R"({"id":1,"ok":true,"result":{"layer":5,"node":"Back"}})");
        EXPECT_EQ(back.GetLayer(), 5);
        EXPECT_EQ(_scene->GetDrawRank(front), backRank);
        EXPECT_GT(_scene->GetDrawRank(back), backRank);
        ASSERT_EQ(_edits.size(), 1u);
        EXPECT_EQ(_edits[0].first, _scene);
        EXPECT_EQ(_edits[0].second, &back);
    }

    TEST_F(SceneNodeControlTest, AWriteOfAClientIsATouchOfTheNode) {
        const IntrusivePtr<ControlClient> client = _service.OpenLocalClient("tool");

        _service.Post(R"({"id":1,"method":"scene_set_node","params":{"node":"Back","layer":5}})", [](const String&) {}, client);
        _service.Drain();

        const ControlTouches& touches = _service.Touches();
        ASSERT_EQ(touches.Size(), 1u);
        EXPECT_EQ(touches.At(0).nodeId, Node("Back").GetRuntimeId());
        EXPECT_TRUE(touches.At(0).component.empty());
    }

    TEST_F(SceneNodeControlTest, EveryValueOfTheLayerTypeIsTaken) {
        EXPECT_TRUE(Call(R"({"node":"Back","layer":-2147483648})").IsOk());
        EXPECT_EQ(Node("Back").GetLayer(), std::numeric_limits<int32_t>::min());
        EXPECT_TRUE(Call(R"({"node":"Back","layer":2147483647})").IsOk());
        EXPECT_EQ(Node("Back").GetLayer(), std::numeric_limits<int32_t>::max());
    }

    TEST_F(SceneNodeControlTest, ARefusedRequestChangesNothing) {
        for (const StringView params :
             {StringView(R"({"node":"Back"})"), StringView(R"({"node":"Back","layer":1.5})"), StringView(R"({"node":"Back","layer":"1"})"), StringView(R"({"node":"Back","layer":true})"),
              StringView(R"({"node":"Back","layer":2147483648})"), StringView(R"({"node":"Back","layer":-2147483649})"), StringView(R"({"node":"Back","layer":18446744073709551615})"),
              StringView(R"({"layer":1})"), StringView(R"({"node":1,"layer":1})"), StringView(R"({"node":"Back\\","layer":1})")}) {
            EXPECT_EQ(Call(params).ErrorCode(), "invalid_params") << String(params).c_str();
        }
        EXPECT_EQ(Call(R"({"node":"Nowhere","layer":1})").ErrorCode(), "node_not_found");
        _active = nullptr;
        EXPECT_EQ(Call(R"({"node":"Back","layer":1})").ErrorCode(), "no_scene");

        EXPECT_EQ(Node("Back").GetLayer(), 0);
        EXPECT_TRUE(_edits.empty());
    }

}  // namespace BECore::Tests
