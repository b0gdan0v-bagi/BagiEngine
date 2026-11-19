#include "ClickableComponentTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/PointerTracker.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UIInputEvents.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Generated/ClickableComponentTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    void ClipPushTestComponent::OnDraw() {
        CoreManager::GetRenderer()->PushClipRect(Rect{_x, _y, _width, _height});
    }

    void ClipPopTestComponent::OnDraw() {
        CoreManager::GetRenderer()->PopClipRect();
    }

    namespace {

        constexpr float kBoxSize = 100.0f;
        constexpr float kInside = 10.0f;
        constexpr float kOutside = 500.0f;

        /// A node with a rect of 100 x 100 at (@p x, 0) and a clickable of @p action
        String ClickableNode(const char* name, const char* action, float x, int layer = 0) {
            return Format(R"(<node name="{}" layer="{}"><components>
  <component type="TransformComponent" x="{}" y="0" width="{}" height="{}"/>
  <component type="ClickableComponent" actionName="{}"/>
</components></node>)",
                          name, layer, x, kBoxSize, kBoxSize, action);
        }

        String SingleSceneXml(const String& nodes) {
            return Format(R"(<SceneManager><scenes><scene type="Scene" name="S"><nodes>{}</nodes></scene></scenes></SceneManager>)", nodes);
        }

        /// Collects the actions and the raw mouse events of one test
        class InputRecorder : public SubscriptionHolder {
        public:
            InputRecorder() {
                Subscribe<InputEvents::ActionEvent, &InputRecorder::OnAction>(this);
                Subscribe<SDLEvents::MouseButtonDownEvent, &InputRecorder::OnRawDown>(this);
                Subscribe<SDLEvents::MouseButtonUpEvent, &InputRecorder::OnRawUp>(this);
            }

            Vector<PoolString> actions;
            int rawDowns = 0;
            int rawUps = 0;

        private:
            void OnAction(const InputEvents::ActionEvent& e) {
                actions.push_back(e.action);
            }
            void OnRawDown(const SDLEvents::MouseButtonDownEvent&) {
                ++rawDowns;
            }
            void OnRawUp(const SDLEvents::MouseButtonUpEvent&) {
                ++rawUps;
            }
        };

        bool Move(float x, float y, SDL_MouseButtonFlags buttonsHeld = 0) {
            return CoreManager::GetUIInput().DispatchMouseMotion(Motion(x, y, buttonsHeld));
        }

        bool Press(float x, float y, uint8_t button = SDL_BUTTON_LEFT, uint32_t which = 0) {
            SDL_MouseButtonEvent event = Button(x, y, true, button);
            event.which = which;
            return CoreManager::GetUIInput().DispatchMouseButtonDown(event);
        }

        bool Release(float x, float y, uint8_t button = SDL_BUTTON_LEFT, uint32_t which = 0) {
            SDL_MouseButtonEvent event = Button(x, y, false, button);
            event.which = which;
            return CoreManager::GetUIInput().DispatchMouseButtonUp(event);
        }

    }  // namespace

    class ClickableComponentTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _renderer = BECore::New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
        }

        void TearDown() override {
            // Destroying the scenes unregisters the clickables from UIInput
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        void LoadScenes(const String& xml) {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(xml.c_str())) << xml.c_str();
            _manager = BECore::New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void LoadNodes(const String& nodes) {
            ASSERT_NO_FATAL_FAILURE(LoadScenes(SingleSceneXml(nodes)));
        }

        void Draw() {
            _manager->DrawAll();
        }

        SceneNode& Node(const char* name) {
            for (const auto& node : _manager->GetActiveScene()->GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(name)) {
                    return *node;
                }
            }
            ADD_FAILURE() << "no node " << name;
            return _manager->GetActiveScene()->GetRootNode();
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(ClickableComponentTest, APressInsideEmitsTheActionOnceAndNoRawEvent) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 0.0f)));
        Draw();
        InputRecorder recorder;

        EXPECT_TRUE(Press(kInside, kInside));

        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "ActA"_intern);
        EXPECT_EQ(recorder.rawDowns, 0);
    }

    TEST_F(ClickableComponentTest, APressOutsideTakesNothingAndStaysRaw) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 0.0f)));
        Draw();
        InputRecorder recorder;

        EXPECT_FALSE(Press(kOutside, kOutside));

        EXPECT_TRUE(recorder.actions.empty());
        EXPECT_EQ(recorder.rawDowns, 1);
    }

    TEST_F(ClickableComponentTest, AnyButtonPresses) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 0.0f)));
        Draw();
        InputRecorder recorder;

        EXPECT_TRUE(Press(kInside, kInside, SDL_BUTTON_RIGHT));

        EXPECT_EQ(recorder.actions.size(), 1u);
    }

    TEST_F(ClickableComponentTest, ASceneThatNeverDrewStillTakesAPress) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 0.0f)));
        InputRecorder recorder;

        EXPECT_TRUE(Press(kInside, kInside));

        EXPECT_EQ(recorder.actions.size(), 1u);
    }

    TEST_F(ClickableComponentTest, AHiddenNodeTakesNothing) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 0.0f)));
        Draw();
        InputRecorder recorder;
        ASSERT_TRUE(Press(kInside, kInside));
        ASSERT_TRUE(Release(kInside, kInside));
        recorder.actions.clear();

        Node("A").SetEditorFlags(EditorFlags::Hidden);
        Draw();

        EXPECT_FALSE(Press(kInside, kInside));
        EXPECT_TRUE(recorder.actions.empty());
        EXPECT_EQ(recorder.rawDowns, 1);
    }

    TEST_F(ClickableComponentTest, ANodeHiddenBeforeItsFirstDrawTakesNothing) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 0.0f)));
        Node("A").SetEditorFlags(EditorFlags::Hidden);
        Draw();
        InputRecorder recorder;

        EXPECT_FALSE(Press(kInside, kInside));

        EXPECT_TRUE(recorder.actions.empty());
    }

    TEST_F(ClickableComponentTest, ANodeShownAgainTakesPressesAgain) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 0.0f)));
        Node("A").SetEditorFlags(EditorFlags::Hidden);
        Draw();
        Node("A").SetEditorFlags(EditorFlags::None);
        Draw();
        InputRecorder recorder;

        EXPECT_TRUE(Press(kInside, kInside));

        EXPECT_EQ(recorder.actions.size(), 1u);
    }

    TEST_F(ClickableComponentTest, TheInactiveSceneTakesNothing) {
        const String xml = Format(R"(<SceneManager><scenes>
<scene type="Scene" name="Empty"><nodes></nodes></scene>
<scene type="Scene" name="Other"><nodes>{}</nodes></scene>
</scenes></SceneManager>)",
                                  ClickableNode("A", "ActOther", 0.0f));
        ASSERT_NO_FATAL_FAILURE(LoadScenes(xml));
        InputRecorder recorder;

        EXPECT_FALSE(Press(kInside, kInside));
        EXPECT_TRUE(recorder.actions.empty());

        _manager->SetActiveScene("Other"_intern);
        EXPECT_TRUE(Press(kInside, kInside));
        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "ActOther"_intern);
    }

    TEST_F(ClickableComponentTest, TheLaterSiblingWinsAndTheLowerOneGetsNothing) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Format("{}{}", ClickableNode("Low", "ActLow", 0.0f), ClickableNode("High", "ActHigh", 0.0f))));
        Draw();
        InputRecorder recorder;

        EXPECT_TRUE(Press(kInside, kInside));

        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "ActHigh"_intern);
    }

    TEST_F(ClickableComponentTest, AHigherLayerWinsOverALaterSibling) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Format("{}{}", ClickableNode("Front", "ActFront", 0.0f, 1), ClickableNode("Later", "ActLater", 0.0f))));
        Draw();
        InputRecorder recorder;

        EXPECT_TRUE(Press(kInside, kInside));

        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "ActFront"_intern);
    }

    TEST_F(ClickableComponentTest, TheLowerOneTakesAPressOutsideTheOneAboveIt) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Format("{}{}", ClickableNode("Low", "ActLow", 0.0f), ClickableNode("High", "ActHigh", kBoxSize / 2))));
        Draw();
        InputRecorder recorder;

        EXPECT_TRUE(Press(kInside, kInside));

        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "ActLow"_intern);
    }

    TEST_F(ClickableComponentTest, APressHeldOnOneAndReleasedOverTheOtherEmitsOnlyTheFirst) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Format("{}{}", ClickableNode("A", "ActA", 0.0f), ClickableNode("B", "ActB", 200.0f))));
        Draw();
        InputRecorder recorder;

        EXPECT_TRUE(Press(kInside, kInside));
        EXPECT_TRUE(Release(200.0f + kInside, kInside));

        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "ActA"_intern);
        EXPECT_EQ(recorder.rawUps, 0);
    }

    TEST_F(ClickableComponentTest, TheAreaOutsideAClipPushedBeforeItIsNoTargetAndTheLowerOneTakesIt) {
        const String clipped = Format(R"(<node name="Clipped"><components>
  <component type="ClipPushTestComponent" x="0" y="0" width="{}" height="{}"/>
  <component type="TransformComponent" x="0" y="0" width="{}" height="{}"/>
  <component type="ClickableComponent" actionName="ActClipped"/>
  <component type="ClipPopTestComponent"/>
</components></node>)",
                                      kBoxSize / 2, kBoxSize, kBoxSize, kBoxSize);
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Format("{}{}", ClickableNode("Under", "ActUnder", 0.0f), clipped)));
        Draw();
        InputRecorder recorder;

        EXPECT_TRUE(Press(kInside, kInside));
        EXPECT_TRUE(Press(kBoxSize * 0.75f, kInside));

        ASSERT_EQ(recorder.actions.size(), 2u);
        EXPECT_EQ(recorder.actions[0], "ActClipped"_intern);
        EXPECT_EQ(recorder.actions[1], "ActUnder"_intern);
        EXPECT_EQ(_renderer->GetClipDepth(), 0u);
    }

    TEST_F(ClickableComponentTest, AnEmptyActionTakesNothing) {
        const String silent = R"(<node name="Silent"><components>
  <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
  <component type="ClickableComponent"/>
</components></node>)";
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Format("{}{}", ClickableNode("Under", "ActUnder", 0.0f), silent)));
        Draw();
        InputRecorder recorder;

        EXPECT_TRUE(Press(kInside, kInside));

        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "ActUnder"_intern);
    }

    TEST_F(ClickableComponentTest, ANodeWithoutATransformTakesNothing) {
        const String bare = R"(<node name="Bare"><components>
  <component type="ClickableComponent" actionName="ActBare"/>
</components></node>)";
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Format("{}{}", ClickableNode("Under", "ActUnder", 0.0f), bare)));
        Draw();
        InputRecorder recorder;

        EXPECT_TRUE(Press(kInside, kInside));

        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "ActUnder"_intern);
    }

    TEST_F(ClickableComponentTest, ADestroyedComponentIsNotCalled) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 0.0f)));
        Draw();
        _manager.Reset();
        InputRecorder recorder;

        EXPECT_FALSE(Press(kInside, kInside));

        EXPECT_TRUE(recorder.actions.empty());
    }

    TEST_F(ClickableComponentTest, TheMotionOverTheRectIsTakenAndOutsideItIsNot) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 0.0f)));
        Draw();

        EXPECT_TRUE(Move(kInside, kInside));
        EXPECT_FALSE(Move(kOutside, kInside));
    }

    TEST_F(ClickableComponentTest, ADragOfTheGamePassesOverTheRect) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 0.0f)));
        Draw();

        EXPECT_FALSE(Move(kInside, kInside, SDL_BUTTON_LMASK));
    }

    TEST_F(ClickableComponentTest, APressItTookKeepsTheMotionEvenOutsideTheRectUntilTheRelease) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 0.0f)));
        Draw();
        ASSERT_TRUE(Press(kInside, kInside));

        EXPECT_TRUE(Move(kOutside, kInside, SDL_BUTTON_LMASK));
        EXPECT_TRUE(Release(kOutside, kInside));
        EXPECT_FALSE(Move(kOutside, kInside)) << "nothing is held any more";
        EXPECT_FALSE(Move(kInside, kInside, SDL_BUTTON_LMASK)) << "a later drag of the game passes over the rect";
    }

    TEST_F(ClickableComponentTest, AReleaseOfAPressOfTheGameOverTheRectStaysRaw) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 200.0f)));
        Draw();
        InputRecorder recorder;
        ASSERT_FALSE(Press(kInside, kInside));

        EXPECT_FALSE(Release(200.0f + kInside, kInside));

        EXPECT_EQ(recorder.rawUps, 1);
        EXPECT_TRUE(recorder.actions.empty());
    }

    TEST_F(ClickableComponentTest, AReleaseOfAnotherButtonOfTheGameReachesTheGameWhileTheClickableHoldsItsOwnPress) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 200.0f)));
        Draw();
        InputRecorder recorder;
        ASSERT_FALSE(Press(kInside, kInside, SDL_BUTTON_RIGHT));
        ASSERT_TRUE(Press(200.0f + kInside, kInside, SDL_BUTTON_LEFT));

        EXPECT_FALSE(Release(200.0f + kInside, kInside, SDL_BUTTON_RIGHT));
        EXPECT_EQ(recorder.rawUps, 1);

        EXPECT_TRUE(Release(200.0f + kInside, kInside, SDL_BUTTON_LEFT));
        EXPECT_EQ(recorder.rawUps, 1);
    }

    TEST_F(ClickableComponentTest, AReleaseOfAnotherPointerOfTheGameReachesTheGameWhileTheClickableHoldsItsOwnPress) {
        constexpr uint32_t kFinger = 7;
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 200.0f)));
        Draw();
        InputRecorder recorder;
        ASSERT_FALSE(Press(kInside, kInside, SDL_BUTTON_LEFT, kFinger));
        ASSERT_TRUE(Press(200.0f + kInside, kInside));

        EXPECT_FALSE(Release(200.0f + kInside, kInside, SDL_BUTTON_LEFT, kFinger));
        EXPECT_EQ(recorder.rawUps, 1);

        EXPECT_TRUE(Release(200.0f + kInside, kInside));
        EXPECT_EQ(recorder.rawUps, 1);
    }

    TEST_F(ClickableComponentTest, ARightPressOfTheGameThenALeftPressOnTheClickableThenTheRightReleaseOverItReachesTheGameOnce) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 200.0f)));
        Draw();
        InputRecorder recorder;
        ASSERT_FALSE(Press(kInside, kInside, SDL_BUTTON_RIGHT));
        ASSERT_TRUE(Press(200.0f + kInside, kInside, SDL_BUTTON_LEFT));

        EXPECT_FALSE(Release(200.0f + kInside, kInside, SDL_BUTTON_RIGHT));
        EXPECT_EQ(recorder.rawUps, 1);
        EXPECT_FALSE(Release(kInside, kInside, SDL_BUTTON_RIGHT)) << "the right press is already released";
        EXPECT_EQ(recorder.rawUps, 2);
    }

    TEST_F(ClickableComponentTest, APressTakenWhileEveryPlaceOfTheCaptureIsFullHasItsReleaseRaw) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(ClickableNode("A", "ActA", 0.0f)));
        Draw();
        InputRecorder recorder;
        constexpr uint32_t kPlaces = static_cast<uint32_t>(PointerTracker::kMaxEntries);
        for (uint32_t which = 1; which <= kPlaces; ++which) {
            ASSERT_TRUE(Press(kInside, kInside, SDL_BUTTON_LEFT, which));
        }
        const uint32_t overflow = kPlaces + 1;
        ASSERT_TRUE(Press(kInside, kInside, SDL_BUTTON_LEFT, overflow));
        ASSERT_EQ(recorder.actions.size(), kPlaces + 1u);

        EXPECT_FALSE(Release(kInside, kInside, SDL_BUTTON_LEFT, overflow)) << "no capture was recorded, so the release is the game's";
        EXPECT_EQ(recorder.rawUps, 1);
        EXPECT_TRUE(Release(kInside, kInside, SDL_BUTTON_LEFT, 1));
        EXPECT_EQ(recorder.rawUps, 1);
    }

    TEST_F(ClickableComponentTest, ANodeWrittenBeforeTheShapeAndBlocksInputFieldsBehavesAsBefore) {
        const String old = R"(<node name="Old"><components>
  <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
  <component type="ClickableComponent" actionName="ActOld"/>
</components></node>)";
        ASSERT_NO_FATAL_FAILURE(LoadNodes(old));
        Draw();
        const auto clickable = Node("Old").GetComponent<ClickableComponent>();
        ASSERT_TRUE(clickable);
        EXPECT_EQ(clickable->_shape, ClickableShape::Rect);
        EXPECT_FALSE(clickable->_blocksInput);
        InputRecorder recorder;

        EXPECT_TRUE(Press(0.0f, 0.0f)) << "a corner of the rect is inside";
        EXPECT_TRUE(Release(0.0f, 0.0f));

        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "ActOld"_intern);
    }

    TEST_F(ClickableComponentTest, AnEllipseTakesTheCentreAndLeavesTheCornersToWhatIsBelow) {
        const String ellipse = R"(<node name="Round" layer="1"><components>
  <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
  <component type="ClickableComponent" actionName="ActRound" shape="Ellipse"/>
</components></node>)";
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Format("{}{}", ClickableNode("Under", "ActUnder", 0.0f), ellipse)));
        Draw();
        InputRecorder recorder;

        EXPECT_TRUE(Press(kBoxSize * 0.5f, kBoxSize * 0.5f));
        EXPECT_TRUE(Release(kBoxSize * 0.5f, kBoxSize * 0.5f));
        EXPECT_TRUE(Press(1.0f, 1.0f));
        EXPECT_TRUE(Release(1.0f, 1.0f));

        ASSERT_EQ(recorder.actions.size(), 2u);
        EXPECT_EQ(recorder.actions[0], "ActRound"_intern);
        EXPECT_EQ(recorder.actions[1], "ActUnder"_intern) << "the corner is outside the ellipse";
    }

    TEST_F(ClickableComponentTest, AnEllipseOfANonSquareRectFollowsBothSides) {
        const String wide = R"(<node name="Wide"><components>
  <component type="TransformComponent" x="0" y="0" width="200" height="40"/>
  <component type="ClickableComponent" actionName="ActWide" shape="Ellipse"/>
</components></node>)";
        ASSERT_NO_FATAL_FAILURE(LoadNodes(wide));
        Draw();

        EXPECT_TRUE(Move(190.0f, 20.0f)) << "near the end of the long axis, on the centre line";
        EXPECT_FALSE(Move(190.0f, 2.0f));
    }

    TEST_F(ClickableComponentTest, ABlockingClickableWithNoActionShieldsWhatIsBelowAndEmitsNothing) {
        const String shield = R"(<node name="Shield" layer="1"><components>
  <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
  <component type="ClickableComponent" blocksInput="true"/>
</components></node>)";
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Format("{}{}", ClickableNode("Under", "ActUnder", 0.0f), shield)));
        Draw();
        InputRecorder recorder;

        EXPECT_TRUE(Move(kInside, kInside)) << "the motion is taken";
        EXPECT_TRUE(Press(kInside, kInside));
        EXPECT_TRUE(Release(kInside, kInside));

        EXPECT_TRUE(recorder.actions.empty());
        EXPECT_EQ(recorder.rawDowns, 0);
        EXPECT_EQ(recorder.rawUps, 0);
        EXPECT_FALSE(Press(kOutside, kInside)) << "outside the shape it shields nothing";
    }

    TEST_F(ClickableComponentTest, ABlockingClickableOfAnEllipseShieldsOnlyTheEllipse) {
        const String shield = R"(<node name="Shield"><components>
  <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
  <component type="ClickableComponent" blocksInput="true" shape="Ellipse"/>
</components></node>)";
        ASSERT_NO_FATAL_FAILURE(LoadNodes(shield));
        Draw();

        EXPECT_TRUE(Move(kBoxSize * 0.5f, kBoxSize * 0.5f));
        EXPECT_FALSE(Move(1.0f, 1.0f));
    }

    TEST_F(ClickableComponentTest, ANodeWithNoActionAndNoBlocksInputStillTakesNothing) {
        const String silent = R"(<node name="Silent"><components>
  <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
  <component type="ClickableComponent" shape="Ellipse"/>
</components></node>)";
        ASSERT_NO_FATAL_FAILURE(LoadNodes(silent));
        Draw();

        EXPECT_FALSE(Move(kBoxSize * 0.5f, kBoxSize * 0.5f));
        EXPECT_FALSE(Press(kBoxSize * 0.5f, kBoxSize * 0.5f));
    }

    TEST_F(ClickableComponentTest, TheFieldsSurviveASaveAndALoad) {
        auto source = BECore::New<ClickableComponent>();
        source->_actionName = "ActSaved"_intern;
        source->_shape = ClickableShape::Ellipse;
        source->_blocksInput = true;

        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("component"));
        source->Serialize(serializer);
        serializer.EndObject();
        const String text = serializer.SaveToString();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(text)) << text.c_str();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        auto copy = BECore::New<ClickableComponent>();
        copy->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_FALSE(deserializer.HasErrors());

        EXPECT_EQ(copy->_actionName, "ActSaved"_intern);
        EXPECT_EQ(copy->_shape, ClickableShape::Ellipse);
        EXPECT_TRUE(copy->_blocksInput);
    }

}  // namespace BECore::Tests
