#include "SceneEventsTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/SceneEventsTest.gen.hpp>

namespace BECore::Tests {

    namespace {

        // Position of the next OnDraw call in the draw pass; the fixture resets it before every test.
        int g_nextDrawIndex = 0;

    }  // namespace

    void SceneEventCounter::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &SceneEventCounter::OnUpdate>(this);
    }

    void SceneEventCounter::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        ++updateCount;
    }

    void SceneEventCounter::OnDraw() {
        ++drawCount;
        drawIndex = g_nextDrawIndex++;
    }

    namespace {

        // Two scenes, one probe node each. The click rectangle (0, 0, 100, 100) is the same in both.
        constexpr const char* kCountingScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="A"><nodes>
    <node name="Probe"><components>
      <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
      <component type="SceneEventCounter"/>
      <component type="ClickableComponent" actionName="ActionA"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="B"><nodes>
    <node name="Probe"><components>
      <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
      <component type="SceneEventCounter"/>
      <component type="ClickableComponent" actionName="ActionB"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        // The quads are never drawn here: Scene::Draw() is not called, so no renderer is needed.
        constexpr const char* kColorScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="A"><nodes>
    <node name="Quad"><components>
      <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
      <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="255"/></component>
      <component type="ChangeToRandomColorDebugComponent"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="B"><nodes>
    <node name="Quad"><components>
      <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
      <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="255"/></component>
      <component type="ChangeToRandomColorDebugComponent"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        // Every node has a SceneEventCounter, so its drawIndex tells where the node sits in the draw pass.
        // "Order": three nodes on the default layer.
        // "Layers": a node in front (layer 1), one behind (layer -1), the others on the default layer.
        // "BackFirst" / "BackLast": a board-like shape, a background node (layer -1) and a piece, in both scene orders.
        // "Nested" / "NestedLayer": a parent and a sibling; the tests add a child to the parent at runtime
        // (the XML has no children), and in "NestedLayer" the parent has layer 1.
        constexpr const char* kDrawOrderScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Order"><nodes>
    <node name="First"><components><component type="SceneEventCounter"/></components></node>
    <node name="Second"><components><component type="SceneEventCounter"/></components></node>
    <node name="Third"><components><component type="SceneEventCounter"/></components></node>
  </nodes></scene>
  <scene type="Scene" name="Layers"><nodes>
    <node name="Front" layer="1"><components><component type="SceneEventCounter"/></components></node>
    <node name="Middle"><components><component type="SceneEventCounter"/></components></node>
    <node name="Back" layer="-1"><components><component type="SceneEventCounter"/></components></node>
    <node name="Middle2"><components><component type="SceneEventCounter"/></components></node>
  </nodes></scene>
  <scene type="Scene" name="BackFirst"><nodes>
    <node name="Board" layer="-1"><components><component type="SceneEventCounter"/></components></node>
    <node name="Piece"><components><component type="SceneEventCounter"/></components></node>
  </nodes></scene>
  <scene type="Scene" name="BackLast"><nodes>
    <node name="Piece"><components><component type="SceneEventCounter"/></components></node>
    <node name="Board" layer="-1"><components><component type="SceneEventCounter"/></components></node>
  </nodes></scene>
  <scene type="Scene" name="Nested"><nodes>
    <node name="Parent"><components><component type="SceneEventCounter"/></components></node>
    <node name="Sibling"><components><component type="SceneEventCounter"/></components></node>
  </nodes></scene>
  <scene type="Scene" name="NestedLayer"><nodes>
    <node name="Parent" layer="1"><components><component type="SceneEventCounter"/></components></node>
    <node name="Sibling"><components><component type="SceneEventCounter"/></components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        // A scene with a portrait view variant over a default view, for the view change event
        constexpr const char* kViewScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Responsive" viewWidth="1280" viewHeight="720">
    <viewVariants><view name="Portrait" when="orientation:portrait" width="720" height="1280"/></viewVariants>
    <nodes><node name="Probe"><components><component type="SceneEventCounter"/></components></node></nodes>
  </scene>
  <scene type="Scene" name="Other" viewWidth="1280" viewHeight="720">
    <viewVariants><view name="Portrait" when="orientation:portrait" width="720" height="1280"/></viewVariants>
    <nodes><node name="Probe"><components><component type="SceneEventCounter"/></components></node></nodes>
  </scene>
</scenes></SceneManager>)";

        /// Collects every SceneViewChangedEvent that is emitted while it is alive.
        class ViewChangeRecorder : public SubscriptionHolder {
        public:
            ViewChangeRecorder() {
                Subscribe<SceneEvents::SceneViewChangedEvent, &ViewChangeRecorder::OnChanged>(this);
            }

            eastl::vector<SceneEvents::SceneViewChangedEvent> events;

        private:
            void OnChanged(const SceneEvents::SceneViewChangedEvent& e) {
                events.push_back(e);
            }
        };

        SceneNode* FindNode(SceneNode& node, PoolString name) {
            if (node.GetName() == name) {
                return &node;
            }
            for (const auto& child : node.GetChildren()) {
                if (auto* found = FindNode(*child, name)) {
                    return found;
                }
            }
            return nullptr;
        }

        constexpr uint32_t PackColor(const Color& c) {
            return (uint32_t{c.r} << 24) | (uint32_t{c.g} << 16) | (uint32_t{c.b} << 8) | uint32_t{c.a};
        }

        constexpr uint32_t kInitialColor = (1u << 24) | (2u << 16) | (3u << 8) | 255u;

        /// Collects every ActionEvent that is emitted while it is alive.
        class ActionRecorder : public SubscriptionHolder {
        public:
            ActionRecorder() {
                Subscribe<InputEvents::ActionEvent, &ActionRecorder::OnAction>(this);
            }

            eastl::vector<PoolString> actions;

        private:
            void OnAction(const InputEvents::ActionEvent& e) {
                actions.push_back(e.action);
            }
        };

        void EmitLeftClick(float x, float y) {
            SDL_MouseButtonEvent button{};
            button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            button.button = SDL_BUTTON_LEFT;
            button.down = true;
            button.x = x;
            button.y = y;
            SDLEvents::MouseButtonDownEvent::Emit(button);
        }

    }  // namespace

    class SceneEventsTest : public ::testing::Test {
    protected:
        void SetUp() override {
            g_nextDrawIndex = 0;
        }

        void TearDown() override {
            // Destroying the scenes drops the components' event connections
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        /// Builds a SceneManager from @p xml the way MakeFromConfig<SceneManager> does: the document element is the
        /// object itself. Use through ASSERT_NO_FATAL_FAILURE.
        void LoadScenes(const char* xml) {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(xml)) << "Failed to load XML:\n" << xml;
            _manager = BECore::New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        Scene& GetScene(PoolString name) const {
            return *_manager->GetSceneByName(name);
        }

        /// The component of type T on the single probe node of scene @p name.
        template <typename T>
        IntrusivePtr<T> GetProbeComponent(PoolString name) const {
            return GetScene(name).GetRootNode().GetChildren().front()->GetComponent<T>();
        }

        /// The SceneEventCounter on the node @p nodeName of scene @p sceneName, found at any depth. Empty if there is none.
        IntrusivePtr<SceneEventCounter> GetCounter(PoolString sceneName, PoolString nodeName) const {
            SceneNode* node = FindNode(GetScene(sceneName).GetRootNode(), nodeName);
            return node ? node->GetComponent<SceneEventCounter>() : IntrusivePtr<SceneEventCounter>{};
        }

        /// Draws the active scene once with a fresh draw index, so drawIndex counts from 0 for this frame.
        void DrawFrame() {
            g_nextDrawIndex = 0;
            _manager->DrawAll();
        }

        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(SceneEventsTest, OnlyActiveSceneReceivesUpdateAndDraw) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kCountingScenesXml));
        auto a = GetProbeComponent<SceneEventCounter>("A"_intern);
        auto b = GetProbeComponent<SceneEventCounter>("B"_intern);
        ASSERT_TRUE(a);
        ASSERT_TRUE(b);
        ASSERT_EQ(_manager->GetActiveScene(), _manager->GetSceneByName("A"_intern));

        _manager->UpdateAll();
        _manager->DrawAll();

        EXPECT_EQ(a->updateCount, 1);
        EXPECT_EQ(a->drawCount, 1);
        EXPECT_EQ(b->updateCount, 0);
        EXPECT_EQ(b->drawCount, 0);
    }

    TEST_F(SceneEventsTest, SetActiveSceneSwapsWhichSceneReacts) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kCountingScenesXml));
        auto a = GetProbeComponent<SceneEventCounter>("A"_intern);
        auto b = GetProbeComponent<SceneEventCounter>("B"_intern);
        ASSERT_TRUE(a);
        ASSERT_TRUE(b);

        _manager->SetActiveScene("B"_intern);
        _manager->UpdateAll();
        _manager->DrawAll();

        EXPECT_EQ(a->updateCount, 0);
        EXPECT_EQ(a->drawCount, 0);
        EXPECT_EQ(b->updateCount, 1);
        EXPECT_EQ(b->drawCount, 1);

        _manager->SetActiveScene("A"_intern);
        _manager->UpdateAll();

        EXPECT_EQ(a->updateCount, 1);
        EXPECT_EQ(b->updateCount, 1);
    }

    TEST_F(SceneEventsTest, EventEmittedOutsideTheManagerStillReachesOnlyTheActiveScene) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kCountingScenesXml));
        auto a = GetProbeComponent<SceneEventCounter>("A"_intern);
        auto b = GetProbeComponent<SceneEventCounter>("B"_intern);
        ASSERT_TRUE(a);
        ASSERT_TRUE(b);

        SceneEvents::SceneUpdateEvent::Emit();

        EXPECT_EQ(a->updateCount, 1);
        EXPECT_EQ(b->updateCount, 0);
    }

    TEST_F(SceneEventsTest, ClickEmitsOnlyTheActiveScenesAction) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kCountingScenesXml));
        ActionRecorder recorder;

        EmitLeftClick(10.0f, 10.0f);
        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "ActionA"_intern);

        _manager->SetActiveScene("B"_intern);
        EmitLeftClick(10.0f, 10.0f);
        ASSERT_EQ(recorder.actions.size(), 2u);
        EXPECT_EQ(recorder.actions[1], "ActionB"_intern);
    }

    TEST_F(SceneEventsTest, UnknownSceneNameKeepsTheActiveScene) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kCountingScenesXml));
        auto a = GetProbeComponent<SceneEventCounter>("A"_intern);
        auto b = GetProbeComponent<SceneEventCounter>("B"_intern);
        ASSERT_TRUE(a);
        ASSERT_TRUE(b);

        _manager->SetActiveScene("NoSuchScene"_intern);
        _manager->UpdateAll();

        EXPECT_EQ(_manager->GetActiveScene(), _manager->GetSceneByName("A"_intern));
        EXPECT_EQ(a->updateCount, 1);
        EXPECT_EQ(b->updateCount, 0);
    }

    TEST_F(SceneEventsTest, ComponentAddedAtRuntimeToAnInactiveSceneStaysQuiet) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kCountingScenesXml));

        auto counter = BECore::New<SceneEventCounter>();
        GetScene("B"_intern).GetRootNode().GetChildren().front()->AddComponent(counter);

        _manager->UpdateAll();
        EXPECT_EQ(counter->updateCount, 0);

        _manager->SetActiveScene("B"_intern);
        _manager->UpdateAll();
        EXPECT_EQ(counter->updateCount, 1);
    }

    TEST_F(SceneEventsTest, DrawsNodesOfTheSameLayerInSceneOrder) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kDrawOrderScenesXml));
        auto first = GetCounter("Order"_intern, "First"_intern);
        auto second = GetCounter("Order"_intern, "Second"_intern);
        auto third = GetCounter("Order"_intern, "Third"_intern);
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);
        ASSERT_TRUE(third);

        DrawFrame();

        EXPECT_EQ(first->drawIndex, 0);
        EXPECT_EQ(second->drawIndex, 1);
        EXPECT_EQ(third->drawIndex, 2);
    }

    TEST_F(SceneEventsTest, DrawsALowerLayerFirstWhateverItsPlaceInTheScene) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kDrawOrderScenesXml));
        _manager->SetActiveScene("Layers"_intern);
        auto front = GetCounter("Layers"_intern, "Front"_intern);
        auto middle = GetCounter("Layers"_intern, "Middle"_intern);
        auto back = GetCounter("Layers"_intern, "Back"_intern);
        auto middle2 = GetCounter("Layers"_intern, "Middle2"_intern);
        ASSERT_TRUE(front);
        ASSERT_TRUE(middle);
        ASSERT_TRUE(back);
        ASSERT_TRUE(middle2);
        EXPECT_EQ(front->GetNode().GetLayer(), 1);
        EXPECT_EQ(middle->GetNode().GetLayer(), 0);
        EXPECT_EQ(back->GetNode().GetLayer(), -1);

        DrawFrame();

        EXPECT_EQ(back->drawIndex, 0);
        EXPECT_EQ(middle->drawIndex, 1);
        EXPECT_EQ(middle2->drawIndex, 2);
        EXPECT_EQ(front->drawIndex, 3);
    }

    TEST_F(SceneEventsTest, BackgroundLayerIsDrawnBeforeThePieceInEitherSceneOrder) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kDrawOrderScenesXml));

        for (const char* sceneName : {"BackFirst", "BackLast"}) {
            const PoolString scene = PoolString::Intern(sceneName);
            _manager->SetActiveScene(scene);
            auto board = GetCounter(scene, "Board"_intern);
            auto piece = GetCounter(scene, "Piece"_intern);
            ASSERT_TRUE(board) << sceneName;
            ASSERT_TRUE(piece) << sceneName;

            DrawFrame();

            EXPECT_EQ(board->drawIndex, 0) << sceneName;
            EXPECT_EQ(piece->drawIndex, 1) << sceneName;
        }
    }

    TEST_F(SceneEventsTest, DrawsAParentThenItsChildrenThenTheNextSibling) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kDrawOrderScenesXml));
        _manager->SetActiveScene("Nested"_intern);
        SceneNode* parent = FindNode(GetScene("Nested"_intern).GetRootNode(), "Parent"_intern);
        ASSERT_NE(parent, nullptr);
        auto child = parent->AddChild("Child"_intern);
        auto childCounter = BECore::New<SceneEventCounter>();
        child->AddComponent(childCounter);
        auto parentCounter = GetCounter("Nested"_intern, "Parent"_intern);
        auto sibling = GetCounter("Nested"_intern, "Sibling"_intern);
        ASSERT_TRUE(parentCounter);
        ASSERT_TRUE(sibling);

        DrawFrame();

        EXPECT_EQ(parentCounter->drawIndex, 0);
        EXPECT_EQ(childCounter->drawIndex, 1);
        EXPECT_EQ(sibling->drawIndex, 2);
    }

    TEST_F(SceneEventsTest, AChildDoesNotInheritTheLayerOfItsParent) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kDrawOrderScenesXml));
        _manager->SetActiveScene("NestedLayer"_intern);
        SceneNode* parent = FindNode(GetScene("NestedLayer"_intern).GetRootNode(), "Parent"_intern);
        ASSERT_NE(parent, nullptr);
        auto child = parent->AddChild("Child"_intern);
        auto childCounter = BECore::New<SceneEventCounter>();
        child->AddComponent(childCounter);
        auto parentCounter = GetCounter("NestedLayer"_intern, "Parent"_intern);
        auto sibling = GetCounter("NestedLayer"_intern, "Sibling"_intern);
        ASSERT_TRUE(parentCounter);
        ASSERT_TRUE(sibling);

        DrawFrame();

        // The child and the sibling are on layer 0, the parent alone is on layer 1, so it comes last.
        EXPECT_EQ(childCounter->drawIndex, 0);
        EXPECT_EQ(sibling->drawIndex, 1);
        EXPECT_EQ(parentCounter->drawIndex, 2);
    }

    TEST_F(SceneEventsTest, ARemovedComponentIsNotDrawn) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kDrawOrderScenesXml));
        auto first = GetCounter("Order"_intern, "First"_intern);
        auto second = GetCounter("Order"_intern, "Second"_intern);
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);

        first->GetNode().RemoveComponent("SceneEventCounter"_intern);
        DrawFrame();

        EXPECT_EQ(first->drawCount, 0);
        EXPECT_EQ(second->drawCount, 1);
        EXPECT_EQ(second->drawIndex, 0);
    }

    TEST_F(SceneEventsTest, ActionEventRecolorsOnlyTheActiveScenesQuad) {
        ASSERT_NO_FATAL_FAILURE(LoadScenes(kColorScenesXml));
        auto quadA = GetProbeComponent<QuadRendererComponent>("A"_intern);
        auto quadB = GetProbeComponent<QuadRendererComponent>("B"_intern);
        ASSERT_TRUE(quadA);
        ASSERT_TRUE(quadB);
        ASSERT_EQ(PackColor(quadA->_color), kInitialColor);
        ASSERT_EQ(PackColor(quadB->_color), kInitialColor);

        InputEvents::ActionEvent::Emit("AnyAction"_intern);

        EXPECT_NE(PackColor(quadA->_color), kInitialColor);
        EXPECT_EQ(PackColor(quadB->_color), kInitialColor);
    }

    // The view change event: decided in Draw(), announced in Update()

    class SceneViewChangedEventTest : public SceneEventsTest {
    protected:
        void SetUp() override {
            SceneEventsTest::SetUp();
            _renderer = BECore::New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            ASSERT_NO_FATAL_FAILURE(LoadScenes(kViewScenesXml));
            _scene = &GetScene("Responsive"_intern);
        }

        void DrawAt(float width, float height) {
            _renderer->width = width;
            _renderer->height = height;
            _manager->DrawAll();
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        Scene* _scene = nullptr;
    };

    TEST_F(SceneViewChangedEventTest, NothingIsAnnouncedWhileTheDefaultViewStays) {
        ViewChangeRecorder recorder;

        DrawAt(900.0f, 600.0f);
        _manager->UpdateAll();
        DrawAt(1000.0f, 500.0f);
        _manager->UpdateAll();

        EXPECT_TRUE(recorder.events.empty());
    }

    TEST_F(SceneViewChangedEventTest, ADrawDecidesAndTheNextUpdateAnnounces) {
        ViewChangeRecorder recorder;

        DrawAt(600.0f, 900.0f);
        EXPECT_TRUE(recorder.events.empty()) << "Draw must not run handlers";
        EXPECT_EQ(_scene->GetActiveView().index, 0);

        _manager->UpdateAll();
        ASSERT_EQ(recorder.events.size(), 1u);
        EXPECT_EQ(recorder.events[0].scene, _scene);
        EXPECT_EQ(recorder.events[0].view.name, "Portrait"_intern);
        EXPECT_EQ(recorder.events[0].view.index, 0);
        EXPECT_FLOAT_EQ(recorder.events[0].view.width, 720.0f);
        EXPECT_FLOAT_EQ(recorder.events[0].view.height, 1280.0f);
    }

    TEST_F(SceneViewChangedEventTest, AChangeIsAnnouncedOnceHoweverManyUpdatesFollow) {
        ViewChangeRecorder recorder;
        DrawAt(600.0f, 900.0f);

        _manager->UpdateAll();
        _manager->UpdateAll();
        DrawAt(500.0f, 800.0f);
        _manager->UpdateAll();

        EXPECT_EQ(recorder.events.size(), 1u);
    }

    TEST_F(SceneViewChangedEventTest, SeveralChangesBetweenUpdatesAreOneEventWithTheLatestView) {
        ViewChangeRecorder recorder;

        DrawAt(600.0f, 900.0f);
        DrawAt(900.0f, 600.0f);
        DrawAt(600.0f, 900.0f);
        _manager->UpdateAll();
        ASSERT_EQ(recorder.events.size(), 1u);
        EXPECT_EQ(recorder.events[0].view.index, 0);

        // A flip and a flip back between two updates is a change of the version, so it is announced with the view it ended on
        DrawAt(900.0f, 600.0f);
        DrawAt(600.0f, 900.0f);
        _manager->UpdateAll();
        EXPECT_EQ(recorder.events.size(), 2u);
    }

    TEST_F(SceneViewChangedEventTest, ChangingBackToTheDefaultIsAnnouncedWithTheDefaultView) {
        ViewChangeRecorder recorder;
        DrawAt(600.0f, 900.0f);
        _manager->UpdateAll();

        DrawAt(900.0f, 600.0f);
        _manager->UpdateAll();

        ASSERT_EQ(recorder.events.size(), 2u);
        EXPECT_EQ(recorder.events[1].view.index, -1);
        EXPECT_TRUE(recorder.events[1].view.name.Empty());
        EXPECT_FLOAT_EQ(recorder.events[1].view.width, 1280.0f);
    }

    TEST_F(SceneViewChangedEventTest, AForcedViewIsAnnouncedLikeAPickAndSoIsClearingIt) {
        ViewChangeRecorder recorder;

        _scene->SetViewOverride(0);
        DrawAt(900.0f, 600.0f);
        _manager->UpdateAll();
        _manager->UpdateAll();
        ASSERT_EQ(recorder.events.size(), 1u);
        EXPECT_EQ(recorder.events[0].view.index, 0);

        _scene->SetViewOverride(std::nullopt);
        DrawAt(900.0f, 600.0f);
        _manager->UpdateAll();
        ASSERT_EQ(recorder.events.size(), 2u);
        EXPECT_EQ(recorder.events[1].view.index, -1);
    }

    TEST_F(SceneViewChangedEventTest, OnlyTheSceneThatIsUpdatedAnnounces) {
        ViewChangeRecorder recorder;
        DrawAt(600.0f, 900.0f);

        // "Other" was never drawn, so it has nothing to announce; "Responsive" is the active scene
        _manager->UpdateAll();

        ASSERT_EQ(recorder.events.size(), 1u);
        EXPECT_EQ(recorder.events[0].scene, _scene);
        EXPECT_EQ(GetScene("Other"_intern).GetViewVersion(), 0u);
    }

}  // namespace BECore::Tests
