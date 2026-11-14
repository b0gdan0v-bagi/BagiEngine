#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UITooltip.h>
#include <BECore/UI/UITooltipComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SubscriptionHolder.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Two screens in window pixels (output 800 x 600, scale 1). Hud (layer 10):
        //   Icon  (100, 100, 48 x 48)   a tooltip
        //   Plain (300, 100, 48 x 48)   none
        //   Off   (500, 100, 48 x 48)   a tooltip, disabled
        //   Lang  (100, 300, 48 x 48)   a tooltip of a string key
        //   Edge  (752, 540, 48 x 48)   a tooltip, in the corner of the screen
        //   Under (50, 170, 300 x 48)   the plate of Icon is drawn over it
        //   Comp, CompLang, CompBare (y 420)   composed elements (a UIInputClicker): a UITooltipComponent, one of a string key, none
        //   Panel (700, 420)  blocksInput and a UITooltipComponent; Ghost (700, 480)  a UITooltipComponent and no hit target
        // Modal (layer 20, hidden until a test shows it): modal, one button with a tooltip at (400, 400)
        constexpr const char* kTooltipXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Icon" x="100" y="100" width="48" height="48" color="30,30,30,255">
            <components>
              <component type="UIInputClicker" action="OpenIcon"/>
              <component type="UITooltipComponent" text="Settings"/>
            </components>
          </element>
          <element type="UIElement" name="Plain" x="300" y="100" width="48" height="48" color="30,30,30,255">
            <components>
              <component type="UIInputClicker" action="OpenPlain"/>
            </components>
          </element>
          <element type="UIElement" name="Off" x="500" y="100" width="48" height="48" color="30,30,30,255">
            <components>
              <component type="UIInputClicker" action="OpenOff" disabled="true"/>
              <component type="UITooltipComponent" text="Not now"/>
            </components>
          </element>
          <element type="UIElement" name="Lang" x="100" y="300" width="48" height="48" color="30,30,30,255">
            <components>
              <component type="UIInputClicker" action="OpenLang"/>
              <component type="UITooltipComponent" text="{#tip.settings}"/>
            </components>
          </element>
          <element type="UIElement" name="Edge" x="752" y="540" width="48" height="48" color="30,30,30,255">
            <components>
              <component type="UIInputClicker" action="OpenEdge"/>
              <component type="UITooltipComponent" text="Settings of the game"/>
            </components>
          </element>
          <element type="UIElement" name="Under" x="50" y="170" width="300" height="48" color="30,30,30,255">
            <components>
              <component type="UIInputClicker" action="OpenUnder"/>
            </components>
          </element>
          <element type="UIElement" name="Comp" x="100" y="420" width="48" height="48" color="30,30,30,255">
            <components><component type="UIInputClicker" action="OpenComp"/><component type="UITooltipComponent" text="Composed tip"/></components>
          </element>
          <element type="UIElement" name="CompLang" x="300" y="420" width="48" height="48" color="30,30,30,255">
            <components><component type="UIInputClicker" action="OpenCompLang"/><component type="UITooltipComponent" text="{#tip.settings}"/></components>
          </element>
          <element type="UIElement" name="CompBare" x="500" y="420" width="48" height="48" color="30,30,30,255">
            <components><component type="UIInputClicker" action="OpenCompBare"/></components>
          </element>
          <element type="UIElement" name="Panel" blocksInput="true" x="700" y="420" width="48" height="48" color="30,30,30,255">
            <components><component type="UITooltipComponent" text="Panel tip"/></components>
          </element>
          <element type="UIElement" name="Ghost" x="700" y="480" width="48" height="48" color="30,30,30,255">
            <components><component type="UITooltipComponent" text="Ghost tip"/></components>
          </element>
        </elements>
      </component>
    </components></node>
    <node name="Modal" layer="20"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize" modal="true" visible="false">
        <elements>
          <element type="UIElement" name="Ok" x="400" y="400" width="48" height="48" color="30,30,30,255">
            <components>
              <component type="UIInputClicker" action="ModalOk"/>
              <component type="UITooltipComponent" text="Modal tip"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr float kEpsilon = 1e-3f;
        constexpr double kStep = 0.125;  // an exact binary fraction, so a sum of steps is exact too; a frame is clamped to 0.25 s anyway

        SDL_MouseButtonEvent Button(float x, float y, bool down, bool touch) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.which = touch ? SDL_TOUCH_MOUSEID : 0;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        SDL_MouseMotionEvent Motion(float x, float y, bool touch, bool held) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.which = touch ? SDL_TOUCH_MOUSEID : 0;
            event.x = x;
            event.y = y;
            event.state = held ? SDL_BUTTON_LMASK : 0;
            return event;
        }

        class ActionRecorder : public SubscriptionHolder {
        public:
            ActionRecorder() {
                Subscribe<InputEvents::ActionEvent, &ActionRecorder::OnAction>(this);
            }
            [[nodiscard]] int Count(const char* action) const {
                return static_cast<int>(std::count(_actions.begin(), _actions.end(), PoolString::Intern(action)));
            }
            [[nodiscard]] size_t Total() const {
                return _actions.size();
            }

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                _actions.push_back(event.action);
            }
            eastl::vector<PoolString> _actions;
        };

        bool Inside(const Rect& inner, const Rect& outer) {
            return inner.x >= outer.x - kEpsilon && inner.y >= outer.y - kEpsilon && inner.x + inner.w <= outer.x + outer.w + kEpsilon && inner.y + inner.h <= outer.y + outer.h + kEpsilon;
        }

    }  // namespace

    class UICanvasTooltipTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
            CoreManager::GetUIStrings().Set("en"_intern, "tip.settings", "Settings");
            CoreManager::GetUIStrings().Set("ru"_intern, "tip.settings", "Настройки");
            CoreManager::GetUIStrings().SetLanguage("en"_intern);
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetTime().Reset();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kTooltipXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _manager->SetActiveScene(PoolString::Intern("Main"));
            const auto& nodes = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren();
            _hud = nodes[0]->GetComponent<UICanvasComponent>();
            _modal = nodes[1]->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_hud);
            ASSERT_TRUE(_modal);
            Draw();
        }

        void TearDown() override {
            CoreManager::GetUIStrings().Clear();
            _hud.Reset();
            _modal.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        /// Lets @p seconds of game time pass, drawing a frame at every step (a frame is clamped to 0.25 s), and leaves the calls of the last frame
        void Wait(double seconds) {
            for (double passed = 0.0; passed < seconds - 1e-9; passed += kStep) {
                CoreManager::GetTime().Advance(kStep);
                Draw();
            }
        }

        static bool Motion(float x, float y, bool touch = false, bool held = false) {
            return CoreManager::GetUIInput().DispatchMouseMotion(BECore::Tests::Motion(x, y, touch, held));
        }
        static bool Press(float x, float y, bool touch = false) {
            return CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true, touch));
        }
        static bool Release(float x, float y, bool touch = false) {
            return CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false, touch));
        }

        UIElement& Get(const char* name) const {
            return GetElement(name);
        }

        UIElement& GetElement(const char* name) const {
            UIElement* element = _hud->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return *element;
        }

        static Rect CenterOf(const Rect& rect) {
            return {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, 0.0f, 0.0f};
        }

        /// The plate of the tooltip in the last frame: the one rect of its colour
        Optional<Rect> Plate() const {
            Optional<Rect> plate;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && ColorsEqual(call.color, UITooltip::kPlateColor)) {
                    EXPECT_FALSE(plate.has_value()) << "one tooltip at a time";
                    plate = call.rect;
                }
            }
            return plate;
        }

        /// The glyphs of the last frame that lie inside @p plate
        size_t GlyphsIn(const Rect& plate) const {
            size_t count = 0;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && Inside(call.rect, plate)) {
                    ++count;
                }
            }
            return count;
        }

        /// The pointer comes to a button and rests there until its tooltip is due; the last frame is the first one that shows it
        void HoverUntilShown(const char* name) {
            const Rect at = CenterOf(GetElement(name).rect);
            Motion(at.x, at.y);
            Wait(UITooltip::kHoverDelay);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _hud;
        IntrusivePtr<UICanvasComponent> _modal;
    };

    // ---- the mouse -------------------------------------------------------------------------------------------------------------

    TEST_F(UICanvasTooltipTest, ARestingMouseShowsTheTooltipAfterTheDelayAndNotBefore) {
        const Rect icon = Get("Icon").rect;
        const Rect at = CenterOf(icon);
        Motion(at.x, at.y);
        Wait(UITooltip::kHoverDelay - kStep);
        EXPECT_FALSE(Plate()) << "not yet";

        Wait(kStep);
        const auto plate = Plate();
        ASSERT_TRUE(plate.has_value());
        EXPECT_GE(plate->y, icon.y + icon.h) << "under the button";
        EXPECT_TRUE(Inside(*plate, {0.0f, 0.0f, 800.0f, 600.0f}));
        EXPECT_GE(GlyphsIn(*plate), 8u) << "the text of the tooltip is drawn on the plate";
    }

    TEST_F(UICanvasTooltipTest, AButtonWithNoTooltipNeverShowsOne) {
        HoverUntilShown("Plain");
        Wait(3.0);

        EXPECT_FALSE(Plate());
    }

    TEST_F(UICanvasTooltipTest, ADisabledButtonStillExplainsItself) {
        HoverUntilShown("Off");

        EXPECT_TRUE(Plate().has_value());
    }

    TEST_F(UICanvasTooltipTest, MovingAwayHidesItAndComingBackNeedsTheDelayAgain) {
        HoverUntilShown("Icon");
        ASSERT_TRUE(Plate().has_value());

        Motion(700.0f, 500.0f);
        Draw();
        EXPECT_FALSE(Plate());

        const Rect at = CenterOf(Get("Icon").rect);
        Motion(at.x, at.y);
        Draw();
        EXPECT_FALSE(Plate()) << "back on the button: the delay starts again";
        Wait(UITooltip::kHoverDelay);
        EXPECT_TRUE(Plate().has_value());
    }

    TEST_F(UICanvasTooltipTest, MovingToAnotherButtonWithATooltipStartsTheDelayAgain) {
        HoverUntilShown("Icon");
        ASSERT_TRUE(Plate().has_value());

        const Rect lang = CenterOf(Get("Lang").rect);
        Motion(lang.x, lang.y);
        Draw();
        EXPECT_FALSE(Plate());
        Wait(UITooltip::kHoverDelay);
        const auto plate = Plate();
        ASSERT_TRUE(plate.has_value());
        EXPECT_GE(plate->y, Get("Lang").rect.y) << "the plate of Lang, not the old one";
    }

    TEST_F(UICanvasTooltipTest, APressHidesItAndTheClickStillFires) {
        ActionRecorder actions;
        HoverUntilShown("Icon");
        ASSERT_TRUE(Plate().has_value());

        const Rect at = CenterOf(Get("Icon").rect);
        EXPECT_TRUE(Press(at.x, at.y));
        Draw();
        EXPECT_FALSE(Plate());
        EXPECT_TRUE(Release(at.x, at.y));

        EXPECT_EQ(actions.Count("OpenIcon"), 1);
        Wait(3.0);
        EXPECT_FALSE(Plate()) << "released on the button: it stays hidden until the pointer has left and come back";
    }

    TEST_F(UICanvasTooltipTest, APressOfAnotherButtonOfTheMouseAlsoHidesIt) {
        HoverUntilShown("Icon");
        ASSERT_TRUE(Plate().has_value());

        const Rect at = CenterOf(Get("Icon").rect);
        CoreManager::GetUIInput().DispatchMouseButtonDown([&] {
            SDL_MouseButtonEvent event = Button(at.x, at.y, true, false);
            event.button = SDL_BUTTON_RIGHT;
            return event;
        }());
        Draw();
        EXPECT_FALSE(Plate());
    }

    TEST_F(UICanvasTooltipTest, ThePlateStaysInsideTheScreenForAButtonInTheCorner) {
        HoverUntilShown("Edge");

        const auto plate = Plate();
        ASSERT_TRUE(plate.has_value());
        EXPECT_TRUE(Inside(*plate, {0.0f, 0.0f, 800.0f, 600.0f}));
        const Rect edge = Get("Edge").rect;
        EXPECT_LE(plate->y + plate->h, edge.y - UITooltip::kGap + kEpsilon) << "no room below: above the button";
    }

    // ---- the plate is no part of the screen ---------------------------------------------------------------------------------------

    TEST_F(UICanvasTooltipTest, ThePlateIsDrawnLastAndIsNoElementOfTheTree) {
        const size_t elements = _hud->GetRoot().children.size();
        HoverUntilShown("Icon");
        const auto plate = Plate();
        ASSERT_TRUE(plate.has_value());

        EXPECT_EQ(_hud->GetRoot().children.size(), elements);
        const DrawCall& last = _renderer->calls.back();
        EXPECT_TRUE(Inside(last.rect, *plate)) << "the last call of the frame is the text of the plate";
        // The top strip of the plate is above Under and below Icon: no element there, plate or not
        EXPECT_EQ(_hud->GetLayout().HitTest(plate->x + plate->w * 0.5f, plate->y + 2.0f), nullptr) << "not hit-tested";
    }

    TEST_F(UICanvasTooltipTest, AClickAimedAtWhatLiesUnderThePlateGoesThere) {
        ActionRecorder actions;
        HoverUntilShown("Icon");
        const auto plate = Plate();
        ASSERT_TRUE(plate.has_value());

        // The plate covers the top of Under; a point of both is a click on Under
        const Rect under = Get("Under").rect;
        const float x = plate->x + plate->w * 0.5f;
        const float y = under.y + 4.0f;
        ASSERT_LT(y, plate->y + plate->h) << "the point is under the plate";
        EXPECT_TRUE(Press(x, y));
        EXPECT_TRUE(Release(x, y));
        EXPECT_EQ(actions.Count("OpenUnder"), 1);
    }

    TEST_F(UICanvasTooltipTest, APointOnThePlateWithNothingUnderItIsNotConsumed) {
        HoverUntilShown("Icon");
        const auto plate = Plate();
        ASSERT_TRUE(plate.has_value());

        const float x = plate->x + plate->w * 0.5f;
        const float y = plate->y + 2.0f;  // above Under, below Icon
        ASSERT_LT(y, Get("Under").rect.y);
        EXPECT_FALSE(Press(x, y));
        EXPECT_FALSE(Release(x, y));
    }

    // ---- a finger ------------------------------------------------------------------------------------------------------------------

    TEST_F(UICanvasTooltipTest, AFingerHeldPastTheLongPressShowsTheTooltipAndItsReleaseIsNoClick) {
        ActionRecorder actions;
        const Rect at = CenterOf(Get("Icon").rect);
        Motion(at.x, at.y, true);
        EXPECT_TRUE(Press(at.x, at.y, true));
        Wait(UITooltip::kLongPress - kStep);
        EXPECT_FALSE(Plate()) << "not yet";

        Wait(kStep);
        ASSERT_TRUE(Plate().has_value());
        EXPECT_TRUE(Release(at.x, at.y, true));

        EXPECT_EQ(actions.Total(), 0u) << "the long press is not a click";
        EXPECT_FALSE(Get("Icon").pressed);
        Draw();
        EXPECT_TRUE(Plate().has_value()) << "it lingers after the release";
    }

    TEST_F(UICanvasTooltipTest, ATouchTooltipGoesAfterTheLinger) {
        const Rect at = CenterOf(Get("Icon").rect);
        Motion(at.x, at.y, true);
        Press(at.x, at.y, true);
        Wait(UITooltip::kLongPress);
        Release(at.x, at.y, true);

        Wait(UITooltip::kTouchLinger - kStep);
        EXPECT_TRUE(Plate().has_value());
        Wait(kStep);
        EXPECT_FALSE(Plate());
        Wait(3.0);
        EXPECT_FALSE(Plate()) << "a finger that rests on a button is no hover";
    }

    TEST_F(UICanvasTooltipTest, AShortTouchClicksAndShowsNothing) {
        ActionRecorder actions;
        const Rect at = CenterOf(Get("Icon").rect);
        Motion(at.x, at.y, true);
        Press(at.x, at.y, true);
        Wait(UITooltip::kLongPress - kStep);
        EXPECT_FALSE(Plate());
        EXPECT_TRUE(Release(at.x, at.y, true));

        EXPECT_EQ(actions.Count("OpenIcon"), 1);
        Wait(3.0);
        EXPECT_FALSE(Plate());
    }

    TEST_F(UICanvasTooltipTest, ALongPressDoesNotSwallowTheNextShortTouch) {
        ActionRecorder actions;
        const Rect at = CenterOf(Get("Icon").rect);
        Motion(at.x, at.y, true);
        Press(at.x, at.y, true);
        Wait(UITooltip::kLongPress);
        Release(at.x, at.y, true);
        ASSERT_EQ(actions.Total(), 0u);

        Press(at.x, at.y, true);
        Release(at.x, at.y, true);
        EXPECT_EQ(actions.Count("OpenIcon"), 1);
    }

    TEST_F(UICanvasTooltipTest, AFingerThatSlidesOffTheButtonShowsNothingAndClicksNothing) {
        ActionRecorder actions;
        const Rect at = CenterOf(Get("Icon").rect);
        Motion(at.x, at.y, true);
        Press(at.x, at.y, true);
        Wait(UITooltip::kLongPress - kStep);
        Motion(700.0f, 500.0f, true, true);
        Wait(UITooltip::kLongPress * 2.0);
        EXPECT_FALSE(Plate());
        Release(700.0f, 500.0f, true);

        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UICanvasTooltipTest, AMouseClickAfterALongTouchStillFires) {
        ActionRecorder actions;
        const Rect at = CenterOf(Get("Icon").rect);
        Motion(at.x, at.y, true);
        Press(at.x, at.y, true);
        Wait(UITooltip::kLongPress);
        Release(at.x, at.y, true);

        Motion(at.x, at.y);
        Press(at.x, at.y);
        Release(at.x, at.y);
        EXPECT_EQ(actions.Count("OpenIcon"), 1);
    }

    // ---- a canvas that cannot show it ----------------------------------------------------------------------------------------------

    TEST_F(UICanvasTooltipTest, AHiddenCanvasClearsItAndDoesNotResumeWhenShownAgain) {
        HoverUntilShown("Icon");
        ASSERT_TRUE(Plate().has_value());

        _hud->_visible = false;
        Draw();
        EXPECT_FALSE(Plate());
        _hud->_visible = true;
        Draw();
        EXPECT_FALSE(Plate());
        Wait(3.0);
        EXPECT_FALSE(Plate()) << "no pointer event since: nothing to start a hover";
    }

    TEST_F(UICanvasTooltipTest, ACanvasThatLosesInputClearsIt) {
        HoverUntilShown("Icon");
        ASSERT_TRUE(Plate().has_value());

        _hud->_inputEnabled = false;
        Draw();
        EXPECT_FALSE(Plate());
    }

    TEST_F(UICanvasTooltipTest, ACanvasBelowAModalOneNeverStartsATooltip) {
        _modal->_visible = true;
        Draw();
        const Rect at = CenterOf(Get("Icon").rect);
        EXPECT_TRUE(Motion(at.x, at.y)) << "the modal canvas takes the motion";
        Wait(UITooltip::kHoverDelay * 2.0);

        EXPECT_FALSE(Plate());
    }

    TEST_F(UICanvasTooltipTest, AModalCanvasShowsTheTooltipOfItsOwnButton) {
        _modal->_visible = true;
        Draw();
        Motion(424.0f, 424.0f);
        Wait(UITooltip::kHoverDelay);

        const auto plate = Plate();
        ASSERT_TRUE(plate.has_value());
        EXPECT_GE(plate->y, 448.0f) << "under the Ok button";
    }

    // ---- the text ------------------------------------------------------------------------------------------------------------------

    TEST_F(UICanvasTooltipTest, ATooltipOfAStringKeyFollowsTheLanguage) {
        HoverUntilShown("Lang");
        const auto english = Plate();
        ASSERT_TRUE(english.has_value());
        EXPECT_EQ(GlyphsIn(*english), 8u) << "Settings";

        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("ru"_intern));
        Draw();
        const auto russian = Plate();
        ASSERT_TRUE(russian.has_value()) << "a language switch does not restart the hover";
        EXPECT_EQ(GlyphsIn(*russian), 9u) << "Настройки";
        EXPECT_NE(russian->w, english->w);
    }

    TEST_F(UICanvasTooltipTest, ALongTooltipWrapsOnAPlateNoWiderThanTheLimit) {
        Get("Lang").GetComponent<UITooltipComponent>()->text = "Settings of the game: the sound, the language and the size of everything on the screen";
        HoverUntilShown("Lang");

        const auto plate = Plate();
        ASSERT_TRUE(plate.has_value());
        EXPECT_LE(plate->w, UITooltip::kMaxWidth + 2.0f * UITooltip::kPadding + kEpsilon);
        EXPECT_GT(plate->h, UITooltip::kFontSize * 2.0f + 2.0f * UITooltip::kPadding) << "several lines";
        EXPECT_TRUE(Inside(*plate, {0.0f, 0.0f, 800.0f, 600.0f}));
    }

    TEST_F(UICanvasTooltipTest, ATooltipThatIsEmptiedWhileShownGoes) {
        HoverUntilShown("Icon");
        ASSERT_TRUE(Plate().has_value());

        Get("Icon").GetComponent<UITooltipComponent>()->text.clear();
        Draw();
        EXPECT_FALSE(Plate());
    }

    // ---- a composed element: the text of its UITooltipComponent --------------------------------------------------------------------

    TEST_F(UICanvasTooltipTest, AComposedElementShowsTheTooltipOfItsComponentAfterTheDelay) {
        const Rect comp = GetElement("Comp").rect;
        const Rect at = CenterOf(comp);
        Motion(at.x, at.y);
        Wait(UITooltip::kHoverDelay - kStep);
        EXPECT_FALSE(Plate()) << "not yet";

        Wait(kStep);
        const auto plate = Plate();
        ASSERT_TRUE(plate.has_value());
        EXPECT_TRUE(Inside(*plate, {0.0f, 0.0f, 800.0f, 600.0f}));
        EXPECT_EQ(GlyphsIn(*plate), 11u) << "Composed tip";
    }

    TEST_F(UICanvasTooltipTest, AComposedElementWithNoTooltipComponentShowsNone) {
        HoverUntilShown("CompBare");
        Wait(3.0);

        EXPECT_FALSE(Plate());
    }

    TEST_F(UICanvasTooltipTest, ABlocksInputElementShowsItsTooltipComponentAndAnElementWithNoHitTargetShowsNone) {
        HoverUntilShown("Panel");
        const auto plate = Plate();
        ASSERT_TRUE(plate.has_value());
        EXPECT_EQ(GlyphsIn(*plate), 8u) << "Panel tip";

        HoverUntilShown("Ghost");
        Wait(3.0);
        EXPECT_FALSE(Plate()) << "the canvas asks only hit-testable elements";
    }

    TEST_F(UICanvasTooltipTest, ATooltipOfAComposedElementFollowsTheLanguageToo) {
        HoverUntilShown("CompLang");
        const auto english = Plate();
        ASSERT_TRUE(english.has_value());
        EXPECT_EQ(GlyphsIn(*english), 8u) << "Settings";

        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("ru"_intern));
        Draw();
        const auto russian = Plate();
        ASSERT_TRUE(russian.has_value());
        EXPECT_EQ(GlyphsIn(*russian), 9u) << "Настройки";
    }

    TEST_F(UICanvasTooltipTest, AFingerHeldOnAComposedElementShowsItsTooltipAndTheReleaseIsNoClick) {
        ActionRecorder actions;
        const Rect at = CenterOf(GetElement("Comp").rect);
        Motion(at.x, at.y, true);
        Press(at.x, at.y, true);
        Wait(UITooltip::kLongPress);
        ASSERT_TRUE(Plate().has_value());
        Release(at.x, at.y, true);
        EXPECT_EQ(actions.Total(), 0u) << "the long press is not a click";

        Press(at.x, at.y, true);
        Release(at.x, at.y, true);
        EXPECT_EQ(actions.Count("OpenComp"), 1) << "a short touch clicks";
    }

    TEST_F(UICanvasTooltipTest, ATooltipComponentTextThatIsEmptiedWhileShownGoes) {
        HoverUntilShown("Comp");
        ASSERT_TRUE(Plate().has_value());

        GetElement("Comp").GetComponent<UITooltipComponent>()->text.clear();
        Draw();
        EXPECT_FALSE(Plate());
    }

}  // namespace BECore::Tests
