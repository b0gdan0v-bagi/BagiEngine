#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UISettings.h>
#include <BECore/UI/UIStateLook.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The smooth hover and press: the look of a state eases from the old state's look to the new one over `stateDuration` (or `UISettings::stateTransition`), on game time. Every number is on the
     * manual clock in steps of a quarter second (a frame is clamped to 0.25 s); the ease is `UIStateLook::kTransitionEase` (`QuadOut`: 0.75 of the way at half the time). The blend itself is
     * `UIStateBlendTest`, the look of a state `UIStateLookTest`.
     */

    namespace {

        // Output 800 x 600, scale 1. Every button is a clicker with a fill, a picture child Icon... and a caption child Cap..., each with a rest and a hover colour that are a multiple of 4 apart:
        //   Btn   (10, 10, 200 x 60)   stateDuration 1; fill 40 -> hover 80 (pressed 20); picture 100 -> 200; caption 100 -> 200
        //   Plain (10, 100, 200 x 60)  no stateDuration: it follows the setting
        //   Pick  (300, 10, 180 x 56)  a dropdown, stateDuration 1: its header box eases through `GetCurrentBox`
        //   Off   (10, 200, 200 x 60)  stateDuration 0: never eases, whatever the setting
        constexpr const char* kTransitionXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Btn" x="10" y="10" width="200" height="60" color="40,40,40,255" hoverColor="80,80,80,255" pressedColor="20,20,20,255" stateDuration="1">
            <components><component type="UIInputClicker" action="Go"/></components>
            <children>
              <element type="UIElement" name="BtnIcon" x="0" y="10" width="40" height="40">
                <components><component type="UIImageComponent" tint="100,100,100,255" hoverTint="200,200,200,255" pressedTint="40,40,40,255"/></components>
              </element>
              <element type="UIElement" name="BtnCap" x="50" y="0" width="140" height="60" fontSize="24">
                <components><component type="UITextComponent" text="Go" textColor="100,100,100,255" hoverTextColor="200,200,200,255" align="Center" verticalAlign="Center"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Plain" x="10" y="100" width="200" height="60" color="40,40,40,255" hoverColor="80,80,80,255">
            <components><component type="UIInputClicker" action="Plain"/></components>
            <children>
              <element type="UIElement" name="PlainIcon" x="0" y="10" width="40" height="40">
                <components><component type="UIImageComponent" tint="100,100,100,255" hoverTint="200,200,200,255"/></components>
              </element>
            </children>
          </element>
          <element type="UIDropdown" name="Pick" x="300" y="10" width="180" height="56" color="40,40,40,255" stateDuration="1">
            <components><component type="UITextComponent" text="Pick one" verticalAlign="Center" wrap="false"/></components>
          </element>
          <element type="UIElement" name="Off" x="10" y="200" width="200" height="60" color="40,40,40,255" hoverColor="80,80,80,255" stateDuration="0">
            <components><component type="UIInputClicker" action="Off"/></components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr double kStep = 0.25;
        constexpr float kEpsilon = 1e-3f;
        constexpr uint32_t kPictureSide = 32;
        constexpr int kFrames = 8;

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        SDL_MouseMotionEvent Motion(float x, float y) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.x = x;
            event.y = y;
            return event;
        }

    }  // namespace

    class UIStateTransitionTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _savedSettings = CoreManager::GetUIEnvironment().GetSettings();
            CoreManager::GetTime().Reset();
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kTransitionXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _manager->SetActiveScene(PoolString::Intern("Main"));
            _canvas = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);

            const Vector<uint8_t> pixels(static_cast<size_t>(kPictureSide) * kPictureSide * 4, 200);
            _texture = _renderer->CreateTexture(pixels.data(), kPictureSide, kPictureSide);
            for (const char* icon : {"BtnIcon", "PlainIcon"}) {
                Get(icon).GetComponent<UIImageComponent>()->GetSprite().SetTexture(_texture);
            }
        }

        void TearDown() override {
            CoreManager::GetUIEnvironment().SetSettings(_savedSettings);
            CoreManager::GetTime().Reset();
            _texture.Reset();
            _canvas.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        /// Lets @p seconds of game time pass, one frame at every quarter second, and leaves the calls of the last frame
        void Wait(double seconds) {
            for (double passed = 0.0; passed < seconds - 1e-9; passed += kStep) {
                CoreManager::GetTime().Advance(kStep);
                Draw();
            }
        }

        void SetStateTransition(float seconds) {
            UISettings settings = _savedSettings;
            settings.stateTransition = seconds;
            CoreManager::GetUIEnvironment().SetSettings(settings);
        }

        UIElement& Get(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return *element;
        }

        static Vec2 CenterOf(const UIElement& element) {
            return {element.rect.x + element.rect.w * 0.5f, element.rect.y + element.rect.h * 0.5f};
        }

        /// The pointer comes to @p element and the frame is drawn at the same game time
        void Hover(const UIElement& element) {
            const Vec2 at = CenterOf(element);
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(at.x, at.y));
            Draw();
        }

        void Press(const UIElement& element) {
            const Vec2 at = CenterOf(element);
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true));
            Draw();
        }

        static bool Inside(const Rect& outer, const Rect& inner) {
            return inner.x >= outer.x - kEpsilon && inner.y >= outer.y - kEpsilon && inner.x + inner.w <= outer.x + outer.w + kEpsilon && inner.y + inner.h <= outer.y + outer.h + kEpsilon;
        }

        /// The red channel of the fill of @p element in the last frame (the plain rect of its box); -1 when there is none
        int Fill(const UIElement& element) const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && std::abs(call.rect.x - element.rect.x) < kEpsilon && std::abs(call.rect.y - element.rect.y) < kEpsilon &&
                    std::abs(call.rect.w - element.rect.w) < kEpsilon && call.rect.h > UIStateLook::kSelectedBarHeight) {
                    return call.color.r;
                }
            }
            return -1;
        }

        /// The red channel of the picture drawn inside @p element; -1 when there is none
        int PictureTint(const UIElement& element) const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.texture == _texture.Get() && Inside(element.rect, call.rect)) {
                    return call.color.r;
                }
            }
            return -1;
        }

        /// The red channel of the last glyph drawn inside @p element; -1 when there is none
        int TextColor(const UIElement& element) const {
            int found = -1;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.texture != _texture.Get() && Inside(element.rect, call.rect)) {
                    found = call.color.r;
                }
            }
            return found;
        }

        UISettings _savedSettings;
        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
        IntrusivePtr<ITexture> _texture;
    };

    TEST_F(UIStateTransitionTest, AHoveredFillEasesFromTheRestToTheHoverOverItsDuration) {
        Draw();
        ASSERT_EQ(Fill(Get("Btn")), 40);
        Hover(Get("Btn"));
        EXPECT_EQ(Fill(Get("Btn")), 40) << "the change starts from what was shown";
        Wait(0.5);
        EXPECT_EQ(Fill(Get("Btn")), 70) << "QuadOut: three quarters of the way at half the time";
        Wait(0.5);
        EXPECT_EQ(Fill(Get("Btn")), 80);
        Wait(0.5);
        EXPECT_EQ(Fill(Get("Btn")), 80) << "and stays";
    }

    TEST_F(UIStateTransitionTest, TheAnimationIsOnGameTimeNotOnFrames) {
        Draw();
        Hover(Get("Btn"));
        CoreManager::GetTime().Advance(kStep);
        CoreManager::GetTime().Advance(kStep);
        Draw();  // two clock steps and one frame: the same half second
        EXPECT_EQ(Fill(Get("Btn")), 70);
        Draw();
        Draw();  // frames at the same game time change nothing
        EXPECT_EQ(Fill(Get("Btn")), 70);
    }

    TEST_F(UIStateTransitionTest, APressDuringAHoverBlendContinuesFromWhatIsShown) {
        Draw();
        Hover(Get("Btn"));
        Wait(0.5);
        ASSERT_EQ(Fill(Get("Btn")), 70);
        Press(Get("Btn"));
        EXPECT_EQ(Fill(Get("Btn")), 70) << "no jump to the pressed fill";
        Wait(1.0);
        EXPECT_EQ(Fill(Get("Btn")), 20);
    }

    TEST_F(UIStateTransitionTest, ThePictureAndTheCaptionOfAButtonFollowItsOwnerTransition) {
        Draw();
        ASSERT_EQ(PictureTint(Get("BtnIcon")), 100);
        ASSERT_EQ(TextColor(Get("BtnCap")), 100);
        Hover(Get("Btn"));
        EXPECT_EQ(PictureTint(Get("BtnIcon")), 100);
        EXPECT_EQ(TextColor(Get("BtnCap")), 100);
        Wait(0.5);
        EXPECT_EQ(PictureTint(Get("BtnIcon")), 175) << "100 + 0.75 * (200 - 100)";
        EXPECT_EQ(TextColor(Get("BtnCap")), 175);
        Wait(0.5);
        EXPECT_EQ(PictureTint(Get("BtnIcon")), 200);
        EXPECT_EQ(TextColor(Get("BtnCap")), 200);
    }

    TEST_F(UIStateTransitionTest, WithNoDurationTheDiscreteLookIsDrawnInTheSameFrame) {
        Draw();
        Hover(Get("Plain"));
        EXPECT_EQ(Fill(Get("Plain")), 80) << "the default setting is 0: the one-frame change";
        EXPECT_EQ(PictureTint(Get("PlainIcon")), 200);
    }

    TEST_F(UIStateTransitionTest, TheSettingIsTheDefaultOfAnElementThatHasNoDurationOfItsOwn) {
        SetStateTransition(1.0f);
        Draw();
        Hover(Get("Plain"));
        Wait(0.5);
        EXPECT_EQ(Fill(Get("Plain")), 70);
        EXPECT_EQ(PictureTint(Get("PlainIcon")), 175) << "the child follows its owner's duration, which is the setting here";
    }

    TEST_F(UIStateTransitionTest, ADurationOfZeroTurnsTheElementOffWhateverTheSetting) {
        SetStateTransition(1.0f);
        Draw();
        Hover(Get("Off"));
        EXPECT_EQ(Fill(Get("Off")), 80);
    }

    TEST_F(UIStateTransitionTest, TheFirstDrawSnapsToTheStateTheElementIsIn) {
        SetStateTransition(1.0f);
        Get("Plain").hovered = true;  // the screen opens under the pointer
        Draw();
        EXPECT_EQ(Fill(Get("Plain")), 80);
        EXPECT_EQ(PictureTint(Get("PlainIcon")), 200);
        Wait(0.5);
        EXPECT_EQ(Fill(Get("Plain")), 80);
    }

    TEST_F(UIStateTransitionTest, AnElementTurnedOffForgetsItsBlend) {
        Draw();
        Hover(Get("Btn"));
        Wait(0.5);
        ASSERT_EQ(Fill(Get("Btn")), 70);
        Get("Btn").stateDuration = 0.0f;
        Draw();
        EXPECT_EQ(Fill(Get("Btn")), 80) << "off means the discrete look at once";
        Get("Btn").stateDuration = 1.0f;
        Draw();
        EXPECT_EQ(Fill(Get("Btn")), 80) << "turned on again, it starts from where it is and does not replay the old blend";
    }

    TEST_F(UIStateTransitionTest, TheHeaderBoxOfADropdownEasesToo) {
        Draw();
        ASSERT_EQ(Fill(Get("Pick")), 40);
        Hover(Get("Pick"));
        const int hovered = Get("Pick").Cast<UIDropdown>()->GetCurrentFill().r;
        ASSERT_GT(hovered, 40);
        EXPECT_EQ(Fill(Get("Pick")), 40);
        Wait(0.5);
        EXPECT_NEAR(Fill(Get("Pick")), 40.0f + 0.75f * static_cast<float>(hovered - 40), 1.0f);
        Wait(0.5);
        EXPECT_EQ(Fill(Get("Pick")), hovered);
    }

    TEST_F(UIStateTransitionTest, GetStateLookStaysThePureLookOfTheStateNow) {
        Draw();
        Hover(Get("Btn"));
        Wait(0.25);
        EXPECT_EQ(Get("Btn").GetStateLook().fill.r, 80) << "a caller that does not draw gets the look of the state, not the blend";
    }

    TEST_F(UIStateTransitionTest, AnOutOfRangeDurationIsKeptInsideTheSetting) {
        EXPECT_FLOAT_EQ(UISettings::ClampStateTransition(-1.0f), 0.0f);
        EXPECT_FLOAT_EQ(UISettings::ClampStateTransition(std::numeric_limits<float>::quiet_NaN()), 0.0f);
        EXPECT_FLOAT_EQ(UISettings::ClampStateTransition(1000.0f), UISettings::kMaxStateTransition);
        Get("Btn").stateDuration = 1000.0f;
        EXPECT_FLOAT_EQ(Get("Btn").GetStateDuration(), UISettings::kMaxStateTransition);
        Get("Btn").stateDuration = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FLOAT_EQ(Get("Btn").GetStateDuration(), 0.0f) << "a NaN is the setting";
    }

    TEST_F(UIStateTransitionTest, AFrameInWhichNothingChangedAllocatesNothing) {
        Draw();
        Hover(Get("Btn"));
        Wait(0.5);  // the blend is on its way, the glyphs are laid out, the calls vector has its capacity
        Wait(0.25);
#if BE_PROFILER_ALLOC
        const uint64_t before = AllocTracker::GetThreadCounters().count;
#endif
        for (int frame = 0; frame < kFrames; ++frame) {
            CoreManager::GetTime().Advance(kStep);
            Draw();
        }
#if BE_PROFILER_ALLOC
        EXPECT_EQ(AllocTracker::GetThreadCounters().count, before);
#endif
        EXPECT_EQ(Fill(Get("Btn")), 80);
    }

}  // namespace BECore::Tests
