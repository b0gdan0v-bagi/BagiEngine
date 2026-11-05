#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIToggle.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // A Screen canvas in window pixels (output 800 x 600, scale 1), every toggle 56 high:
        //   Sound   (10, 10, 300 wide)   bind soundOn, the defaults "true" / "false"
        //   Custom  (10, 100, 300 wide)  bind mode, onValue "1", offValue "0", explicit colors
        //   NoAction(10, 200, 300 wide)  bind soundOn, no action
        //   Off     (10, 300, 300 wide)  disabled, bind soundOn
        //   Long    (10, 400, 220 wide)  a caption too long for the area left of the switch
        //   Unbound (10, 500, 300 wide)  no bind
        constexpr const char* kToggleXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIToggle" name="Sound" bind="soundOn" action="ToggleTestSound" text="Sound" x="10" y="10" width="300" height="56" color="30,30,30,255"/>
          <element type="UIToggle" name="Custom" bind="mode" action="ToggleTestCustom" onValue="1" offValue="0" text="Custom" x="10" y="100" width="300" height="56"
                   trackColor="10,20,30,255" fillColor="40,50,60,255" thumbColor="70,80,90,255"/>
          <element type="UIToggle" name="NoAction" bind="soundOn" text="No action" x="10" y="200" width="300" height="56"/>
          <element type="UIToggle" name="Off" bind="soundOn" action="ToggleTestOff" text="Disabled" x="10" y="300" width="300" height="56" disabled="true"/>
          <element type="UIToggle" name="Long" bind="soundOn" action="ToggleTestLong" text="A caption far too long for the room that the switch leaves" x="10" y="400" width="220" height="56"/>
          <element type="UIToggle" name="Unbound" action="ToggleTestUnbound" text="Unbound" x="10" y="500" width="300" height="56"/>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

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

        bool Near(const Rect& a, const Rect& b, float tolerance = 1e-2f) {
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

        eastl::string ElementToXml(const UIElement& element) {
            XmlSerializer serializer;
            EXPECT_TRUE(serializer.BeginObject("element"));
            element.Serialize(serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        }

        void SetVar(const char* name, const char* value) {
            CoreManager::GetUIVars().Set(PoolString::Intern(name), value);
        }

    }  // namespace

    class UIToggleTest : public ::testing::Test {
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

            _canvas = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            _canvas.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void Draw() {
            _manager->SetActiveScene(PoolString::Intern("Main"));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        UIToggle& Toggle(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            UIToggle* typed = element ? element->Cast<UIToggle>() : nullptr;
            EXPECT_NE(typed, nullptr) << name << " is not a toggle";
            return *typed;
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
            const auto parts = UIToggle::GetParts(Toggle(name).rect);
            return FindRect(parts.thumbOn) != nullptr && FindRect(parts.thumbOff) == nullptr;
        }

        bool ThumbDrawnAtLeft(const char* name) const {
            const auto parts = UIToggle::GetParts(Toggle(name).rect);
            return FindRect(parts.thumbOff) != nullptr && FindRect(parts.thumbOn) == nullptr;
        }

        static Point Middle(const UIElement& element) {
            return {element.rect.x + element.rect.w * 0.5f, element.rect.y + element.rect.h * 0.5f};
        }

        void Click(const char* name) {
            const Point at = Middle(Toggle(name));
            ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true)));
            ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at.x, at.y, false)));
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
    };

    // ---- on and off follow the variable -----------------------------------------------------------------------------------

    TEST_F(UIToggleTest, AToggleBuiltFromAFileHasItsDefaults) {
        const auto& sound = Toggle("Sound");
        static_assert(std::is_base_of_v<UILabel, UIToggle>);
        EXPECT_EQ(sound.bind, PoolString::Intern("soundOn"));
        EXPECT_EQ(sound.onValue, PoolString::Intern("true"));
        EXPECT_EQ(sound.offValue, PoolString::Intern("false"));
        EXPECT_TRUE(sound.interactive);
        EXPECT_FALSE(sound.disabled);
        EXPECT_EQ(sound.align, TextAlign::Left);
        EXPECT_EQ(sound.verticalAlign, UIVerticalAlign::Center);

        const auto made = AbstractFactory<UIElement>::GetInstance().Create("UIToggle");
        ASSERT_TRUE(made);
        const auto* typed = made->Cast<UIToggle>();
        ASSERT_NE(typed, nullptr);
        EXPECT_EQ(typed->onValue, PoolString::Intern("true"));
        EXPECT_EQ(typed->offValue, PoolString::Intern("false"));

        const auto& custom = Toggle("Custom");
        EXPECT_EQ(custom.onValue, PoolString::Intern("1"));
        EXPECT_EQ(custom.offValue, PoolString::Intern("0"));
    }

    TEST_F(UIToggleTest, AnUnsetVariableAnEmptyBindAndAnEmptyOnValueAreOff) {
        Draw();
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

    TEST_F(UIToggleTest, TheVariableMustEqualTheOnValue) {
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

    TEST_F(UIToggleTest, ThePositionOfTheThumbFollowsTheVariableAtTheNextDraw) {
        Draw();
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

    TEST_F(UIToggleTest, TheTrackShowsTheStateWithItsOwnColor) {
        Draw();
        const auto parts = UIToggle::GetParts(Toggle("Custom").rect);
        const DrawCall* off = FindRect(parts.track);
        ASSERT_NE(off, nullptr);
        EXPECT_EQ(off->color.r, 10);
        EXPECT_EQ(off->color.g, 20);

        SetVar("mode", "1");
        Draw();
        const DrawCall* on = FindRect(parts.track);
        ASSERT_NE(on, nullptr);
        EXPECT_EQ(on->color.r, 40);
        EXPECT_EQ(on->color.g, 50);
        const DrawCall* thumb = FindRect(parts.thumbOn);
        ASSERT_NE(thumb, nullptr);
        EXPECT_EQ(thumb->color.r, 70);

        // Derived colors: the off and the on track differ for a toggle with no colors of its own, and the thumb is the text color
        Draw();
        auto& sound = Toggle("Sound");
        const Color offTrack = sound.GetCurrentTrack();
        SetVar("soundOn", "true");
        const Color onTrack = sound.GetCurrentTrack();
        EXPECT_NE(offTrack.a, onTrack.a);
        EXPECT_GT(offTrack.a, 0);
        EXPECT_LT(offTrack.a, 255);
        EXPECT_EQ(sound.GetCurrentThumb().r, sound.textColor.r);
    }

    // ---- the click only asks -----------------------------------------------------------------------------------------------

    TEST_F(UIToggleTest, AClickAsksForTheOppositeAndChangesNothing) {
        ActionLog actions;
        Draw();
        ASSERT_NO_FATAL_FAILURE(Click("Sound"));
        ASSERT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.LastAction(), PoolString::Intern("ToggleTestSound"));
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("true")) << "it was off: the click asks for on";

        Draw();
        EXPECT_FALSE(CoreManager::GetUIVars().Has(PoolString::Intern("soundOn"))) << "the toggle does not set the variable";
        EXPECT_FALSE(Toggle("Sound").IsOn());
        EXPECT_TRUE(ThumbDrawnAtLeft("Sound"));

        SetVar("soundOn", "true");  // the game's answer
        Draw();
        EXPECT_TRUE(Toggle("Sound").IsOn());
        EXPECT_TRUE(ThumbDrawnAtRight("Sound"));

        ASSERT_NO_FATAL_FAILURE(Click("Sound"));
        ASSERT_EQ(actions.Total(), 2u);
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("false")) << "it was on: the click asks for off";
        Draw();
        EXPECT_TRUE(Toggle("Sound").IsOn()) << "still on until the game says otherwise";

        ASSERT_NO_FATAL_FAILURE(Click("Sound"));
        EXPECT_EQ(actions.Total(), 3u) << "a refused request is asked again, the toggle does not filter";
    }

    TEST_F(UIToggleTest, TheCaptionAndTheSwitchAreOneTouchTarget) {
        ActionLog actions;
        Draw();
        const auto& sound = Toggle("Sound");
        const auto parts = UIToggle::GetParts(sound.rect);
        for (const float x : {sound.rect.x + 2.0f, parts.text.x + parts.text.w * 0.5f, parts.track.x + parts.track.w * 0.5f, sound.rect.x + sound.rect.w - 1.0f}) {
            const float y = sound.rect.y + sound.rect.h * 0.5f;
            ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true))) << x;
            ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false))) << x;
        }
        EXPECT_EQ(actions.Total(), 4u);
    }

    TEST_F(UIToggleTest, CustomValuesAreAskedFor) {
        ActionLog actions;
        Draw();
        ASSERT_NO_FATAL_FAILURE(Click("Custom"));
        EXPECT_EQ(actions.LastAction(), PoolString::Intern("ToggleTestCustom"));
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("1"));

        SetVar("mode", "1");
        Draw();
        ASSERT_NO_FATAL_FAILURE(Click("Custom"));
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("0"));
        EXPECT_EQ(actions.Total(), 2u);
    }

    TEST_F(UIToggleTest, AnUnboundToggleAlwaysAsksForOn) {
        ActionLog actions;
        Draw();
        ASSERT_NO_FATAL_FAILURE(Click("Unbound"));
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("true"));
    }

    TEST_F(UIToggleTest, AnEmptyActionEmitsNothing) {
        ActionLog actions;
        Draw();
        ASSERT_NO_FATAL_FAILURE(Click("NoAction"));
        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UIToggleTest, APressThatMovedOffTheToggleDoesNotClick) {
        ActionLog actions;
        Draw();
        const Point at = Middle(Toggle("Sound"));
        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true)));
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(700.0f, 550.0f, SDL_BUTTON_LMASK));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(700.0f, 550.0f, false));
        EXPECT_EQ(actions.Total(), 0u);

        // And one that lands on another toggle is not a click on either
        const Point other = Middle(Toggle("Custom"));
        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true)));
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(other.x, other.y, SDL_BUTTON_LMASK));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(other.x, other.y, false));
        EXPECT_EQ(actions.Total(), 0u);
    }

    // ---- disabled ----------------------------------------------------------------------------------------------------------

    TEST_F(UIToggleTest, ADisabledToggleSwallowsTheClickAndKeepsTheThumbWhereItWas) {
        ActionLog actions;
        Draw();
        EXPECT_TRUE(ThumbDrawnAtLeft("Off"));
        ASSERT_NO_FATAL_FAILURE(Click("Off")) << "it still takes the click, so what is below does not get it";
        EXPECT_EQ(actions.Total(), 0u);

        SetVar("soundOn", "true");
        Draw();
        EXPECT_TRUE(ThumbDrawnAtRight("Off")) << "a disabled toggle still shows the state";
        ASSERT_NO_FATAL_FAILURE(Click("Off"));
        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UIToggleTest, ADisabledToggleIsDrawnGrey) {
        SetVar("soundOn", "true");
        Draw();
        const auto& enabled = Toggle("Sound");
        const auto& disabled = Toggle("Off");
        EXPECT_NEAR(disabled.GetCurrentTrack().a, enabled.GetCurrentTrack().a / 2.0f, 1.0f) << "half the alpha of the same state";
        EXPECT_LT(disabled.GetCurrentThumb().a, enabled.GetCurrentThumb().a);

        const auto parts = UIToggle::GetParts(disabled.rect);
        const DrawCall* thumb = FindRect(parts.thumbOn);
        ASSERT_NE(thumb, nullptr);
        EXPECT_EQ(thumb->color.a, disabled.GetCurrentThumb().a);
        EXPECT_LT(thumb->color.a, 255);
    }

    // ---- the geometry ------------------------------------------------------------------------------------------------------

    TEST_F(UIToggleTest, TheThumbIsAtTheRightWhenOnAndAtTheLeftWhenOff) {
        const auto parts = UIToggle::GetParts({10.0f, 10.0f, 300.0f, 56.0f});
        EXPECT_LT(parts.thumbOff.x, parts.thumbOn.x);
        EXPECT_FLOAT_EQ(parts.thumbOff.y, parts.thumbOn.y);
        EXPECT_FLOAT_EQ(parts.thumbOff.w, parts.thumbOff.h) << "the thumb is a square";
        EXPECT_FLOAT_EQ(parts.thumbOff.x - parts.track.x, parts.track.x + parts.track.w - (parts.thumbOn.x + parts.thumbOn.w)) << "the same air at both ends";
        EXPECT_NEAR(parts.track.h, 56.0f * UIToggle::kSwitchHeightRatio, 1e-3f);
        EXPECT_NEAR(parts.track.w, parts.track.h * UIToggle::kSwitchAspect, 1e-3f);
        // The track is centered vertically and ends at the right with the air it has above and below
        EXPECT_NEAR(parts.track.y + parts.track.h * 0.5f, 10.0f + 28.0f, 1e-3f);
        EXPECT_NEAR(310.0f - (parts.track.x + parts.track.w), parts.track.y - 10.0f, 1e-3f);
    }

    TEST_F(UIToggleTest, TheSwitchAndTheThumbStayInsideTheRectAtAnySize) {
        for (const float height : {0.0f, 4.0f, 10.0f, 32.0f, 56.0f, 120.0f}) {
            for (const float width : {0.0f, 5.0f, 30.0f, 60.0f, 100.0f, 300.0f, 900.0f}) {
                const Rect area{7.0f, 9.0f, width, height};
                const auto parts = UIToggle::GetParts(area);
                const std::string where = std::to_string(width) + " x " + std::to_string(height);
                EXPECT_TRUE(Inside(parts.track, area)) << where;
                EXPECT_TRUE(Inside(parts.thumbOn, parts.track) || parts.thumbOn.IsEmpty()) << where;
                EXPECT_TRUE(Inside(parts.thumbOff, parts.track) || parts.thumbOff.IsEmpty()) << where;
                EXPECT_TRUE(Inside(parts.text, area)) << where;
                EXPECT_GE(parts.text.w, 0.0f) << where;
                EXPECT_LE(parts.text.x + parts.text.w, parts.track.x + 1e-3f) << "the caption area ends before the switch: " << where;
            }
        }
    }

    TEST_F(UIToggleTest, TheDrawnSwitchAndThumbAreInsideTheLaidOutRect) {
        SetVar("soundOn", "true");
        Draw();
        for (const char* name : {"Sound", "Custom", "Off", "Long"}) {
            const Rect rect = Toggle(name).rect;
            const auto parts = UIToggle::GetParts(rect);
            const DrawCall* track = FindRect(parts.track);
            ASSERT_NE(track, nullptr) << name;
            EXPECT_TRUE(Inside(track->rect, rect)) << name;
        }
    }

    // ---- the caption never runs under the switch ---------------------------------------------------------------------------

    TEST_F(UIToggleTest, TheCaptionIsMeasuredInTheAreaLeftOfTheSwitch) {
        Draw();
        const auto& sound = Toggle("Sound");
        const auto parts = UIToggle::GetParts(sound.rect);
        EXPECT_LT(parts.text.w, sound.rect.w);
        EXPECT_NEAR(parts.text.x + parts.text.w + UIToggle::kCaptionGap, parts.track.x, 1e-3f);

        const UIContentFit shortFit = sound.CheckContent(sound.rect.w, sound.rect.h, 1.0f);
        EXPECT_TRUE(shortFit.hasContent);
        EXPECT_TRUE(shortFit.fits);

        const auto& tooLong = Toggle("Long");
        const UIContentFit longFit = tooLong.CheckContent(tooLong.rect.w, tooLong.rect.h, 1.0f);
        EXPECT_TRUE(longFit.hasContent);
        EXPECT_FALSE(longFit.fits) << "the caption is too long for the room the switch leaves";
        EXPECT_TRUE(longFit.canShrink) << "an ellipsis label can shrink: the check needs `fits`, not the validator alone";
    }

    TEST_F(UIToggleTest, ACaptionThatFitsTheWholeRectButNotTheAreaIsCaught) {
        // The same caption in a plain label of the same rect fits; in the toggle the switch takes room it needs
        Draw();
        auto& sound = Toggle("Sound");
        sound.text = "Word";
        bool onlyTheAreaCutsIt = false;
        for (int words = 0; words < 40; ++words) {
            sound.text.append(" word");
            const UIContentFit whole = sound.UILabel::CheckContent(sound.rect.w, sound.rect.h, 1.0f);
            const UIContentFit area = sound.CheckContent(sound.rect.w, sound.rect.h, 1.0f);
            EXPECT_TRUE(!area.fits || whole.fits) << "what fits the area fits the whole rect: " << words;
            onlyTheAreaCutsIt = onlyTheAreaCutsIt || (whole.fits && !area.fits);
        }
        EXPECT_TRUE(onlyTheAreaCutsIt) << "some caption is long for the area left of the switch and short for the whole rect";
    }

    TEST_F(UIToggleTest, AClippedCaptionThatDoesNotFitIsADefect) {
        Draw();
        auto& tooLong = Toggle("Long");
        tooLong.overflow = TextOverflow::Clip;
        const UIContentFit fit = tooLong.CheckContent(tooLong.rect.w, tooLong.rect.h, 1.0f);
        EXPECT_FALSE(fit.fits);
        EXPECT_FALSE(fit.canShrink) << "a clipped text cannot shrink, so the validator reports TextDoesNotFit";
    }

    TEST_F(UIToggleTest, ARectWithNoRoomForTheCaptionIsNotFittingEither) {
        auto& sound = Toggle("Sound");
        const UIContentFit fit = sound.CheckContent(20.0f, 56.0f, 1.0f);
        EXPECT_TRUE(fit.hasContent);
        EXPECT_FALSE(fit.fits);
        EXPECT_FALSE(fit.canShrink);

        sound.text = "";
        EXPECT_FALSE(sound.CheckContent(20.0f, 56.0f, 1.0f).hasContent) << "no caption, nothing to fit";
    }

    TEST_F(UIToggleTest, TheDrawnCaptionDoesNotReachTheSwitch) {
        Draw();
        const auto& tooLong = Toggle("Long");
        const auto parts = UIToggle::GetParts(tooLong.rect);
        bool anyGlyph = false;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Rect) {
                continue;
            }
            anyGlyph = true;
            if (call.rect.y >= tooLong.rect.y && call.rect.y + call.rect.h <= tooLong.rect.y + tooLong.rect.h && call.rect.x >= tooLong.rect.x - 1.0f &&
                call.rect.x <= tooLong.rect.x + tooLong.rect.w) {
                EXPECT_LE(call.rect.x + call.rect.w, parts.track.x + 1.0f);
            }
        }
        EXPECT_TRUE(anyGlyph) << "the text was drawn";
    }

    TEST_F(UIToggleTest, TheCaptionIsLaidOutOnceForAnUnchangedScreen) {
        Draw();
        const uint32_t layouts = Toggle("Sound").GetLayoutCount();
        EXPECT_GT(layouts, 0u);
        SetVar("soundOn", "true");
        Draw();
        Draw();
        EXPECT_EQ(Toggle("Sound").GetLayoutCount(), layouts) << "flipping the switch lays no text out again";
    }

    // ---- the pointer -------------------------------------------------------------------------------------------------------

    TEST_F(UIToggleTest, HoverAndPressShiftTheTrack) {
        Draw();
        auto& sound = Toggle("Custom");  // explicit track colors: the derived white one is already at the top of the channel range
        const Color normal = sound.GetCurrentTrack();
        const Point at = Middle(sound);

        CoreManager::GetUIInput().DispatchMouseMotion(Motion(at.x, at.y));
        ASSERT_TRUE(sound.hovered);
        const Color hover = sound.GetCurrentTrack();
        EXPECT_GT(hover.r, normal.r) << "lighter under the pointer";
        EXPECT_EQ(hover.a, normal.a);

        ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true)));
        ASSERT_TRUE(sound.pressed);
        const Color pressed = sound.GetCurrentTrack();
        EXPECT_LT(pressed.r, hover.r);
        EXPECT_LT(pressed.r, normal.r) << "darker while held";

        Draw();
        const DrawCall* drawn = FindRect(UIToggle::GetParts(sound.rect).track);
        ASSERT_NE(drawn, nullptr);
        EXPECT_EQ(drawn->color.r, pressed.r);

        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at.x, at.y, false));
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(700.0f, 550.0f));
        EXPECT_EQ(sound.GetCurrentTrack().r, normal.r);
    }

    // ---- reflection --------------------------------------------------------------------------------------------------------

    TEST_F(UIToggleTest, TheFieldsSurviveAnXmlRoundTrip) {
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

        const auto* custom = loaded->GetRoot().FindByName(PoolString::Intern("Custom"))->Cast<UIToggle>();
        ASSERT_NE(custom, nullptr);
        EXPECT_EQ(custom->bind, PoolString::Intern("mode"));
        EXPECT_EQ(custom->action, PoolString::Intern("ToggleTestCustom"));
        EXPECT_EQ(custom->onValue, PoolString::Intern("1"));
        EXPECT_EQ(custom->offValue, PoolString::Intern("0"));
        EXPECT_EQ(custom->trackColor.g, 20);
        EXPECT_EQ(custom->fillColor.g, 50);
        EXPECT_EQ(custom->thumbColor.g, 80);
        EXPECT_TRUE(loaded->GetRoot().FindByName(PoolString::Intern("Off"))->Cast<UIToggle>()->disabled);
        const auto* sound = loaded->GetRoot().FindByName(PoolString::Intern("Sound"))->Cast<UIToggle>();
        ASSERT_NE(sound, nullptr);
        EXPECT_EQ(sound->onValue, PoolString::Intern("true")) << "the defaults come back as the defaults";
        EXPECT_EQ(sound->offValue, PoolString::Intern("false"));
        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
        }
    }

    TEST_F(UIToggleTest, TheFieldsSurviveABinaryRoundTrip) {
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

        const auto* custom = loaded->GetRoot().FindByName(PoolString::Intern("Custom"))->Cast<UIToggle>();
        ASSERT_NE(custom, nullptr);
        EXPECT_EQ(custom->onValue, PoolString::Intern("1"));
        EXPECT_EQ(custom->thumbColor.b, 90);
        EXPECT_EQ(loaded->GetRoot().FindByName(PoolString::Intern("Sound"))->Cast<UIToggle>()->offValue, PoolString::Intern("off"));
        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
        }
    }

}  // namespace BECore::Tests
