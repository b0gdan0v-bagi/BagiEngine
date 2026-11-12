#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UISwitchComponent.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UIToggleComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UIPartShapeChecks.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SubscriptionHolder.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // A Screen canvas in window pixels (output 800 x 600, scale 1). Every row is a composed toggle 56 high: a horizontal stack of a caption (flex 1) and a switch (height 100%, aspect 1.25)
        //   Sound    (10, 10, 300 wide)   bind soundOn, the defaults "true" / "false", a plate
        //   Custom   (10, 100, 300 wide)  bind mode, onValue "1", offValue "0", explicit colours on the switch
        //   NoAction (10, 200, 300 wide)  bind soundOn, no action
        //   Off      (10, 300, 300 wide)  the clicker is disabled
        //   Round    (10, 400, 300 wide)  partShape Round
        //   Unbound  (10, 500, 300 wide)  no bind
        //   Long     (330, 10, 220 wide)  a caption far too long for the room that the switch leaves
        constexpr const char* kToggleXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Sound" x="10" y="10" width="300" height="56" color="30,30,30,255" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
            <components>
              <component type="UIInputClicker"/>
              <component type="UIToggleComponent" bind="soundOn" action="ToggleCompSound"/>
            </components>
            <children>
              <element type="UIElement" name="SoundCaption" flex="1" height="100%" fontSize="24">
                <components><component type="UITextComponent" text="Sound" verticalAlign="Center" wrap="false"/></components>
              </element>
              <element type="UIElement" name="SoundSwitch" height="100%" aspectRatio="1.25">
                <components><component type="UISwitchComponent"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Custom" x="10" y="100" width="300" height="56" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
            <components>
              <component type="UIInputClicker"/>
              <component type="UIToggleComponent" bind="mode" action="ToggleCompCustom" onValue="1" offValue="0"/>
            </components>
            <children>
              <element type="UIElement" name="CustomCaption" flex="1" height="100%" fontSize="24">
                <components><component type="UITextComponent" text="Custom" verticalAlign="Center" wrap="false"/></components>
              </element>
              <element type="UIElement" name="CustomSwitch" height="100%" aspectRatio="1.25">
                <components><component type="UISwitchComponent" trackColor="10,20,30,255" fillColor="40,50,60,255" thumbColor="70,80,90,255"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="NoAction" x="10" y="200" width="300" height="56" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
            <components>
              <component type="UIInputClicker"/>
              <component type="UIToggleComponent" bind="soundOn"/>
            </components>
            <children>
              <element type="UIElement" name="NoActionCaption" flex="1" height="100%" fontSize="24">
                <components><component type="UITextComponent" text="No action" verticalAlign="Center" wrap="false"/></components>
              </element>
              <element type="UIElement" name="NoActionSwitch" height="100%" aspectRatio="1.25">
                <components><component type="UISwitchComponent"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Off" x="10" y="300" width="300" height="56" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
            <components>
              <component type="UIInputClicker" disabled="true"/>
              <component type="UIToggleComponent" bind="soundOn" action="ToggleCompOff"/>
            </components>
            <children>
              <element type="UIElement" name="OffCaption" flex="1" height="100%" fontSize="24">
                <components><component type="UITextComponent" text="Disabled" verticalAlign="Center" wrap="false"/></components>
              </element>
              <element type="UIElement" name="OffSwitch" height="100%" aspectRatio="1.25">
                <components><component type="UISwitchComponent"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Round" x="10" y="400" width="300" height="56" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
            <components>
              <component type="UIInputClicker"/>
              <component type="UIToggleComponent" bind="soundOn" action="ToggleCompRound"/>
            </components>
            <children>
              <element type="UIElement" name="RoundCaption" flex="1" height="100%" fontSize="24">
                <components><component type="UITextComponent" text="Round" verticalAlign="Center" wrap="false"/></components>
              </element>
              <element type="UIElement" name="RoundSwitch" height="100%" aspectRatio="1.25">
                <components><component type="UISwitchComponent" partShape="Round"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Unbound" x="10" y="500" width="300" height="56" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
            <components>
              <component type="UIInputClicker"/>
              <component type="UIToggleComponent" action="ToggleCompUnbound"/>
            </components>
            <children>
              <element type="UIElement" name="UnboundCaption" flex="1" height="100%" fontSize="24">
                <components><component type="UITextComponent" text="Unbound" verticalAlign="Center" wrap="false"/></components>
              </element>
              <element type="UIElement" name="UnboundSwitch" height="100%" aspectRatio="1.25">
                <components><component type="UISwitchComponent"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Long" x="330" y="10" width="220" height="56" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
            <components>
              <component type="UIInputClicker"/>
              <component type="UIToggleComponent" bind="soundOn" action="ToggleCompLong"/>
            </components>
            <children>
              <element type="UIElement" name="LongCaption" flex="1" height="100%" fontSize="24">
                <components><component type="UITextComponent" text="A caption far too long for the room that the switch leaves" verticalAlign="Center" wrap="false"/></components>
              </element>
              <element type="UIElement" name="LongSwitch" height="100%" aspectRatio="1.25">
                <components><component type="UISwitchComponent"/></components>
              </element>
            </children>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr float kEpsilon = 1e-2f;
        constexpr float kRowHeight = 56.0f;
        constexpr float kCaptionGap = 8.0f;  // the spacing of the row in the XML above

        const Color kOffTrackAuthored{10, 20, 30, 255};
        const Color kOnTrackAuthored{40, 50, 60, 255};
        const Color kThumbAuthored{70, 80, 90, 255};

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        SDL_MouseMotionEvent Motion(float x, float y, SDL_MouseButtonFlags held = 0) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.x = x;
            event.y = y;
            event.state = held;
            return event;
        }

        struct Point {
            float x = 0.0f;
            float y = 0.0f;
        };

        bool Near(const Rect& a, const Rect& b, float tolerance = kEpsilon) {
            return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.w - b.w) <= tolerance && std::abs(a.h - b.h) <= tolerance;
        }

        bool Inside(const Rect& inner, const Rect& outer, float tolerance = 1e-3f) {
            return inner.x >= outer.x - tolerance && inner.y >= outer.y - tolerance && inner.x + inner.w <= outer.x + outer.w + tolerance && inner.y + inner.h <= outer.y + outer.h + tolerance;
        }

        /// The actions the toggles emit, with the value each carried, in order
        class ActionLog : public SubscriptionHolder {
        public:
            ActionLog() {
                Subscribe<InputEvents::ActionEvent, &ActionLog::OnAction>(this);
            }

            size_t Total() const {
                return _actions.size();
            }
            PoolString LastAction() const {
                return _actions.empty() ? PoolString{} : _actions.back();
            }
            PoolString LastValue() const {
                return _values.empty() ? PoolString{} : _values.back();
            }

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                _actions.push_back(event.action);
                _values.push_back(event.value);
            }

            eastl::vector<PoolString> _actions;
            eastl::vector<PoolString> _values;
        };

        void SetVar(const char* name, const char* value) {
            CoreManager::GetUIVars().Set(PoolString::Intern(name), value);
        }

        eastl::string ElementToXml(const UIElement& element) {
            XmlSerializer serializer;
            EXPECT_TRUE(serializer.BeginObject("element"));
            element.Serialize(serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        }

    }  // namespace

    class UIToggleComponentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kToggleXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _manager->SetActiveScene(PoolString::Intern("Main"));

            _canvas = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);
            Draw();
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            _canvas.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        UIElement& Get(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return *element;
        }

        /// The row called @p name; its toggle component
        UIToggleComponent& Toggle(const char* name) const {
            UIToggleComponent* toggle = Get(name).GetComponent<UIToggleComponent>();
            EXPECT_NE(toggle, nullptr) << name;
            return *toggle;
        }

        UISwitchComponent& Switch(const char* name) const {
            UISwitchComponent* component = Get((eastl::string(name) + "Switch").c_str()).GetComponent<UISwitchComponent>();
            EXPECT_NE(component, nullptr) << name;
            return *component;
        }

        /// The rect of the switch child of row @p name
        const Rect& SwitchRect(const char* name) const {
            return Get((eastl::string(name) + "Switch").c_str()).rect;
        }

        UISwitchComponent::Parts PartsOf(const char* name) const {
            return UISwitchComponent::GetParts(SwitchRect(name));
        }

        /// The rect call of the last draw that has exactly this destination, or null
        const DrawCall* FindRect(const Rect& wanted) const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && Near(call.rect, wanted)) {
                    return &call;
                }
            }
            return nullptr;
        }

        bool ThumbDrawnAtRight(const char* name) const {
            const auto parts = PartsOf(name);
            return FindRect(parts.thumbOn) != nullptr && FindRect(parts.thumbOff) == nullptr;
        }

        bool ThumbDrawnAtLeft(const char* name) const {
            const auto parts = PartsOf(name);
            return FindRect(parts.thumbOff) != nullptr && FindRect(parts.thumbOn) == nullptr;
        }

        static Point Middle(const Rect& rect) {
            return {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
        }

        /// A press and a release at @p at
        static void ClickAt(const Point& at) {
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(at.x, at.y));
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true));
            CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at.x, at.y, false));
        }

        void Click(const char* name) const {
            ClickAt(Middle(Get(name).rect));
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
    };

    // ---- on and off follow the variable -----------------------------------------------------------------------------------

    TEST_F(UIToggleComponentTest, ARowBuiltFromAFileHasItsDefaults) {
        const auto& sound = Toggle("Sound");
        EXPECT_EQ(sound.bind, PoolString::Intern("soundOn"));
        EXPECT_EQ(sound.onValue, PoolString::Intern("true"));
        EXPECT_EQ(sound.offValue, PoolString::Intern("false"));

        const auto made = AbstractFactory<UIComponent>::GetInstance().Create("UIToggleComponent");
        ASSERT_TRUE(made);
        const auto* typed = made->Cast<UIToggleComponent>();
        ASSERT_NE(typed, nullptr);
        EXPECT_EQ(typed->onValue, PoolString::Intern("true"));
        EXPECT_EQ(typed->offValue, PoolString::Intern("false"));

        const auto& custom = Toggle("Custom");
        EXPECT_EQ(custom.onValue, PoolString::Intern("1"));
        EXPECT_EQ(custom.offValue, PoolString::Intern("0"));
    }

    TEST_F(UIToggleComponentTest, TheRowIsInteractiveAndTheTouchTargetIsTheWholeRow) {
        EXPECT_TRUE(Get("Sound").IsInteractive());
        EXPECT_TRUE(Toggle("Sound").MakesInteractive());
        EXPECT_FALSE(Get("SoundCaption").IsInteractive());
        EXPECT_FALSE(Get("SoundSwitch").IsInteractive());
        const Point caption = Middle(Get("SoundCaption").rect);
        const Point toggle = Middle(SwitchRect("Sound"));
        EXPECT_EQ(_canvas->GetLayout().HitTest(caption.x, caption.y), &Get("Sound"));
        EXPECT_EQ(_canvas->GetLayout().HitTest(toggle.x, toggle.y), &Get("Sound"));
    }

    TEST_F(UIToggleComponentTest, AnUnsetVariableAnEmptyBindAndAnEmptyOnValueAreOff) {
        EXPECT_FALSE(Toggle("Sound").IsOn());
        EXPECT_TRUE(ThumbDrawnAtLeft("Sound"));

        SetVar("soundOn", "true");
        EXPECT_FALSE(Toggle("Unbound").IsOn()) << "no bind: never on, whatever the variables say";

        auto& sound = Toggle("Sound");
        ASSERT_TRUE(sound.IsOn());
        sound.onValue = PoolString{};
        EXPECT_FALSE(sound.IsOn()) << "an empty onValue is never matched, not even by an empty variable";
        SetVar("soundOn", "");
        EXPECT_FALSE(sound.IsOn());
    }

    TEST_F(UIToggleComponentTest, TheVariableMustEqualTheOnValue) {
        SetVar("soundOn", "true");
        SetVar("mode", "true");
        EXPECT_TRUE(Toggle("Sound").IsOn());
        EXPECT_FALSE(Toggle("Custom").IsOn()) << "this one is on for \"1\"";

        SetVar("soundOn", "True");
        EXPECT_FALSE(Toggle("Sound").IsOn()) << "the match is exact, case included";
        SetVar("soundOn", "false");
        EXPECT_FALSE(Toggle("Sound").IsOn());
        SetVar("mode", "1");
        EXPECT_TRUE(Toggle("Custom").IsOn());
    }

    TEST_F(UIToggleComponentTest, TheStateCheckedOfTheRowAndItsChildrenIsWhetherTheToggleIsOn) {
        EXPECT_FALSE(Get("Sound").GetVisualState().checked);
        SetVar("soundOn", "true");
        EXPECT_TRUE(Toggle("Sound").IsChecked(Get("Sound")));
        Draw();
        EXPECT_TRUE(Get("Sound").GetVisualState().checked);
        EXPECT_TRUE(Get("SoundSwitch").GetVisualState().checked);
        EXPECT_TRUE(Get("SoundCaption").GetVisualState().checked);
        EXPECT_FALSE(Get("Custom").GetVisualState().checked);
    }

    TEST_F(UIToggleComponentTest, ThePositionOfTheThumbFollowsTheVariableAtTheNextDraw) {
        EXPECT_TRUE(ThumbDrawnAtLeft("Sound"));

        SetVar("soundOn", "true");
        EXPECT_TRUE(ThumbDrawnAtLeft("Sound")) << "nothing is redrawn before the next draw";
        Draw();
        EXPECT_TRUE(ThumbDrawnAtRight("Sound"));

        SetVar("soundOn", "false");
        Draw();
        EXPECT_TRUE(ThumbDrawnAtLeft("Sound"));

        CoreManager::GetUIVars().Remove(PoolString::Intern("soundOn"));
        SetVar("soundOn", "true");
        Draw();
        EXPECT_TRUE(ThumbDrawnAtRight("Sound"));
        CoreManager::GetUIVars().Remove(PoolString::Intern("soundOn"));
        Draw();
        EXPECT_TRUE(ThumbDrawnAtLeft("Sound")) << "a variable that goes away is off";
    }

    TEST_F(UIToggleComponentTest, TheTrackShowsTheStateWithItsOwnColour) {
        const auto parts = PartsOf("Custom");
        const DrawCall* off = FindRect(parts.track);
        ASSERT_NE(off, nullptr);
        EXPECT_TRUE(ColorsEqual(off->color, kOffTrackAuthored));

        SetVar("mode", "1");
        Draw();
        const DrawCall* on = FindRect(parts.track);
        ASSERT_NE(on, nullptr);
        EXPECT_TRUE(ColorsEqual(on->color, kOnTrackAuthored));
        const DrawCall* thumb = FindRect(parts.thumbOn);
        ASSERT_NE(thumb, nullptr);
        EXPECT_TRUE(ColorsEqual(thumb->color, kThumbAuthored));
    }

    // ---- the click only asks -----------------------------------------------------------------------------------------------

    TEST_F(UIToggleComponentTest, AClickAsksForTheOppositeAndChangesNothing) {
        ActionLog actions;
        Click("Sound");
        ASSERT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.LastAction(), PoolString::Intern("ToggleCompSound"));
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("true")) << "it was off: the click asks for on";

        Draw();
        EXPECT_FALSE(CoreManager::GetUIVars().Has(PoolString::Intern("soundOn"))) << "the toggle does not set the variable";
        EXPECT_FALSE(Toggle("Sound").IsOn());
        EXPECT_TRUE(ThumbDrawnAtLeft("Sound"));

        SetVar("soundOn", "true");  // the game's answer
        Draw();
        EXPECT_TRUE(ThumbDrawnAtRight("Sound"));

        Click("Sound");
        ASSERT_EQ(actions.Total(), 2u);
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("false")) << "it was on: the click asks for off";
        Draw();
        EXPECT_TRUE(Toggle("Sound").IsOn()) << "still on until the game says otherwise";

        Click("Sound");
        EXPECT_EQ(actions.Total(), 3u) << "a refused request is asked again, the toggle does not filter";
    }

    TEST_F(UIToggleComponentTest, TheClickerOfTheRowEmitsNothingOfItsOwn) {
        ActionLog actions;
        EXPECT_TRUE(Get("Sound").GetComponent<UIInputClicker>()->action.Empty());
        Click("Sound");
        EXPECT_EQ(actions.Total(), 1u) << "one click, one action";
    }

    TEST_F(UIToggleComponentTest, TheCaptionAndTheSwitchAndTheGapAreOneTouchTarget) {
        ActionLog actions;
        const auto& row = Get("Sound").rect;
        const Rect& caption = Get("SoundCaption").rect;
        const Rect& sw = SwitchRect("Sound");
        const float y = row.y + row.h * 0.5f;
        for (const float x : {row.x + 2.0f, caption.x + caption.w * 0.5f, caption.x + caption.w + kCaptionGap * 0.5f, sw.x + sw.w * 0.5f, row.x + row.w - 1.0f}) {
            ClickAt({x, y});
        }
        EXPECT_EQ(actions.Total(), 5u);
    }

    TEST_F(UIToggleComponentTest, CustomValuesAreAskedFor) {
        ActionLog actions;
        Click("Custom");
        EXPECT_EQ(actions.LastAction(), PoolString::Intern("ToggleCompCustom"));
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("1"));

        SetVar("mode", "1");
        Draw();
        Click("Custom");
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("0"));
        EXPECT_EQ(actions.Total(), 2u);
    }

    TEST_F(UIToggleComponentTest, AnUnboundToggleAlwaysAsksForOn) {
        ActionLog actions;
        Click("Unbound");
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("true"));
    }

    TEST_F(UIToggleComponentTest, AnEmptyActionEmitsNothing) {
        ActionLog actions;
        Click("NoAction");
        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UIToggleComponentTest, APressThatMovedOffTheRowDoesNotClick) {
        ActionLog actions;
        const Point at = Middle(Get("Sound").rect);
        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true)));
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(700.0f, 550.0f, SDL_BUTTON_LMASK));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(700.0f, 550.0f, false));
        EXPECT_EQ(actions.Total(), 0u);

        const Point other = Middle(Get("Custom").rect);
        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true)));
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(other.x, other.y, SDL_BUTTON_LMASK));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(other.x, other.y, false));
        EXPECT_EQ(actions.Total(), 0u) << "landing on another toggle is a click on neither";
    }

    // ---- disabled ----------------------------------------------------------------------------------------------------------

    TEST_F(UIToggleComponentTest, ADisabledRowSwallowsTheClickAndKeepsTheThumbWhereItWas) {
        ActionLog actions;
        EXPECT_TRUE(Get("Off").IsDisabled());
        EXPECT_TRUE(ThumbDrawnAtLeft("Off"));
        const Point at = Middle(Get("Off").rect);
        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true))) << "it still takes the press, so what is below does not get it";
        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at.x, at.y, false)));
        EXPECT_EQ(actions.Total(), 0u);

        SetVar("soundOn", "true");
        Draw();
        EXPECT_TRUE(ThumbDrawnAtRight("Off")) << "a disabled toggle still shows the state";
        Click("Off");
        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UIToggleComponentTest, ADisabledSwitchIsDrawnAtHalfTheAlpha) {
        SetVar("soundOn", "true");
        Draw();
        const auto enabled = PartsOf("Sound");
        const auto disabled = PartsOf("Off");
        const DrawCall* enabledTrack = FindRect(enabled.track);
        const DrawCall* disabledTrack = FindRect(disabled.track);
        const DrawCall* disabledThumb = FindRect(disabled.thumbOn);
        ASSERT_TRUE(enabledTrack && disabledTrack && disabledThumb);
        EXPECT_NEAR(disabledTrack->color.a, enabledTrack->color.a / 2.0f, 1.0f);
        EXPECT_NEAR(disabledThumb->color.a, 127.0f, 1.0f);
        EXPECT_TRUE(Get("OffSwitch").GetVisualState().disabled) << "the switch follows the row";
    }

    // ---- the geometry ------------------------------------------------------------------------------------------------------

    TEST_F(UIToggleComponentTest, TheSwitchIsAtTheRightEndAndTheCaptionTakesTheRoomItLeaves) {
        for (const char* name : {"Sound", "Custom", "Off", "Round"}) {
            const Rect& row = Get(name).rect;
            const Rect& caption = Get((eastl::string(name) + "Caption").c_str()).rect;
            const Rect& sw = SwitchRect(name);
            EXPECT_NEAR(sw.h, row.h, kEpsilon) << name;
            EXPECT_NEAR(sw.w, row.h * UISwitchComponent::kAreaAspect, kEpsilon) << name;
            EXPECT_NEAR(sw.x + sw.w, row.x + row.w, kEpsilon) << "the switch is at the right end: " << name;
            EXPECT_NEAR(caption.x + caption.w + kCaptionGap, sw.x, kEpsilon) << name;
            EXPECT_NEAR(caption.w, row.w - row.h * UISwitchComponent::kAreaAspect - kCaptionGap, kEpsilon) << name;
        }
    }

    TEST_F(UIToggleComponentTest, TheTrackAndThumbOfA300By56RowAreAtTheirNumbers) {
        // The numbers the first toggle (a class, now gone) drew, kept as the specification: a 28 high, 56 wide track with 14 of air to the right
        const auto parts = PartsOf("Sound");
        const Rect& row = Get("Sound").rect;
        EXPECT_TRUE(Near(parts.track, Rect{row.x + 230.0f, row.y + 14.0f, 56.0f, 28.0f}));
        EXPECT_TRUE(Near(parts.thumbOff, Rect{row.x + 233.0f, row.y + 17.0f, 22.0f, 22.0f}));
        EXPECT_TRUE(Near(parts.thumbOn, Rect{row.x + 261.0f, row.y + 17.0f, 22.0f, 22.0f}));
        EXPECT_NEAR(Get("SoundCaption").rect.w, 222.0f, kEpsilon);
    }

    TEST_F(UIToggleComponentTest, TheConstantsOfTheSwitch) {
        EXPECT_FLOAT_EQ(UISwitchComponent::kSwitchHeightRatio, 0.5f);
        EXPECT_FLOAT_EQ(UISwitchComponent::kSwitchAspect, 2.0f);
        EXPECT_FLOAT_EQ(UISwitchComponent::kThumbInset, 3.0f);
        EXPECT_FLOAT_EQ(UISwitchComponent::kAreaAspect, UISwitchComponent::kSwitchHeightRatio * UISwitchComponent::kSwitchAspect + (1.0f - UISwitchComponent::kSwitchHeightRatio) * 0.5f)
            << "the track and the air at the right end";
    }

    TEST_F(UIToggleComponentTest, TheTrackIsInTheRectAtThreeHeights) {
        for (const float height : {20.0f, 56.0f, 120.0f}) {
            const Rect area{7.0f, 9.0f, height * UISwitchComponent::kAreaAspect, height};
            const auto parts = UISwitchComponent::GetParts(area);
            EXPECT_NEAR(parts.track.h, height * UISwitchComponent::kSwitchHeightRatio, 1e-3f) << height;
            EXPECT_NEAR(parts.track.w, parts.track.h * UISwitchComponent::kSwitchAspect, 1e-3f) << height;
            EXPECT_NEAR(parts.track.x, area.x, 1e-3f) << "the track starts at the left edge of a switch element: " << height;
            EXPECT_NEAR(parts.track.y + parts.track.h * 0.5f, area.y + height * 0.5f, 1e-3f) << height;
            EXPECT_NEAR(area.x + area.w - (parts.track.x + parts.track.w), parts.track.y - area.y, 1e-3f) << "the air at the right end is that above the track: " << height;
            EXPECT_FLOAT_EQ(parts.thumbOff.w, parts.thumbOff.h) << "the thumb is a square";
            EXPECT_FLOAT_EQ(parts.thumbOff.x - parts.track.x, parts.track.x + parts.track.w - (parts.thumbOn.x + parts.thumbOn.w)) << "the same air at both ends: " << height;
            EXPECT_LT(parts.thumbOff.x, parts.thumbOn.x) << height;
        }
    }

    TEST_F(UIToggleComponentTest, ThePartsStayInsideTheRectAtAnySize) {
        for (const float height : {0.0f, 4.0f, 10.0f, 32.0f, 56.0f, 120.0f}) {
            for (const float width : {0.0f, 5.0f, 30.0f, 60.0f, 100.0f, 900.0f}) {
                const Rect area{7.0f, 9.0f, width, height};
                const auto parts = UISwitchComponent::GetParts(area);
                const std::string where = std::to_string(width) + " x " + std::to_string(height);
                EXPECT_TRUE(Inside(parts.track, area)) << where;
                EXPECT_TRUE(Inside(parts.thumbOn, parts.track) || parts.thumbOn.IsEmpty()) << where;
                EXPECT_TRUE(Inside(parts.thumbOff, parts.track) || parts.thumbOff.IsEmpty()) << where;
            }
        }
    }

    TEST_F(UIToggleComponentTest, AWidthSmallerThanTheTrackShrinksTheTrackToTheRect) {
        const Rect narrow{5.0f, 5.0f, 20.0f, 56.0f};
        const auto parts = UISwitchComponent::GetParts(narrow);
        EXPECT_NEAR(parts.track.w, 20.0f, 1e-3f) << "the track is no wider than the rect";
        EXPECT_NEAR(parts.track.x, 5.0f, 1e-3f);
        EXPECT_TRUE(Inside(parts.track, narrow));
    }

    TEST_F(UIToggleComponentTest, TheDrawnSwitchAndThumbAreInsideTheLaidOutRect) {
        SetVar("soundOn", "true");
        Draw();
        for (const char* name : {"Sound", "Custom", "Off"}) {
            const DrawCall* track = FindRect(PartsOf(name).track);
            ASSERT_NE(track, nullptr) << name;
            EXPECT_TRUE(Inside(track->rect, Get(name).rect)) << name;
        }
    }

    // ---- the caption never runs under the switch ---------------------------------------------------------------------------

    TEST_F(UIToggleComponentTest, ACaptionTooLongForTheRoomTheSwitchLeavesIsCaught) {
        const UIElement& caption = Get("LongCaption");
        const UIContentFit longFit = caption.CheckContent(caption.rect.w, caption.rect.h, 1.0f);
        EXPECT_TRUE(longFit.hasContent);
        EXPECT_FALSE(longFit.fits) << "the caption is too long for the room the switch leaves";
        EXPECT_TRUE(longFit.canShrink) << "an ellipsis text can shrink: the check needs `fits`, not the validator alone";

        const UIElement& shortCaption = Get("SoundCaption");
        const UIContentFit shortFit = shortCaption.CheckContent(shortCaption.rect.w, shortCaption.rect.h, 1.0f);
        EXPECT_TRUE(shortFit.hasContent);
        EXPECT_TRUE(shortFit.fits);
    }

    TEST_F(UIToggleComponentTest, AClippedCaptionThatDoesNotFitCannotShrink) {
        UIElement& caption = Get("LongCaption");
        caption.GetComponent<UITextComponent>()->overflow = TextOverflow::Clip;
        const UIContentFit fit = caption.CheckContent(caption.rect.w, caption.rect.h, 1.0f);
        EXPECT_FALSE(fit.fits);
        EXPECT_FALSE(fit.canShrink) << "a clipped text cannot shrink, so the validator reports TextDoesNotFit";
    }

    TEST_F(UIToggleComponentTest, TheDrawnCaptionDoesNotReachTheSwitch) {
        const Rect& row = Get("Long").rect;
        const Rect& sw = SwitchRect("Long");
        bool anyGlyph = false;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Rect) {
                continue;
            }
            if (call.rect.y >= row.y && call.rect.y + call.rect.h <= row.y + row.h && call.rect.x >= row.x - 1.0f && call.rect.x <= row.x + row.w) {
                anyGlyph = true;
                EXPECT_LE(call.rect.x + call.rect.w, sw.x + 1.0f);
            }
        }
        EXPECT_TRUE(anyGlyph) << "the text was drawn";
    }

    // ---- the colours of the states -----------------------------------------------------------------------------------------

    TEST_F(UIToggleComponentTest, TheColoursOfTheStatesAreDerivedFromAWhiteCaption) {
        const auto& sw = Switch("Sound");
        UIStateLook::State state;
        const Color off = sw.GetCurrentTrack(state);
        EXPECT_EQ(off.r, 255);
        EXPECT_EQ(off.a, static_cast<unsigned char>(255.0f * UISwitchComponent::kOffTrackAlpha));
        state.checked = true;
        EXPECT_TRUE(ColorsEqual(sw.GetCurrentTrack(state), kFillFallback));
        EXPECT_TRUE(ColorsEqual(sw.GetCurrentThumb(state), Color{255, 255, 255, 255}));
    }

    TEST_F(UIToggleComponentTest, HoverLightensPressDarkensAndDisabledHalvesTheTrack) {
        const auto& sw = Switch("Custom");
        UIStateLook::State state;
        const Color normal = sw.GetCurrentTrack(state);
        EXPECT_TRUE(ColorsEqual(normal, kOffTrackAuthored));

        state.hovered = true;
        const Color hover = sw.GetCurrentTrack(state);
        EXPECT_TRUE(ColorsEqual(hover, ShiftColor(normal, kHoverBrighten)));
        state.pressed = true;
        EXPECT_TRUE(ColorsEqual(sw.GetCurrentTrack(state), ShiftColor(normal, -kPressDarken)));
        state.disabled = true;
        EXPECT_TRUE(ColorsEqual(sw.GetCurrentTrack(state), WithAlpha(normal, kDisabledAlpha))) << "disabled wins over the pointer";
        EXPECT_TRUE(ColorsEqual(sw.GetCurrentThumb(state), WithAlpha(kThumbAuthored, kDisabledAlpha)));
    }

    TEST_F(UIToggleComponentTest, ThePointerOverTheRowShiftsTheTrackOfItsSwitch) {
        const auto parts = PartsOf("Custom");
        const Point at = Middle(Get("Custom").rect);
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(at.x, at.y));
        Draw();
        const DrawCall* hovered = FindRect(parts.track);
        ASSERT_NE(hovered, nullptr);
        EXPECT_TRUE(ColorsEqual(hovered->color, ShiftColor(kOffTrackAuthored, kHoverBrighten))) << "the pointer is over the caption, the switch follows the row";

        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true));
        Draw();
        const DrawCall* held = FindRect(parts.track);
        ASSERT_NE(held, nullptr);
        EXPECT_TRUE(ColorsEqual(held->color, ShiftColor(kOffTrackAuthored, -kPressDarken)));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at.x, at.y, false));
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(700.0f, 550.0f));
        Draw();
        EXPECT_TRUE(ColorsEqual(FindRect(parts.track)->color, kOffTrackAuthored));
    }

    // ---- the shape ---------------------------------------------------------------------------------------------------------

    TEST_F(UIToggleComponentTest, ThePartsAreRectanglesByDefault) {
        EXPECT_EQ(Switch("Sound").partShape, UIPartShape::Square);
        const auto parts = PartsOf("Sound");
        EXPECT_NE(FindRect(parts.track), nullptr);
        EXPECT_NE(FindRect(parts.thumbOff), nullptr);
        EXPECT_TRUE(CallsIn(*_renderer, DrawKind::Geometry, SwitchRect("Sound")).empty());
    }

    TEST_F(UIToggleComponentTest, ARoundSwitchDrawsAPillTrackAndACircleThumb) {
        const auto parts = PartsOf("Round");
        EXPECT_EQ(FindRect(parts.track), nullptr);
        EXPECT_EQ(FindRect(parts.thumbOff), nullptr);

        const auto drawn = CallsIn(*_renderer, DrawKind::Geometry, SwitchRect("Round"));
        ASSERT_EQ(drawn.size(), 2u) << "the track and the thumb";
        ExpectRoundPart(drawn[0], parts.track);
        ExpectRoundPart(drawn[1], parts.thumbOff);

        SetVar("soundOn", "true");
        Draw();
        const auto drawnOn = CallsIn(*_renderer, DrawKind::Geometry, SwitchRect("Round"));
        ASSERT_EQ(drawnOn.size(), 2u);
        ExpectRoundPart(drawnOn[1], parts.thumbOn);
    }

    // ---- themes ------------------------------------------------------------------------------------------------------------

    TEST_F(UIToggleComponentTest, ATokenInEachColourFieldOfTheSwitchResolves) {
        UIThemes themes;
        themes.Set("light"_intern, "off", Color{1, 2, 3, 255});
        themes.Set("light"_intern, "on", Color{4, 5, 6, 255});
        themes.Set("light"_intern, "knob", Color{7, 8, 9, 255});
        themes.Set("dark"_intern, "off", Color{11, 12, 13, 255});

        auto sw = New<UISwitchComponent>();
        sw->trackColor = UIColor::FromToken("off"_intern);
        sw->fillColor = UIColor::FromToken("on"_intern);
        sw->thumbColor = UIColor::FromToken("knob"_intern);
        sw->ApplyTheme(themes);
        EXPECT_TRUE(ColorsEqual(sw->trackColor, Color{1, 2, 3, 255}));
        EXPECT_TRUE(ColorsEqual(sw->fillColor, Color{4, 5, 6, 255}));
        EXPECT_TRUE(ColorsEqual(sw->thumbColor, Color{7, 8, 9, 255}));

        themes.SetTheme("dark"_intern);
        sw->ApplyTheme(themes);
        EXPECT_TRUE(ColorsEqual(sw->trackColor, Color{11, 12, 13, 255}));
        EXPECT_EQ(sw->trackColor.token, "off"_intern) << "the token stays";
    }

    TEST_F(UIToggleComponentTest, ASwitchInTheCanvasTakesTheColoursOfTheCurrentTheme) {
        auto& themes = CoreManager::GetUIThemes();
        themes.Clear();
        themes.Set("light"_intern, "thumb", Color{9, 8, 7, 255});
        Switch("Sound").thumbColor = UIColor::FromToken("thumb"_intern);
        Get("SoundSwitch").ResetTheme();
        Draw();
        const DrawCall* thumb = FindRect(PartsOf("Sound").thumbOff);
        ASSERT_NE(thumb, nullptr);
        EXPECT_TRUE(ColorsEqual(thumb->color, Color{9, 8, 7, 255}));
        themes.Clear();
    }

    // ---- reflection --------------------------------------------------------------------------------------------------------

    TEST_F(UIToggleComponentTest, TheFieldsSurviveAnXmlRoundTrip) {
        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("component"));
        _canvas->Serialize(serializer);
        serializer.EndObject();
        const eastl::string xml = serializer.SaveToString();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
        auto loaded = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_FALSE(deserializer.HasErrors());
        loaded->Initialize();

        const auto* custom = loaded->GetRoot().FindByName(PoolString::Intern("Custom"))->GetComponent<UIToggleComponent>();
        ASSERT_NE(custom, nullptr);
        EXPECT_EQ(custom->bind, PoolString::Intern("mode"));
        EXPECT_EQ(custom->action, PoolString::Intern("ToggleCompCustom"));
        EXPECT_EQ(custom->onValue, PoolString::Intern("1"));
        EXPECT_EQ(custom->offValue, PoolString::Intern("0"));
        const auto* sw = loaded->GetRoot().FindByName(PoolString::Intern("CustomSwitch"))->GetComponent<UISwitchComponent>();
        ASSERT_NE(sw, nullptr);
        EXPECT_TRUE(ColorsEqual(sw->trackColor, kOffTrackAuthored));
        EXPECT_TRUE(ColorsEqual(sw->fillColor, kOnTrackAuthored));
        EXPECT_TRUE(ColorsEqual(sw->thumbColor, kThumbAuthored));
        EXPECT_EQ(loaded->GetRoot().FindByName(PoolString::Intern("RoundSwitch"))->GetComponent<UISwitchComponent>()->partShape, UIPartShape::Round);
        EXPECT_TRUE(loaded->GetRoot().FindByName(PoolString::Intern("Off"))->GetComponent<UIInputClicker>()->disabled);
        const auto* sound = loaded->GetRoot().FindByName(PoolString::Intern("Sound"))->GetComponent<UIToggleComponent>();
        ASSERT_NE(sound, nullptr);
        EXPECT_EQ(sound->onValue, PoolString::Intern("true")) << "the defaults come back as the defaults";
        EXPECT_EQ(sound->offValue, PoolString::Intern("false"));
        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
        }
    }

    TEST_F(UIToggleComponentTest, TheFieldsSurviveABinaryRoundTrip) {
        Toggle("Sound").offValue = PoolString::Intern("off");
        BinarySerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("component"));
        _canvas->Serialize(serializer);
        serializer.EndObject();

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData().data(), serializer.GetData().size()));
        auto loaded = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_FALSE(deserializer.HasErrors());
        loaded->Initialize();

        const auto* custom = loaded->GetRoot().FindByName(PoolString::Intern("Custom"))->GetComponent<UIToggleComponent>();
        ASSERT_NE(custom, nullptr);
        EXPECT_EQ(custom->onValue, PoolString::Intern("1"));
        EXPECT_EQ(loaded->GetRoot().FindByName(PoolString::Intern("CustomSwitch"))->GetComponent<UISwitchComponent>()->thumbColor.b, kThumbAuthored.b);
        EXPECT_EQ(loaded->GetRoot().FindByName(PoolString::Intern("Sound"))->GetComponent<UIToggleComponent>()->offValue, PoolString::Intern("off"));
        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
        }
    }

    TEST_F(UIToggleComponentTest, ThePartShapeIsSkippedAsADefaultWhenAsked) {
        auto xmlOf = [](const UIElement& element) {
            XmlSerializer serializer;
            serializer.SetSkipDefaults(true);
            EXPECT_TRUE(serializer.BeginObject("element"));
            element.Serialize(serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        };
        EXPECT_EQ(xmlOf(Get("SoundSwitch")).find("partShape"), eastl::string::npos);
        EXPECT_NE(xmlOf(Get("RoundSwitch")).find("partShape=\"Round\""), eastl::string::npos);
    }

}  // namespace BECore::Tests
