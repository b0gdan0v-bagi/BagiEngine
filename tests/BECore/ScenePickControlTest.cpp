// The control method that names the node under a point of the screen, `scene_pick`, over scenes loaded from a string and drawn.

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodePath.h>
#include <BECore/Scene/ScenePickControl.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // The output is 800 x 600. "Main" has no view size; "Fit" shows a 400 x 200 view in it: scale 2, a bar of 100 pixels above and below
        constexpr const char* kPickScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Board"><components/><children>
      <node name="a/b"><components>
        <component type="TransformComponent" x="0" y="0" width="80" height="60"/>
        <component type="QuadRendererComponent"/>
      </components></node>
      <node name="a/b"><components>
        <component type="TransformComponent" x="400" y="300" width="80" height="60"/>
        <component type="QuadRendererComponent"/>
      </components></node>
    </children></node>
    <node name="Hud" layer="1"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Fit" viewWidth="400" viewHeight="200"><nodes>
    <node name="Box"><components>
      <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
      <component type="QuadRendererComponent"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr float kWidth = 800.0f;
        constexpr float kHeight = 600.0f;

        class ScenePickControlTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                IntrusivePtr<RecordingRenderer> renderer = New<RecordingRenderer>();
                renderer->width = kWidth;
                renderer->height = kHeight;
                CoreManager::GetRendererManager().SetRenderer(renderer);

                XmlDeserializer deserializer;
                ASSERT_TRUE(deserializer.LoadFromString(kPickScenesXml));
                _manager = New<SceneManager>();
                _manager->Deserialize(deserializer);
                ASSERT_FALSE(deserializer.HasErrors());
                _manager->Initialize();

                auto hud = Node("Main", "Hud").GetComponent<UICanvasComponent>();
                ASSERT_TRUE(hud);
                auto panel = hud->GetRoot().AddChild("Panel"_intern);
                panel->style.rect.x = UILength::Px(600.0f);
                panel->style.rect.y = UILength::Px(0.0f);
                panel->style.rect.width = UILength::Px(200.0f);
                panel->style.rect.height = UILength::Px(100.0f);
                panel->color = Color{10, 20, 30, 255};
                hud->Invalidate();

                RegisterScenePickControl(_service, [this] { return _active; }, [this] { return _onScreen; });
            }

            void TearDown() override {
                _manager.Reset();
                CoreManager::GetRendererManager().SetRenderer({});
            }

            /// Makes @p name the scene the method works on, and draws it
            void Show(const char* name) {
                _manager->SetActiveScene(PoolString::Intern(name));
                _manager->DrawAll();
                _active = _manager->GetActiveScene();
            }

            SceneNode& Node(const char* sceneName, const char* name) const {
                SceneNode* node = FindNodeBelow(_manager->GetSceneByName(PoolString::Intern(sceneName))->GetRootNode(), name);
                EXPECT_NE(node, nullptr) << name;
                return Require(node);
            }

            ControlReply Call(StringView params) {
                return CallControlText(_service, 1, "scene_pick", params);
            }

            /// Picks the point at the output pixel (@p x, @p y)
            ControlReply PickPixel(float x, float y) {
                return Call(Format(R"({{"x":{},"y":{}}})", x / kWidth, y / kHeight));
            }

            ControlService _service;
            IntrusivePtr<SceneManager> _manager;
            Scene* _active = nullptr;  // what the providers give
            bool _onScreen = true;
        };

        constexpr StringView kMiss = R"({"id":1,"ok":true,"result":{"hit":false,"node":""}})";

    }  // namespace

    TEST_F(ScenePickControlTest, TheAnsweredPathReadsBackToThePickedNode) {
        Show("Main");

        const ControlReply reply = PickPixel(420.0f, 320.0f);

        EXPECT_EQ(reply.Text(), R"({"id":1,"ok":true,"result":{"hit":true,"node":"Board/a\\/b[1]"}})");
        const auto found = FindSceneNodeByPath(_active->GetRootNode(), "Board/a\\/b[1]");
        ASSERT_TRUE(found.has_value());
        const SceneNode& board = Node("Main", "Board");
        EXPECT_EQ(*found, board.GetChildren()[1].Get());
    }

    TEST_F(ScenePickControlTest, ACanvasElementAnswersItsCanvasNode) {
        Show("Main");

        EXPECT_EQ(PickPixel(700.0f, 50.0f).Text(), R"({"id":1,"ok":true,"result":{"hit":true,"node":"Hud"}})");
    }

    TEST_F(ScenePickControlTest, TheFractionsAreOfTheWholeScreenBarsIncluded) {
        Show("Fit");

        // The box covers the output from (0, 100) to (200, 300)
        EXPECT_EQ(PickPixel(100.0f, 200.0f).Text(), R"({"id":1,"ok":true,"result":{"hit":true,"node":"Box"}})");
        EXPECT_EQ(PickPixel(100.0f, 50.0f).Text(), kMiss);
        EXPECT_EQ(PickPixel(100.0f, 350.0f).Text(), kMiss);
    }

    TEST_F(ScenePickControlTest, NothingUnderThePointIsAMissTheEdgesIncluded) {
        Show("Main");

        EXPECT_EQ(PickPixel(300.0f, 200.0f).Text(), kMiss);
        EXPECT_EQ(Call(R"({"x":1,"y":1})").Text(), kMiss);
    }

    TEST_F(ScenePickControlTest, ASceneThatNeverDrewHasNothingToPick) {
        _active = _manager->GetSceneByName("Main"_intern);

        EXPECT_EQ(Call(R"({"x":0,"y":0})").Text(), kMiss);
    }

    TEST_F(ScenePickControlTest, ANodeThatNoPathCanNameIsAMiss) {
        Show("Main");
        SceneNode* parent = &_active->GetRootNode();
        for (size_t depth = 0; depth <= SceneNodePathLimits::kMaxSegments; ++depth) {
            parent = _active->CreateNode("n"_intern, parent).Get();
        }
        const String longName(SceneNodePathLimits::kMaxPathBytes + 1, 'n');
        const IntrusivePtr<SceneNode> wide = _active->CreateNode(PoolString::Intern(longName));
        float x = 100.0f;
        for (SceneNode* node : {parent, wide.Get()}) {
            auto transform = New<TransformComponent>();
            transform->_x = x;
            transform->_y = 400.0f;
            transform->_width = 50.0f;
            transform->_height = 50.0f;
            node->AddComponent(transform);
            node->AddComponent(New<QuadRendererComponent>());
            x += 100.0f;
        }
        _manager->DrawAll();

        EXPECT_EQ(PickPixel(110.0f, 410.0f).Text(), kMiss);
        EXPECT_EQ(PickPixel(210.0f, 410.0f).Text(), kMiss);
    }

    TEST_F(ScenePickControlTest, EachRefusalHasItsCode) {
        Show("Main");

        for (const StringView params : {StringView(R"({})"), StringView(R"({"x":0.5})"), StringView(R"({"y":0.5})"), StringView(R"({"x":"0.5","y":0.5})"), StringView(R"({"x":0.5,"y":true})"),
                                        StringView(R"({"x":-0.001,"y":0.5})"), StringView(R"({"x":0.5,"y":1.001})"), StringView(R"({"x":1e999,"y":0.5})"), StringView(R"({"x":0.5,"y":-1e999})")}) {
            EXPECT_EQ(Call(params).ErrorCode(), "invalid_params") << String(params).c_str();
        }
        _onScreen = false;
        EXPECT_EQ(Call(R"({"x":0,"y":0})").ErrorCode(), "not_screen_view");
        _active = nullptr;
        EXPECT_EQ(Call(R"({"x":0,"y":0})").ErrorCode(), "no_scene");
    }

}  // namespace BECore::Tests
