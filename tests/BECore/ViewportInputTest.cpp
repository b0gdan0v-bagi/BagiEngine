#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/Viewport/ViewportInput.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Screen units equal pixels (800 x 600). One button, Box (100, 100, 200 x 80); the rest of the image is empty ground.
        constexpr const char* kSceneXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Box" x="100" y="100" width="200" height="80" color="40,40,40,255">
            <components><component type="UIInputClicker" action="Boxed"/></components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr float kGroundX = 600.0f;
        constexpr float kGroundY = 400.0f;
        constexpr uint8_t kMiddleButton = 2;

        class Recorder : public SubscriptionHolder {
        public:
            Recorder() {
                Subscribe<InputEvents::ActionEvent, &Recorder::OnAction>(this);
                Subscribe<SDLEvents::MouseButtonDownEvent, &Recorder::OnDown>(this);
                Subscribe<SDLEvents::MouseButtonUpEvent, &Recorder::OnUp>(this);
                Subscribe<SDLEvents::MouseMotionEvent, &Recorder::OnMotion>(this);
                Subscribe<SDLEvents::MouseWheelEvent, &Recorder::OnWheel>(this);
            }

            int actions = 0;
            eastl::vector<SDL_MouseButtonEvent> downs;
            eastl::vector<SDL_MouseButtonEvent> ups;
            int motions = 0;
            int wheels = 0;

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                if (event.action == "Boxed"_intern) {
                    ++actions;
                }
            }
            void OnDown(const SDLEvents::MouseButtonDownEvent& event) {
                downs.push_back(event.button);
            }
            void OnUp(const SDLEvents::MouseButtonUpEvent& event) {
                ups.push_back(event.button);
            }
            void OnMotion(const SDLEvents::MouseMotionEvent&) {
                ++motions;
            }
            void OnWheel(const SDLEvents::MouseWheelEvent&) {
                ++wheels;
            }
        };

    }  // namespace

    class ViewportInputTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kSceneXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _manager->SetActiveScene(PoolString::Intern("Main"));
            ASSERT_TRUE(_manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>());
            _renderer->ClearCalls();
            _manager->DrawAll();  // the layout exists after one draw

            _sink = [this](const ViewportEditClickEvent& click) { _editClicks.push_back(click); };
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        // The pointer arrived at (x, y), over the image
        static ViewportPointerFrame At(float x, float y) {
            ViewportPointerFrame frame;
            frame.x = x;
            frame.y = y;
            frame.deltaX = 1.0f;
            frame.hovered = true;
            return frame;
        }

        static ViewportPointerFrame Press(float x, float y) {
            ViewportPointerFrame frame = At(x, y);
            frame.down[0] = true;
            frame.clicked[0] = true;
            frame.clicks[0] = 1;
            return frame;
        }

        static ViewportPointerFrame Release(float x, float y) {
            ViewportPointerFrame frame = At(x, y);
            frame.deltaX = 0.0f;
            frame.released[0] = true;
            return frame;
        }

        void Send(const ViewportPointerFrame& frame, ViewportInputMode mode, bool modifier = false) {
            _pointer.Forward(frame, mode, modifier, CoreManager::GetUIInput(), _sink);
        }

        void Click(float x, float y, ViewportInputMode mode, bool modifier = false) {
            Send(Press(x, y), mode, modifier);
            Send(Release(x, y), mode, modifier);
        }

        static constexpr float kBoxX = 200.0f;  // the middle of Box
        static constexpr float kBoxY = 140.0f;

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        ViewportPointer _pointer;
        ViewportPointer::EditClickSink _sink;
        eastl::vector<ViewportEditClickEvent> _editClicks;
        Recorder _recorder;
    };

    TEST(ViewportInputTargetTest, TheModifierFlipsTheMode) {
        EXPECT_EQ(ResolveTarget(ViewportInputMode::Play, false), ViewportInputTarget::Game);
        EXPECT_EQ(ResolveTarget(ViewportInputMode::Play, true), ViewportInputTarget::Editor);
        EXPECT_EQ(ResolveTarget(ViewportInputMode::Edit, false), ViewportInputTarget::Editor);
        EXPECT_EQ(ResolveTarget(ViewportInputMode::Edit, true), ViewportInputTarget::Game);
    }

    TEST_F(ViewportInputTest, PlayClickOnAButtonFiresItAndTheGameSeesNoPress) {
        Click(kBoxX, kBoxY, ViewportInputMode::Play);

        EXPECT_EQ(_recorder.actions, 1);
        EXPECT_TRUE(_recorder.downs.empty());
        EXPECT_TRUE(_editClicks.empty());
        EXPECT_FALSE(_pointer.IsGameHeld());
    }

    TEST_F(ViewportInputTest, PlayClickOnEmptyGroundReachesTheGameWithTheSameCoordinates) {
        Click(kGroundX, kGroundY, ViewportInputMode::Play);

        ASSERT_EQ(_recorder.downs.size(), 1u);
        EXPECT_EQ(_recorder.downs[0].x, kGroundX);
        EXPECT_EQ(_recorder.downs[0].y, kGroundY);
        EXPECT_EQ(_recorder.downs[0].button, SDL_BUTTON_LEFT);
        EXPECT_EQ(_recorder.downs[0].clicks, 1);
        EXPECT_TRUE(_recorder.downs[0].down);
        ASSERT_EQ(_recorder.ups.size(), 1u);
        EXPECT_EQ(_recorder.ups[0].x, kGroundX);
        EXPECT_EQ(_recorder.actions, 0);
        EXPECT_TRUE(_editClicks.empty());
    }

    TEST_F(ViewportInputTest, EditGivesTheGameNothingAndTheSinkOneEditClick) {
        Send(At(kBoxX, kBoxY), ViewportInputMode::Edit);
        Click(kBoxX, kBoxY, ViewportInputMode::Edit);
        Click(kGroundX, kGroundY, ViewportInputMode::Edit);
        ViewportPointerFrame wheel = At(kGroundX, kGroundY);
        wheel.wheelY = 1.0f;
        Send(wheel, ViewportInputMode::Edit);

        EXPECT_EQ(_recorder.actions, 0);
        EXPECT_TRUE(_recorder.downs.empty());
        EXPECT_TRUE(_recorder.ups.empty());
        EXPECT_EQ(_recorder.motions, 0);
        EXPECT_EQ(_recorder.wheels, 0);
        ASSERT_EQ(_editClicks.size(), 2u);
        EXPECT_EQ(_editClicks[0].x, kBoxX);
        EXPECT_EQ(_editClicks[0].y, kBoxY);
        EXPECT_EQ(_editClicks[0].button, 0);
        EXPECT_EQ(_editClicks[1].x, kGroundX);
        EXPECT_EQ(_editClicks[1].y, kGroundY);
    }

    TEST_F(ViewportInputTest, EditClickOfAnotherButtonKeepsItsIndex) {
        ViewportPointerFrame frame = At(kGroundX, kGroundY);
        frame.down[kMiddleButton] = true;
        frame.clicked[kMiddleButton] = true;
        Send(frame, ViewportInputMode::Edit);

        ASSERT_EQ(_editClicks.size(), 1u);
        EXPECT_EQ(_editClicks[0].button, kMiddleButton);
    }

    TEST_F(ViewportInputTest, TheModifierSendsAPlayClickToTheEditorAndAnEditClickToTheGame) {
        Click(kBoxX, kBoxY, ViewportInputMode::Play, true);
        EXPECT_EQ(_recorder.actions, 0);
        EXPECT_TRUE(_recorder.downs.empty());
        EXPECT_EQ(_editClicks.size(), 1u);

        Click(kBoxX, kBoxY, ViewportInputMode::Edit, true);
        EXPECT_EQ(_recorder.actions, 1);
        EXPECT_EQ(_editClicks.size(), 1u);
    }

    TEST_F(ViewportInputTest, AGamePressStillReleasesAfterTheModeFlipped) {
        Send(Press(kBoxX, kBoxY), ViewportInputMode::Play);
        EXPECT_TRUE(_pointer.IsGameHeld());
        Send(Release(kBoxX, kBoxY), ViewportInputMode::Edit);

        EXPECT_EQ(_recorder.actions, 1);
        EXPECT_FALSE(_pointer.IsGameHeld());
        EXPECT_TRUE(_editClicks.empty());
    }

    TEST_F(ViewportInputTest, AnEditorPressIsNotReleasedToTheGameAfterTheModeFlipped) {
        Send(Press(kGroundX, kGroundY), ViewportInputMode::Edit);
        Send(Release(kGroundX, kGroundY), ViewportInputMode::Play);

        EXPECT_TRUE(_recorder.ups.empty());
        EXPECT_EQ(_editClicks.size(), 1u);
    }

    TEST_F(ViewportInputTest, APressAndItsReleaseInOneFrameAreBothSeen) {
        ViewportPointerFrame frame = Press(kBoxX, kBoxY);
        frame.released[0] = true;
        Send(frame, ViewportInputMode::Play);

        EXPECT_EQ(_recorder.actions, 1);
        EXPECT_FALSE(_pointer.IsGameHeld());
    }

    TEST_F(ViewportInputTest, AGamePressIsFollowedOutsideTheImageAndResetForgetsIt) {
        Send(Press(kGroundX, kGroundY), ViewportInputMode::Play);
        ViewportPointerFrame outside = At(-50.0f, -50.0f);
        outside.hovered = false;
        outside.down[0] = true;
        const int motionsBefore = _recorder.motions;
        Send(outside, ViewportInputMode::Play);
        EXPECT_EQ(_recorder.motions, motionsBefore + 1);

        _pointer.Reset();
        EXPECT_FALSE(_pointer.IsGameHeld());
        Send(Release(kGroundX, kGroundY), ViewportInputMode::Play);
        EXPECT_TRUE(_recorder.ups.empty());
    }

    TEST_F(ViewportInputTest, ANotHoveredImageGetsNoPressAndNoWheel) {
        ViewportPointerFrame frame = Press(kGroundX, kGroundY);
        frame.hovered = false;
        frame.wheelY = 1.0f;
        Send(frame, ViewportInputMode::Play);

        EXPECT_TRUE(_recorder.downs.empty());
        EXPECT_EQ(_recorder.wheels, 0);
        EXPECT_FALSE(_pointer.IsGameHeld());
    }

    TEST_F(ViewportInputTest, WheelReachesTheGameInPlay) {
        ViewportPointerFrame frame = At(kGroundX, kGroundY);
        frame.wheelY = -1.0f;
        Send(frame, ViewportInputMode::Play);

        EXPECT_EQ(_recorder.wheels, 1);
    }

    TEST_F(ViewportInputTest, ATouchAndAPenKeepTheirWhich) {
        ViewportPointerFrame touch = Press(kGroundX, kGroundY);
        touch.source = ViewportPointerSource::Touch;
        Send(touch, ViewportInputMode::Play);
        ViewportPointerFrame pen = Press(kGroundX, kGroundY);
        pen.source = ViewportPointerSource::Pen;
        Send(pen, ViewportInputMode::Play);
        Send(Press(kGroundX, kGroundY), ViewportInputMode::Play);

        ASSERT_EQ(_recorder.downs.size(), 3u);
        EXPECT_EQ(_recorder.downs[0].which, SDL_TOUCH_MOUSEID);
        EXPECT_EQ(_recorder.downs[1].which, SDL_PEN_MOUSEID);
        EXPECT_EQ(_recorder.downs[2].which, 0u);
    }

    TEST_F(ViewportInputTest, MotionCarriesTheHeldButtonsInItsState) {
        eastl::vector<SDL_MouseMotionEvent> motions;
        struct MotionCatcher : SubscriptionHolder {
            explicit MotionCatcher(eastl::vector<SDL_MouseMotionEvent>& out) : _out(out) {
                Subscribe<SDLEvents::MouseMotionEvent, &MotionCatcher::OnMotion>(this);
            }
            void OnMotion(const SDLEvents::MouseMotionEvent& event) {
                _out.push_back(event.motion);
            }
            eastl::vector<SDL_MouseMotionEvent>& _out;
        } catcher(motions);

        Send(Press(kGroundX, kGroundY), ViewportInputMode::Play);
        ViewportPointerFrame drag = At(kGroundX + 5.0f, kGroundY);
        drag.down[0] = true;
        drag.deltaX = 5.0f;
        Send(drag, ViewportInputMode::Play);

        ASSERT_EQ(motions.size(), 2u);
        EXPECT_EQ(motions[1].x, kGroundX + 5.0f);
        EXPECT_EQ(motions[1].xrel, 5.0f);
        EXPECT_NE(motions[1].state & SDL_BUTTON_LMASK, 0u);
    }

}  // namespace BECore::Tests
