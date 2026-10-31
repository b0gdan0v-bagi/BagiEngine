#include "SceneEventsTest.h"

#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/SceneEventsTest.gen.hpp>

namespace BECore::Tests {

    void SceneEventCounter::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &SceneEventCounter::OnUpdate>(this);
        SubscribeInScene<SceneEvents::SceneDrawEvent, &SceneEventCounter::OnDraw>(this);
    }

    void SceneEventCounter::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        ++updateCount;
    }

    void SceneEventCounter::OnDraw(const SceneEvents::SceneDrawEvent&) {
        ++drawCount;
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

        // The quads are never drawn here: SceneDrawEvent is not emitted, so no renderer is needed.
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
        void TearDown() override {
            // Destroying the scenes drops the components' event connections
            _manager.Reset();
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

}  // namespace BECore::Tests
