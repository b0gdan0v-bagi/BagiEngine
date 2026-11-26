#include "ComponentDetachTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/PointerTargetComponentTest.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Time/Time.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/ComponentDetachTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    void LateJoinProbe::Join() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &LateJoinProbe::OnUpdate>(this);
        SubscribeInScene<InputEvents::ActionEvent, &LateJoinProbe::OnAction>(this);
    }

    void LateJoinProbe::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        ++updates;
    }

    void LateJoinProbe::OnAction(const InputEvents::ActionEvent&) {
        ++actions;
    }

    namespace {

        constexpr float kOutputWidth = 800.0f;
        constexpr float kOutputHeight = 600.0f;
        constexpr float kInside = 10.0f;
        constexpr float kOutside = 500.0f;
        constexpr uint32_t kMouse = 1;

        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main" viewWidth="800" viewHeight="600"><nodes>
    <node name="Target"><components>
      <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
      <component type="PointerTargetTestComponent"/>
    </components></node>
    <node name="Hud"><components>
      <component type="TransformComponent" x="20" y="30" width="200" height="100"/>
      <component type="UICanvasComponent" space="World"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        bool Press(float x, float y) {
            SDL_MouseButtonEvent event{};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.which = kMouse;
            event.button = SDL_BUTTON_LEFT;
            event.down = true;
            event.x = x;
            event.y = y;
            return CoreManager::GetUIInput().DispatchMouseButtonDown(event);
        }

        bool Release(float x, float y) {
            SDL_MouseButtonEvent event{};
            event.type = SDL_EVENT_MOUSE_BUTTON_UP;
            event.which = kMouse;
            event.button = SDL_BUTTON_LEFT;
            event.x = x;
            event.y = y;
            return CoreManager::GetUIInput().DispatchMouseButtonUp(event);
        }

        bool Move(float x, float y, SDL_MouseButtonFlags held = 0) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.which = kMouse;
            event.state = held;
            event.x = x;
            event.y = y;
            return CoreManager::GetUIInput().DispatchMouseMotion(event);
        }

        Vector<TargetCall> Calls(const TargetScript& script) {
            Vector<TargetCall> calls;
            for (const TargetRecord& record : script.log) {
                calls.push_back(record.call);
            }
            return calls;
        }

    }  // namespace

    /// A component that somebody still holds after its node let it go, died, or its scene died
    class ComponentDetachTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetTime().Reset();
            _renderer = New<RecordingRenderer>();
            _renderer->width = kOutputWidth;
            _renderer->height = kOutputHeight;
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetTime().Reset();
        }

        Scene& Main() const {
            return Require(_manager->GetActiveScene());
        }

        SceneNode& Node(const char* name) const {
            for (const auto& node : Main().GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(name)) {
                    return *node;
                }
            }
            ADD_FAILURE() << "no node " << name;
            return Main().GetRootNode();
        }

        IntrusivePtr<LateJoinProbe> AddProbe(SceneNode& node) const {
            IntrusivePtr<LateJoinProbe> probe = New<LateJoinProbe>();
            node.AddComponent(probe);
            return probe;
        }

        static void RemoveTarget(SceneNode& node) {
            node.RemoveComponent("PointerTargetTestComponent"_intern);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtr<SceneManager> _manager;
        TargetScript _script;
    };

    TEST_F(ComponentDetachTest, RemoveComponentDetaches) {
        const IntrusivePtr<SceneNode> node = Main().CreateNode("N"_intern);
        const IntrusivePtr<LateJoinProbe> probe = AddProbe(*node);
        ASSERT_EQ(probe->TryGetNode(), node.Get());
        ASSERT_EQ(probe->TryGetScene(), &Main());
        ASSERT_TRUE(probe->IsSceneActive());

        node->RemoveComponent("LateJoinProbe"_intern);

        EXPECT_EQ(probe->TryGetNode(), nullptr);
        EXPECT_EQ(probe->TryGetScene(), nullptr);
        EXPECT_FALSE(probe->IsSceneActive());
    }

    TEST_F(ComponentDetachTest, AddComponentDetachesTheOneItReplaces) {
        const IntrusivePtr<SceneNode> node = Main().CreateNode("N"_intern);
        const IntrusivePtr<LateJoinProbe> first = AddProbe(*node);

        const IntrusivePtr<LateJoinProbe> second = AddProbe(*node);

        EXPECT_EQ(first->TryGetNode(), nullptr);
        EXPECT_EQ(second->TryGetNode(), node.Get());
    }

    TEST_F(ComponentDetachTest, AComponentMovedToAnotherNodeKeepsItWhenTheOldNodeLetsGo) {
        const IntrusivePtr<SceneNode> removing = Main().CreateNode("Removing"_intern);
        IntrusivePtr<SceneNode> dying = Main().CreateNode("Dying"_intern);
        const IntrusivePtr<SceneNode> target = Main().CreateNode("Target"_intern);
        const IntrusivePtr<LateJoinProbe> probe = AddProbe(*removing);
        dying->AddComponent(probe);
        target->AddComponent(probe);

        removing->RemoveComponent("LateJoinProbe"_intern);
        Main().GetRootNode().RemoveChild(dying.Get());
        dying.Reset();

        EXPECT_EQ(probe->TryGetNode(), target.Get());
    }

    TEST_F(ComponentDetachTest, ADeadNodeDetachesItsComponentAndTheChildSomebodyHolds) {
        IntrusivePtr<SceneNode> parent = Main().CreateNode("Parent"_intern);
        const IntrusivePtr<SceneNode> child = Main().CreateNode("Child"_intern, parent.Get());
        const IntrusivePtr<LateJoinProbe> ofParent = AddProbe(*parent);
        const IntrusivePtr<LateJoinProbe> ofChild = AddProbe(*child);

        Main().GetRootNode().RemoveChild(parent.Get());
        // A node RemoveChild() took out and somebody holds keeps its scene
        EXPECT_EQ(ofParent->TryGetScene(), &Main());
        parent.Reset();

        EXPECT_EQ(ofParent->TryGetNode(), nullptr);
        EXPECT_EQ(child->GetParent(), nullptr);
        // Joining asks the parents whether the node is in the tree: none of them is the dead one
        ofChild->Join();
        EXPECT_EQ(Main().GetUpdaterCount(SceneUpdateKind::Frame), 0u);
    }

    TEST_F(ComponentDetachTest, AHeldNodeDoesNotNameADeadScene) {
        const IntrusivePtr<SceneNode> node = Main().CreateNode("N"_intern);
        const IntrusivePtr<LateJoinProbe> probe = AddProbe(*node);

        _manager.Reset();

        EXPECT_EQ(probe->TryGetNode(), node.Get());
        EXPECT_EQ(probe->TryGetScene(), nullptr);
        EXPECT_FALSE(probe->IsSceneActive());
        probe->Join();
        SceneEvents::SceneUpdateEvent::Emit(0.1f);
        EXPECT_EQ(probe->updates, 0);
    }

    TEST_F(ComponentDetachTest, SubscribeInSceneOfARemovedComponentJoinsNoListAndHearsNothing) {
        const IntrusivePtr<SceneNode> node = Main().CreateNode("N"_intern);
        const IntrusivePtr<LateJoinProbe> probe = AddProbe(*node);
        node->RemoveComponent("LateJoinProbe"_intern);

        probe->Join();
        _manager->UpdateAll();
        InputEvents::ActionEvent::Emit("Anything"_intern);

        EXPECT_EQ(Main().GetUpdaterCount(SceneUpdateKind::Frame), 0u);
        EXPECT_EQ(probe->updates, 0);
        EXPECT_EQ(probe->actions, 0);
    }

    TEST_F(ComponentDetachTest, ARemovedTargetThatIsHoveredAndHoldsAPressGetsNoCallback) {
        SceneNode& node = Node("Target");
        const IntrusivePtr<PointerTargetTestComponent> target = node.GetComponent<PointerTargetTestComponent>();
        ASSERT_TRUE(target);
        target->script = &_script;
        _manager->DrawAll();
        Move(kInside, kInside);
        Press(kInside, kInside);
        const Vector<TargetCall> before = Calls(_script);
        ASSERT_EQ(before, (Vector<TargetCall>{TargetCall::Enter, TargetCall::Move, TargetCall::Press}));

        RemoveTarget(node);
        Move(kInside, kInside, SDL_BUTTON_LMASK);
        Move(kOutside, kOutside, SDL_BUTTON_LMASK);
        Release(kOutside, kOutside);
        // Public, so a holder can call them without UIInput
        EXPECT_FALSE(target->OnMouseButtonDown(SDL_MouseButtonEvent{}));
        EXPECT_FALSE(target->OnMouseWheel(SDL_MouseWheelEvent{}));
        target->OnPointerLeave(kMouse);

        EXPECT_EQ(Calls(_script), before);
    }

    TEST_F(ComponentDetachTest, ATargetItsEnterCallbackRemovedGetsNoMove) {
        SceneNode& node = Node("Target");
        const IntrusivePtr<PointerTargetTestComponent> target = node.GetComponent<PointerTargetTestComponent>();
        ASSERT_TRUE(target);
        target->script = &_script;
        _script.hook = [&node](TargetCall) { RemoveTarget(node); };
        _manager->DrawAll();

        Move(kInside, kInside);

        EXPECT_EQ(Calls(_script), (Vector<TargetCall>{TargetCall::Enter}));
    }

    TEST_F(ComponentDetachTest, ATargetThatDeclinedAMoveAndRemovedItselfGetsNoExit) {
        SceneNode& node = Node("Target");
        const IntrusivePtr<PointerTargetTestComponent> target = node.GetComponent<PointerTargetTestComponent>();
        ASSERT_TRUE(target);
        target->script = &_script;
        _script.takeMove = false;
        _script.hook = [&node](TargetCall call) {
            if (call == TargetCall::Move) {
                RemoveTarget(node);
            }
        };
        _manager->DrawAll();

        Move(kInside, kInside);

        EXPECT_EQ(Calls(_script), (Vector<TargetCall>{TargetCall::Enter, TargetCall::Move}));
    }

    TEST_F(ComponentDetachTest, ARemovedCanvasLaysOutWithoutASceneOrAWorldBox) {
        SceneNode& node = Node("Hud");
        const IntrusivePtr<UICanvasComponent> canvas = node.GetComponent<UICanvasComponent>();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(kOutputWidth, kOutputHeight);
        ASSERT_GT(canvas->GetLayout().GetContext().sceneViewWidth, 0.0f);
        ASSERT_GT(canvas->GetLayout().GetContext().worldBox.w, 0.0f);

        node.RemoveComponent("UICanvasComponent"_intern);
        canvas->UpdateLayout(kOutputWidth, kOutputHeight);

        EXPECT_EQ(canvas->GetLayout().GetContext().sceneViewWidth, 0.0f);
        EXPECT_EQ(canvas->GetLayout().GetContext().worldBox.w, 0.0f);
    }

}  // namespace BECore::Tests
