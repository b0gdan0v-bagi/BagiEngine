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
#include <BECore/UI/UISliderComponent.h>
#include <BECore/UI/UIThemes.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UIPartShapeChecks.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // A Screen canvas in window pixels (output 800 x 600, scale 1). Every slider is one element with a clicker (`stateLook="false"`) and a `UISliderComponent`, 56 high unless said:
        //   left column (x 100, 300 wide):
        //     Volume (y 10)   bind volume, 0..1 step 0.1; a plate with fills of states
        //     Fine   (y 100)  bind fine, 0..100 step 5
        //     Free   (y 200)  bind free, 0..1 continuous, explicit colours
        //     Unit   (y 300)  bind unit, 0..10 step 1
        //     Quarter(y 400)  bind quarter, 0..1 step 0.25
        //     Odd    (y 500)  bind odd, 0..1 step 0.3 (not a whole number of steps)
        //     Round  (y 560)  30 high, partShape Round, explicit colours
        //   right column (x 450, 300 wide):
        //     Off      (y 10)   the clicker is disabled
        //     NoAction (y 100)  no action
        //     Other    (y 200)  a second slider, bind other, 0..1 step 0.1
        //     Short    (y 300)  30 high: below a touch target of 32
        //     Reversed (y 400)  maxValue below minValue
        //     Narrow   (y 500)  40 wide, 56 high: narrower than its thumb would be
        constexpr const char* kSliderXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Volume" x="100" y="10" width="300" height="56" color="30,30,30,255" hoverColor="90,90,90,255" pressedColor="10,10,10,255">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="volume" action="SliderCompVolume" minValue="0" maxValue="1" step="0.1"/>
            </components>
          </element>
          <element type="UIElement" name="Fine" x="100" y="100" width="300" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="fine" action="SliderCompFine" minValue="0" maxValue="100" step="5"/>
            </components>
          </element>
          <element type="UIElement" name="Free" x="100" y="200" width="300" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="free" action="SliderCompFree" trackColor="10,20,30,255" fillColor="40,50,60,255" thumbColor="70,80,90,255"/>
            </components>
          </element>
          <element type="UIElement" name="Unit" x="100" y="300" width="300" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="unit" action="SliderCompUnit" minValue="0" maxValue="10" step="1"/>
            </components>
          </element>
          <element type="UIElement" name="Quarter" x="100" y="400" width="300" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="quarter" action="SliderCompQuarter" step="0.25"/>
            </components>
          </element>
          <element type="UIElement" name="Odd" x="100" y="500" width="300" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="odd" action="SliderCompOdd" step="0.3"/>
            </components>
          </element>
          <element type="UIElement" name="Off" x="450" y="10" width="300" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false" disabled="true"/>
              <component type="UISliderComponent" bind="volume" action="SliderCompOff" step="0.1"/>
            </components>
          </element>
          <element type="UIElement" name="NoAction" x="450" y="100" width="300" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="volume" step="0.1"/>
            </components>
          </element>
          <element type="UIElement" name="Other" x="450" y="200" width="300" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="other" action="SliderCompOther" step="0.1"/>
            </components>
          </element>
          <element type="UIElement" name="Short" x="450" y="300" width="300" height="30">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="volume" action="SliderCompShort"/>
            </components>
          </element>
          <element type="UIElement" name="Reversed" x="450" y="400" width="300" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="reversed" action="SliderCompReversed" minValue="1" maxValue="0"/>
            </components>
          </element>
          <element type="UIElement" name="Narrow" x="450" y="500" width="40" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="narrow" action="SliderCompNarrow"/>
            </components>
          </element>
          <element type="UIElement" name="Round" x="100" y="560" width="300" height="30">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="free" action="SliderCompRound" partShape="Round" trackColor="10,20,30,255" fillColor="40,50,60,255" thumbColor="70,80,90,255"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr float kEpsilon = 1e-2f;

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

        bool Near(const Rect& a, const Rect& b, float tolerance = kEpsilon) {
            return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.w - b.w) <= tolerance && std::abs(a.h - b.h) <= tolerance;
        }

        bool Inside(const Rect& inner, const Rect& outer, float tolerance = 1e-3f) {
            return inner.x >= outer.x - tolerance && inner.y >= outer.y - tolerance && inner.x + inner.w <= outer.x + outer.w + tolerance && inner.y + inner.h <= outer.y + outer.h + tolerance;
        }

        /// The actions the sliders emit, with the value each carried, in order
        class ActionLog : public SubscriptionHolder {
        public:
            ActionLog() {
                Subscribe<InputEvents::ActionEvent, &ActionLog::OnAction>(this);
            }

            size_t Total() const {
                return _actions.size();
            }
            size_t Count(const char* action) const {
                return static_cast<size_t>(std::count(_actions.begin(), _actions.end(), PoolString::Intern(action)));
            }
            String LastValue() const {
                return _values.empty() ? String{} : String(_values.back().ToStringView().data(), _values.back().ToStringView().size());
            }
            /// The values of one action, joined with spaces
            String Values(const char* action) const {
                String joined;
                for (size_t i = 0; i < _actions.size(); ++i) {
                    if (_actions[i] == PoolString::Intern(action)) {
                        joined += joined.empty() ? "" : " ";
                        joined.append(_values[i].ToStringView().data(), _values[i].ToStringView().size());
                    }
                }
                return joined;
            }

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                _actions.push_back(event.action);
                _values.push_back(event.value);
            }

            Vector<PoolString> _actions;
            Vector<PoolString> _values;
        };

        String ElementToXml(const UIElement& element) {
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

    class UISliderComponentTest : public ::testing::Test {
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
            ASSERT_TRUE(deserializer.LoadFromString(kSliderXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();

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
            _manager->SetActiveScene(PoolString::Intern("Main"));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        UIElement& Element(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return *element;
        }

        UISliderComponent& Slider(const char* name) const {
            UISliderComponent* slider = Element(name).GetComponent<UISliderComponent>();
            EXPECT_NE(slider, nullptr) << name << " is not a slider";
            return *slider;
        }

        const Rect& RectOf(const char* name) const {
            return Element(name).rect;
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

        /// The x of the pointer that asks for @p fraction of the travel of a slider
        float XAt(const char* name, float fraction) const {
            const auto parts = UISliderComponent::GetParts(RectOf(name), 0.0f);
            return parts.travelStart + (parts.travelEnd - parts.travelStart) * fraction;
        }

        float MiddleY(const char* name) const {
            const Rect& rect = RectOf(name);
            return rect.y + rect.h * 0.5f;
        }

        bool Down(const char* name, float x) const {
            return CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, MiddleY(name), true));
        }
        bool Up(float x, float y) const {
            return CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
        }
        void Drag(float x, float y) const {
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(x, y, SDL_BUTTON_LMASK));
        }

        /// A whole press and release at a fraction of the travel
        void Tap(const char* name, float fraction) const {
            const float x = XAt(name, fraction);
            ASSERT_TRUE(Down(name, x));
            ASSERT_TRUE(Up(x, MiddleY(name)));
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
    };

    // ---- the thumb follows the variable --------------------------------------------------------------------------------------

    TEST_F(UISliderComponentTest, ASliderBuiltFromAFileHasItsFields) {
        const auto& volume = Slider("Volume");
        EXPECT_EQ(volume.bind, PoolString::Intern("volume"));
        EXPECT_EQ(volume.action, PoolString::Intern("SliderCompVolume"));
        EXPECT_FLOAT_EQ(volume.minValue, 0.0f);
        EXPECT_FLOAT_EQ(volume.maxValue, 1.0f);
        EXPECT_FLOAT_EQ(volume.step, 0.1f);
        EXPECT_TRUE(Element("Off").GetComponent<UIInputClicker>()->disabled);

        const auto made = AbstractFactory<UIComponent>::GetInstance().Create("UISliderComponent");
        ASSERT_TRUE(made);
        const auto* typed = made->Cast<UISliderComponent>();
        ASSERT_NE(typed, nullptr);
        EXPECT_FLOAT_EQ(typed->minValue, 0.0f);
        EXPECT_FLOAT_EQ(typed->maxValue, 1.0f);
        EXPECT_FLOAT_EQ(typed->step, 0.0f);
        EXPECT_EQ(typed->partShape, UIPartShape::Square);
        EXPECT_TRUE(typed->MakesInteractive());
        EXPECT_FALSE(typed->GivesStateLook());
    }

    TEST_F(UISliderComponentTest, TheElementIsInteractiveAndItsClickerEmitsNothing) {
        EXPECT_TRUE(Element("Volume").IsInteractive());
        EXPECT_FALSE(Element("Volume").interactive) << "the component makes it one, not the field";
        EXPECT_TRUE(Element("Volume").IsHitTestable());
        EXPECT_FALSE(Element("Volume").GivesStateLook()) << "stateLook=\"false\": the plate is still";
        EXPECT_TRUE(Element("Volume").GetComponent<UIInputClicker>()->action.Empty());
    }

    TEST_F(UISliderComponentTest, AClickerlessSliderStillWorksButCannotBeDisabled) {
        auto& volume = Element("Volume");
        volume.components.erase(volume.components.begin());  // the clicker was the first
        ASSERT_EQ(volume.GetComponent<UIInputClicker>(), nullptr);
        EXPECT_TRUE(volume.IsInteractive());
        ActionLog actions;
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.5f));
        EXPECT_EQ(actions.LastValue(), "0.5");
        EXPECT_FALSE(volume.IsDisabled());
    }

    TEST_F(UISliderComponentTest, TheThumbStandsAtTheFractionOfTheTravel) {
        const auto& volume = Slider("Volume");
        const Rect& rect = RectOf("Volume");
        for (const float fraction : {0.0f, 0.25f, 1.0f}) {
            SetVar("volume", fraction == 0.0f ? "0" : fraction == 0.25f ? "0.25" : "1");
            Draw();
            EXPECT_NEAR(volume.GetFraction(), fraction, 1e-6f);
            const auto parts = UISliderComponent::GetParts(rect, fraction);
            const DrawCall* thumb = FindRect(parts.thumb);
            ASSERT_NE(thumb, nullptr) << fraction;
            // The centre of the thumb is at the fraction of the travel: 56 units of thumb, so the travel is 28 units in from each end
            EXPECT_NEAR(thumb->rect.x + thumb->rect.w * 0.5f, rect.x + 28.0f + (rect.w - 56.0f) * fraction, 1e-2f) << fraction;
        }
    }

    TEST_F(UISliderComponentTest, AnUnsetOrMalformedVariableIsTheMinimumAndOutOfRangeIsClamped) {
        const auto& fine = Slider("Fine");
        EXPECT_FLOAT_EQ(fine.GetValue(), 0.0f);
        SetVar("fine", "abc");
        EXPECT_FLOAT_EQ(fine.GetValue(), 0.0f);
        SetVar("fine", "nan");
        EXPECT_FLOAT_EQ(fine.GetFraction(), 0.0f);
        SetVar("fine", "250");
        EXPECT_FLOAT_EQ(fine.GetValue(), 100.0f);
        EXPECT_FLOAT_EQ(fine.GetFraction(), 1.0f);
        SetVar("fine", "-4");
        EXPECT_FLOAT_EQ(fine.GetValue(), 0.0f);
        SetVar("fine", "50");
        EXPECT_FLOAT_EQ(fine.GetFraction(), 0.5f);

        auto& unbound = Slider("Volume");
        unbound.bind = PoolString{};
        EXPECT_FLOAT_EQ(unbound.GetValue(), 0.0f);
    }

    TEST_F(UISliderComponentTest, TheThumbIsASquareOfTheHeightAndNeverLeavesTheRect) {
        for (const char* name : {"Volume", "Fine", "Short", "Narrow", "Reversed", "Round"}) {
            const Rect& rect = RectOf(name);
            for (const float fraction : {0.0f, 0.5f, 1.0f}) {
                const auto parts = UISliderComponent::GetParts(rect, fraction);
                EXPECT_NEAR(parts.thumb.w, parts.thumb.h, 1e-4f) << name;
                EXPECT_NEAR(parts.thumb.h, std::min(rect.h, rect.w), 1e-4f) << name;
                EXPECT_TRUE(Inside(parts.thumb, rect)) << name << " at " << fraction;
                EXPECT_TRUE(Inside(parts.track, rect)) << name;
                EXPECT_LE(parts.track.h, UISliderComponent::kTrackHeight + 1e-4f) << name;
            }
        }
        const Rect& volume = RectOf("Volume");
        EXPECT_NEAR(UISliderComponent::GetParts(volume, 0.0f).thumb.x, volume.x, 1e-4f);
        EXPECT_NEAR(UISliderComponent::GetParts(volume, 1.0f).thumb.x + 56.0f, volume.x + volume.w, 1e-4f);
        EXPECT_NEAR(UISliderComponent::GetParts(volume, 0.5f).track.y + UISliderComponent::kTrackHeight * 0.5f, volume.y + 28.0f, 1e-4f) << "the track is in the middle of the height";
    }

    TEST_F(UISliderComponentTest, TheFilledPartRunsToTheCentreOfTheThumb) {
        SetVar("free", "0.5");
        Draw();
        const auto parts = UISliderComponent::GetParts(RectOf("Free"), 0.5f);
        const DrawCall* track = FindRect(parts.track);
        const DrawCall* filled = FindRect(parts.filled);
        const DrawCall* thumb = FindRect(parts.thumb);
        ASSERT_TRUE(track && filled && thumb);
        EXPECT_EQ(track->color.r, 10);
        EXPECT_EQ(filled->color.r, 40);
        EXPECT_EQ(thumb->color.r, 70);
        EXPECT_NEAR(filled->rect.x + filled->rect.w, thumb->rect.x + thumb->rect.w * 0.5f, 1e-2f);

        // Derived colours: a slider with none of its own still has three looks
        const auto& volume = Slider("Volume");
        const auto& element = Element("Volume");
        EXPECT_GT(volume.GetCurrentTrack(element).a, 0);
        EXPECT_GT(volume.GetCurrentFill(element).a, 0);
        EXPECT_GT(volume.GetCurrentThumb(element).a, 0);
    }

    TEST_F(UISliderComponentTest, ThePlateIsOneRectOfTheElementColourAndNeverChangesWithThePointer) {
        const Rect& rect = RectOf("Volume");
        const DrawCall* rest = FindRect(rect);
        ASSERT_NE(rest, nullptr);
        EXPECT_EQ(rest->color.r, 30);

        CoreManager::GetUIInput().DispatchMouseMotion(Motion(XAt("Volume", 0.5f), MiddleY("Volume")));
        Draw();
        ASSERT_TRUE(Element("Volume").hovered);
        EXPECT_EQ(FindRect(rect)->color.r, 30) << "hovered: the plate stays";
        ASSERT_TRUE(Down("Volume", XAt("Volume", 0.5f)));
        Draw();
        EXPECT_EQ(FindRect(rect)->color.r, 30) << "pressed: the plate stays";
        ASSERT_TRUE(Up(XAt("Volume", 0.5f), MiddleY("Volume")));
    }

    // ---- a press asks for the value under the pointer ----------------------------------------------------------------------------

    TEST_F(UISliderComponentTest, APressAtTheEdgesAndTheMiddleAsksForTheMinimumTheMiddleAndTheMaximum) {
        ActionLog actions;
        const Rect& volume = RectOf("Volume");

        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.0f));
        EXPECT_EQ(actions.LastValue(), "0");
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.5f));
        EXPECT_EQ(actions.LastValue(), "0.5");
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 1.0f));
        EXPECT_EQ(actions.LastValue(), "1");
        EXPECT_EQ(actions.Count("SliderCompVolume"), 3u);

        // The edges of the rect and everything within half a thumb of them are the ends
        for (const float x : {volume.x, volume.x + 1.0f, volume.x + 27.0f}) {
            ASSERT_TRUE(Down("Volume", x));
            ASSERT_TRUE(Up(x, MiddleY("Volume")));
            EXPECT_EQ(actions.LastValue(), "0") << x;
        }
        for (const float x : {volume.x + volume.w - 27.0f, volume.x + volume.w - 1.0f, volume.x + volume.w - 0.01f}) {
            ASSERT_TRUE(Down("Volume", x));
            ASSERT_TRUE(Up(x, MiddleY("Volume")));
            EXPECT_EQ(actions.LastValue(), "1") << x;
        }
    }

    TEST_F(UISliderComponentTest, ThePressChangesNothingByItselfAndTheGamesAnswerMovesTheThumb) {
        ActionLog actions;
        const auto& volume = Slider("Volume");
        const Rect& rect = RectOf("Volume");
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.3f));
        ASSERT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.LastValue(), "0.3");

        Draw();
        EXPECT_FALSE(CoreManager::GetUIVars().Has(PoolString::Intern("volume"))) << "the slider does not set the variable";
        EXPECT_FLOAT_EQ(volume.GetFraction(), 0.0f);
        EXPECT_NE(FindRect(UISliderComponent::GetParts(rect, 0.0f).thumb), nullptr) << "no answer: the thumb stays";
        EXPECT_EQ(FindRect(UISliderComponent::GetParts(rect, 0.3f).thumb), nullptr);

        SetVar("volume", "0.3");  // the game's answer
        Draw();
        EXPECT_NE(FindRect(UISliderComponent::GetParts(rect, 0.3f).thumb), nullptr);
        EXPECT_EQ(FindRect(UISliderComponent::GetParts(rect, 0.0f).thumb), nullptr);
    }

    TEST_F(UISliderComponentTest, ValuesAreSnappedToTheStepAndWrittenShortAndExact) {
        ActionLog actions;
        // step 0.1: 0.3 and never 0.30000001
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.3f));
        EXPECT_EQ(actions.LastValue(), "0.3");
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.7f));
        EXPECT_EQ(actions.LastValue(), "0.7");
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.34f));
        EXPECT_EQ(actions.LastValue(), "0.3");
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.36f));
        EXPECT_EQ(actions.LastValue(), "0.4");
        // step 1 over 0..10: "7", not "7.0"
        ASSERT_NO_FATAL_FAILURE(Tap("Unit", 0.7f));
        EXPECT_EQ(actions.LastValue(), "7");
        // 0..100 step 5
        ASSERT_NO_FATAL_FAILURE(Tap("Fine", 0.35f));
        EXPECT_EQ(actions.LastValue(), "35");
        ASSERT_NO_FATAL_FAILURE(Tap("Fine", 0.372f));
        EXPECT_EQ(actions.LastValue(), "35");
        ASSERT_NO_FATAL_FAILURE(Tap("Fine", 0.39f));
        EXPECT_EQ(actions.LastValue(), "40");
        // step 0.25
        ASSERT_NO_FATAL_FAILURE(Tap("Quarter", 0.3f));
        EXPECT_EQ(actions.LastValue(), "0.25");
        ASSERT_NO_FATAL_FAILURE(Tap("Quarter", 0.4f));
        EXPECT_EQ(actions.LastValue(), "0.5");
        // a continuous slider writes what is under the pointer, short
        ASSERT_NO_FATAL_FAILURE(Tap("Free", 0.5f));
        EXPECT_EQ(actions.LastValue(), "0.5");
    }

    TEST_F(UISliderComponentTest, ARangeThatIsNotAWholeNumberOfStepsStillReachesItsMaximum) {
        ActionLog actions;
        ASSERT_NO_FATAL_FAILURE(Tap("Odd", 1.0f));
        EXPECT_EQ(actions.LastValue(), "1");
        ASSERT_NO_FATAL_FAILURE(Tap("Odd", 0.9f));
        EXPECT_EQ(actions.LastValue(), "0.9");
        ASSERT_NO_FATAL_FAILURE(Tap("Odd", 0.3f));
        EXPECT_EQ(actions.LastValue(), "0.3");
        ASSERT_NO_FATAL_FAILURE(Tap("Odd", 0.0f));
        EXPECT_EQ(actions.LastValue(), "0");
    }

    TEST_F(UISliderComponentTest, AValueReadBackFromTheVariableLandsOnTheSameThumb) {
        for (const char* name : {"Volume", "Fine", "Unit", "Quarter", "Free", "Odd"}) {
            const auto& slider = Slider(name);
            const auto& element = Element(name);
            for (int i = 0; i <= 100; ++i) {
                const float value = slider.ValueAt(element, XAt(name, static_cast<float>(i) / 100.0f));
                const String text = UISliderComponent::FormatValue(value);
                EXPECT_LE(text.size(), 9u) << name << " " << text.c_str();
                CoreManager::GetUIVars().Set(slider.bind, StringView(text));
                const float expected = UIText::FractionInRange(value, slider.minValue, slider.maxValue);
                EXPECT_NEAR(slider.GetFraction(), expected, 1e-6f) << name << " " << text.c_str();
            }
        }
    }

    TEST_F(UISliderComponentTest, TheTextOfAValueIsShortAndExact) {
        EXPECT_EQ(UISliderComponent::FormatValue(0.3f), "0.3");
        EXPECT_EQ(UISliderComponent::FormatValue(0.1f * 3.0f), "0.3");
        EXPECT_EQ(UISliderComponent::FormatValue(0.30000001f), "0.3");
        EXPECT_EQ(UISliderComponent::FormatValue(7.0f), "7");
        EXPECT_EQ(UISliderComponent::FormatValue(35.0f), "35");
        EXPECT_EQ(UISliderComponent::FormatValue(100.0f), "100");
        EXPECT_EQ(UISliderComponent::FormatValue(0.0f), "0");
        EXPECT_EQ(UISliderComponent::FormatValue(-0.0f), "0");
        EXPECT_EQ(UISliderComponent::FormatValue(-1.5f), "-1.5");
        EXPECT_EQ(UISliderComponent::FormatValue(0.000123f), "0.000123");
        EXPECT_EQ(UISliderComponent::FormatValue(12345.67f), "12345.7");
        EXPECT_EQ(UISliderComponent::FormatValue(0.01000001f), "0.01");
        EXPECT_EQ(UISliderComponent::FormatValue(1234567.0f), "1234567");
        EXPECT_EQ(UISliderComponent::FormatValue(std::numeric_limits<float>::quiet_NaN()), "0");
        EXPECT_EQ(UISliderComponent::FormatValue(std::numeric_limits<float>::infinity()), "0");
        EXPECT_FALSE(UISliderComponent::FormatValue(1e30f).empty());
    }

    TEST_F(UISliderComponentTest, ValueAtXIsPure) {
        const Rect area{100.0f, 10.0f, 300.0f, 56.0f};
        EXPECT_FLOAT_EQ(UISliderComponent::ValueAtX(area, 0.0f, 1.0f, 0.1f, 128.0f), 0.0f);
        EXPECT_FLOAT_EQ(UISliderComponent::ValueAtX(area, 0.0f, 1.0f, 0.1f, 372.0f), 1.0f);
        EXPECT_FLOAT_EQ(UISliderComponent::ValueAtX(area, 0.0f, 1.0f, 0.1f, 250.0f), 0.5f);
        EXPECT_FLOAT_EQ(UISliderComponent::ValueAtX(area, 0.0f, 100.0f, 5.0f, 250.0f), 50.0f);
        EXPECT_FLOAT_EQ(UISliderComponent::ValueAtX(area, 1.0f, 0.0f, 0.0f, 250.0f), 1.0f) << "a reversed range asks for the minimum as written";
        EXPECT_FLOAT_EQ(UISliderComponent::ValueAtX(area, 2.0f, 2.0f, 0.0f, 250.0f), 2.0f) << "an empty range too";
        EXPECT_FLOAT_EQ(UISliderComponent::ValueAtX({0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 0.0f, 5.0f), 0.0f) << "a rect with no travel";
    }

    // ---- a drag keeps asking ------------------------------------------------------------------------------------------------------

    TEST_F(UISliderComponentTest, ADragEmitsOncePerChangeOfTheValue) {
        ActionLog actions;
        ASSERT_TRUE(Down("Volume", XAt("Volume", 0.0f)));
        const float y = MiddleY("Volume");
        Drag(XAt("Volume", 0.02f), y);  // inside the first step
        Drag(XAt("Volume", 0.03f), y);
        Drag(XAt("Volume", 0.10f), y);
        Drag(XAt("Volume", 0.11f), y);  // still 0.1
        Drag(XAt("Volume", 0.20f), y);
        Drag(XAt("Volume", 0.50f), y);
        Drag(XAt("Volume", 0.50f), y);  // the same place twice
        Drag(XAt("Volume", 0.40f), y);  // and back
        ASSERT_TRUE(Up(XAt("Volume", 0.40f), y));
        EXPECT_EQ(actions.Values("SliderCompVolume"), "0 0.1 0.2 0.5 0.4");
        EXPECT_EQ(actions.Total(), 5u) << "the release adds nothing";
        EXPECT_EQ(Slider("Volume").GetLastEmittedValue(), PoolString::Intern("0.4"));
    }

    TEST_F(UISliderComponentTest, ADragThatLeavesTheSliderKeepsFollowingAndClamps) {
        ActionLog actions;
        const Rect& volume = RectOf("Volume");
        ASSERT_TRUE(Down("Volume", XAt("Volume", 0.5f)));
        Drag(XAt("Volume", 0.8f), volume.y - 40.0f);              // above
        Drag(XAt("Volume", 0.6f), volume.y + volume.h + 200.0f);  // far below, over other sliders
        Drag(volume.x + volume.w + 200.0f, volume.y + 10.0f);     // past the end
        Drag(volume.x - 90.0f, volume.y + 10.0f);                 // before the start
        Drag(volume.x - 90.0f, volume.y + 12.0f);                 // and a little more: the same value
        ASSERT_TRUE(Up(volume.x - 90.0f, volume.y + 12.0f));
        EXPECT_EQ(actions.Values("SliderCompVolume"), "0.5 0.8 0.6 1 0");
        EXPECT_EQ(actions.Total(), 5u);
    }

    TEST_F(UISliderComponentTest, AReleaseFiresNoSecondActionAndTwoSlidersDoNotDisturbEachOther) {
        ActionLog actions;
        ASSERT_TRUE(Down("Volume", XAt("Volume", 0.5f)));
        // Dragged over the other slider (which is to the right, so the pressed one clamps at its end): only the pressed one follows
        Drag(XAt("Other", 0.9f), MiddleY("Other"));
        ASSERT_TRUE(Up(XAt("Other", 0.9f), MiddleY("Other")));
        EXPECT_EQ(actions.Count("SliderCompOther"), 0u);
        EXPECT_EQ(actions.Values("SliderCompVolume"), "0.5 1");
        EXPECT_FALSE(Element("Other").pressed);

        ASSERT_NO_FATAL_FAILURE(Tap("Other", 0.2f));
        EXPECT_EQ(actions.Values("SliderCompOther"), "0.2");
        EXPECT_EQ(actions.Count("SliderCompVolume"), 2u);

        // A press on the same value as the last gesture ended on still emits
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.9f));
        EXPECT_EQ(actions.Values("SliderCompVolume"), "0.5 1 0.9");
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.9f));
        EXPECT_EQ(actions.Values("SliderCompVolume"), "0.5 1 0.9 0.9");

        // Motion with no press held does nothing
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(XAt("Volume", 0.1f), MiddleY("Volume")));
        EXPECT_EQ(actions.Count("SliderCompVolume"), 4u);
    }

    TEST_F(UISliderComponentTest, ADisabledSliderTakesThePressAndDoesNothing) {
        ActionLog actions;
        const auto& off = Slider("Off");
        ASSERT_TRUE(Element("Off").IsDisabled());
        ASSERT_TRUE(Down("Off", XAt("Off", 0.5f))) << "the press does not fall through to what is below";
        Drag(XAt("Off", 0.9f), MiddleY("Off"));
        ASSERT_TRUE(Up(XAt("Off", 0.9f), MiddleY("Off")));
        EXPECT_EQ(actions.Total(), 0u);
        EXPECT_TRUE(off.GetLastEmittedValue().Empty());

        SetVar("volume", "0.5");
        Draw();
        EXPECT_NE(FindRect(UISliderComponent::GetParts(RectOf("Off"), 0.5f).thumb), nullptr) << "it still shows the value";
        const auto& on = Slider("Volume");
        const auto& offElement = Element("Off");
        const auto& onElement = Element("Volume");
        EXPECT_LT(off.GetCurrentThumb(offElement).a, on.GetCurrentThumb(onElement).a) << "and is drawn grey";
        EXPECT_LT(off.GetCurrentTrack(offElement).a, on.GetCurrentTrack(onElement).a);
        EXPECT_LT(off.GetCurrentFill(offElement).a, on.GetCurrentFill(onElement).a);
    }

    TEST_F(UISliderComponentTest, ASliderOfADisabledGroupEmitsNothingEither) {
        ActionLog actions;
        Element("Volume").groupDisabled = true;
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.5f));
        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UISliderComponentTest, AnEmptyActionEmitsNothing) {
        ActionLog actions;
        ASSERT_TRUE(Down("NoAction", XAt("NoAction", 0.5f)));
        Drag(XAt("NoAction", 0.9f), MiddleY("NoAction"));
        ASSERT_TRUE(Up(XAt("NoAction", 0.9f), MiddleY("NoAction")));
        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UISliderComponentTest, AnEmptyOrReversedRangeAsksForTheMinimumOnly) {
        ActionLog actions;
        ASSERT_NO_FATAL_FAILURE(Tap("Reversed", 0.7f));
        EXPECT_EQ(actions.LastValue(), "1") << "the minimum value of the range as written";
        EXPECT_FLOAT_EQ(Slider("Reversed").GetFraction(), 0.0f);
        SetVar("reversed", "0.5");
        EXPECT_FLOAT_EQ(Slider("Reversed").GetFraction(), 0.0f);
        const auto parts = UISliderComponent::GetParts(RectOf("Reversed"), Slider("Reversed").GetFraction());
        EXPECT_TRUE(Inside(parts.thumb, RectOf("Reversed")));
    }

    TEST_F(UISliderComponentTest, ASliderNarrowerThanItsThumbStillWorks) {
        ActionLog actions;
        const Rect& narrow = RectOf("Narrow");
        EXPECT_TRUE(Inside(UISliderComponent::GetParts(narrow, 1.0f).thumb, narrow));
        ASSERT_NO_FATAL_FAILURE(Tap("Narrow", 1.0f));
        EXPECT_EQ(actions.LastValue(), "0");
        const float x = narrow.x + narrow.w - 1.0f;
        ASSERT_TRUE(Down("Narrow", x));
        ASSERT_TRUE(Up(x, MiddleY("Narrow")));
        EXPECT_EQ(actions.Count("SliderCompNarrow"), 2u);
    }

    TEST_F(UISliderComponentTest, ThePressedThumbIsDarkerAndTheHoveredOneLighter) {
        const auto& free = Slider("Free");
        const auto& element = Element("Free");
        const Color rest = free.GetCurrentThumb(element);
        ASSERT_TRUE(Down("Free", XAt("Free", 0.5f)));
        EXPECT_LT(free.GetCurrentThumb(element).r, rest.r);
        Drag(700.0f, 550.0f);  // the press is held wherever the pointer went
        EXPECT_LT(free.GetCurrentThumb(element).r, rest.r);
        ASSERT_TRUE(Up(700.0f, 550.0f));
        EXPECT_EQ(free.GetCurrentThumb(element).r, rest.r);

        CoreManager::GetUIInput().DispatchMouseMotion(Motion(XAt("Free", 0.5f), MiddleY("Free")));
        EXPECT_GT(free.GetCurrentThumb(element).r, rest.r);
    }

    // ---- the validator -----------------------------------------------------------------------------------------------------------

    TEST_F(UISliderComponentTest, ASliderBelowTheTouchSizeIsCaught) {
        UILayoutValidatorOptions options;
        Vector<PoolString> small;
        for (const UILayoutIssue& issue : _canvas->GetLayout().Validate(options)) {
            if (issue.kind == UILayoutIssueKind::TouchTargetTooSmall) {
                small.push_back(issue.element);
            }
        }
        // The 30-unit ones (Short and Round); the 56-unit ones hold the touch size by their height
        ASSERT_EQ(small.size(), 2u);
        EXPECT_NE(std::find(small.begin(), small.end(), PoolString::Intern("Short")), small.end());
        EXPECT_NE(std::find(small.begin(), small.end(), PoolString::Intern("Round")), small.end());
    }

    // ---- files --------------------------------------------------------------------------------------------------------------------

    TEST_F(UISliderComponentTest, TheFieldsSurviveAnXmlRoundTrip) {
        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("component"));
        _canvas->Serialize(serializer);
        serializer.EndObject();
        const String xml = serializer.SaveToString();

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(StringView(xml)));
        auto loaded = New<UICanvasComponent>();
        ASSERT_TRUE(deserializer.BeginObject("component"));
        loaded->Deserialize(deserializer);
        deserializer.EndObject();
        ASSERT_FALSE(deserializer.HasErrors());
        loaded->Initialize();

        const auto* fine = loaded->GetRoot().FindByName(PoolString::Intern("Fine"))->GetComponent<UISliderComponent>();
        ASSERT_NE(fine, nullptr);
        EXPECT_EQ(fine->bind, PoolString::Intern("fine"));
        EXPECT_EQ(fine->action, PoolString::Intern("SliderCompFine"));
        EXPECT_FLOAT_EQ(fine->maxValue, 100.0f);
        EXPECT_FLOAT_EQ(fine->step, 5.0f);
        const auto* free = loaded->GetRoot().FindByName(PoolString::Intern("Free"))->GetComponent<UISliderComponent>();
        ASSERT_NE(free, nullptr);
        EXPECT_EQ(free->trackColor.g, 20);
        EXPECT_EQ(free->fillColor.g, 50);
        EXPECT_EQ(free->thumbColor.g, 80);
        EXPECT_FALSE(loaded->GetRoot().FindByName(PoolString::Intern("Free"))->GetComponent<UIInputClicker>()->stateLook);
        EXPECT_TRUE(loaded->GetRoot().FindByName(PoolString::Intern("Off"))->GetComponent<UIInputClicker>()->disabled);
        EXPECT_FLOAT_EQ(loaded->GetRoot().FindByName(PoolString::Intern("Reversed"))->GetComponent<UISliderComponent>()->minValue, 1.0f);
        EXPECT_EQ(loaded->GetRoot().FindByName(PoolString::Intern("Round"))->GetComponent<UISliderComponent>()->partShape, UIPartShape::Round);
        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
        }
    }

    TEST_F(UISliderComponentTest, TheFieldsSurviveABinaryRoundTrip) {
        Slider("Volume").step = 0.05f;
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

        const auto* free = loaded->GetRoot().FindByName(PoolString::Intern("Free"))->GetComponent<UISliderComponent>();
        ASSERT_NE(free, nullptr);
        EXPECT_EQ(free->thumbColor.b, 90);
        EXPECT_FLOAT_EQ(loaded->GetRoot().FindByName(PoolString::Intern("Volume"))->GetComponent<UISliderComponent>()->step, 0.05f);
        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
        }
    }

    TEST_F(UISliderComponentTest, ThePartShapeIsSkippedAsADefaultWhenAsked) {
        const auto written = [](const UIElement& element) {
            XmlSerializer serializer;
            serializer.SetSkipDefaults(true);
            EXPECT_TRUE(serializer.BeginObject("element"));
            element.Serialize(serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        };
        EXPECT_EQ(written(Element("Free")).find("partShape"), String::npos);
        EXPECT_NE(written(Element("Round")).find("partShape=\"Round\""), String::npos);
    }

    // ---- the shape of the parts ----------------------------------------------------------------------------------------------

    TEST_F(UISliderComponentTest, ThePartsAreRectanglesByDefault) {
        SetVar("free", "0.5");
        Draw();
        const auto& free = Slider("Free");
        EXPECT_EQ(free.partShape, UIPartShape::Square);
        const auto parts = UISliderComponent::GetParts(RectOf("Free"), free.GetFraction());
        EXPECT_NE(FindRect(parts.track), nullptr);
        EXPECT_NE(FindRect(parts.filled), nullptr);
        EXPECT_NE(FindRect(parts.thumb), nullptr);
        EXPECT_TRUE(CallsIn(*_renderer, DrawKind::Geometry, RectOf("Free")).empty());
    }

    TEST_F(UISliderComponentTest, ARoundSliderDrawsPillsAndACircleInsteadOfRectangles) {
        SetVar("free", "0.5");
        Draw();
        const Rect& rect = RectOf("Round");
        const auto parts = UISliderComponent::GetParts(rect, Slider("Round").GetFraction());
        EXPECT_EQ(FindRect(parts.track), nullptr);
        EXPECT_EQ(FindRect(parts.filled), nullptr);
        EXPECT_EQ(FindRect(parts.thumb), nullptr);
        EXPECT_TRUE(CallsIn(*_renderer, DrawKind::Rect, rect).empty()) << "nothing of the slider is a rect any more";

        const auto drawn = CallsIn(*_renderer, DrawKind::Geometry, rect);
        ASSERT_EQ(drawn.size(), 3u) << "the track, the filled part, the thumb, in this order";
        ExpectRoundPart(drawn[0], parts.track);
        ExpectRoundPart(drawn[1], parts.filled);
        ExpectRoundPart(drawn[2], parts.thumb);
        EXPECT_EQ(drawn[2].color.r, Slider("Round").thumbColor.r) << "the colours are the ones the square parts have";
    }

    TEST_F(UISliderComponentTest, ThePartShapeChangesNeitherTheTouchAreaNorTheValue) {
        auto& free = Slider("Free");
        const float before = free.ValueAt(Element("Free"), XAt("Free", 0.5f));
        free.partShape = UIPartShape::Round;
        Draw();
        EXPECT_FLOAT_EQ(free.ValueAt(Element("Free"), XAt("Free", 0.5f)), before);
        EXPECT_TRUE(Element("Free").IsInteractive());
    }

    // ---- themes ------------------------------------------------------------------------------------------------------------

    TEST_F(UISliderComponentTest, ATokenInEachColourFieldResolves) {
        UIThemes themes;
        themes.Set("light"_intern, "track", Color{1, 2, 3, 255});
        themes.Set("light"_intern, "fill", Color{4, 5, 6, 255});
        themes.Set("light"_intern, "thumb", Color{7, 8, 9, 255});
        themes.Set("dark"_intern, "track", Color{11, 12, 13, 255});

        auto slider = New<UISliderComponent>();
        slider->trackColor = UIColor::FromToken("track"_intern);
        slider->fillColor = UIColor::FromToken("fill"_intern);
        slider->thumbColor = UIColor::FromToken("thumb"_intern);
        slider->ApplyTheme(themes);
        EXPECT_TRUE(ColorsEqual(slider->trackColor, Color{1, 2, 3, 255}));
        EXPECT_TRUE(ColorsEqual(slider->fillColor, Color{4, 5, 6, 255}));
        EXPECT_TRUE(ColorsEqual(slider->thumbColor, Color{7, 8, 9, 255}));

        themes.SetTheme("dark"_intern);
        slider->ApplyTheme(themes);
        EXPECT_TRUE(ColorsEqual(slider->trackColor, Color{11, 12, 13, 255}));
        EXPECT_EQ(slider->trackColor.token, "track"_intern) << "the token stays";
    }

    TEST_F(UISliderComponentTest, ASliderInTheCanvasTakesTheColoursOfTheCurrentTheme) {
        auto& themes = CoreManager::GetUIThemes();
        themes.Clear();
        themes.Set("light"_intern, "thumb", Color{9, 8, 7, 255});
        Slider("Volume").thumbColor = UIColor::FromToken("thumb"_intern);
        Element("Volume").ResetTheme();
        Draw();
        const DrawCall* thumb = FindRect(UISliderComponent::GetParts(RectOf("Volume"), 0.0f).thumb);
        ASSERT_NE(thumb, nullptr);
        EXPECT_TRUE(ColorsEqual(thumb->color, Color{9, 8, 7, 255}));
        themes.Clear();
    }

}  // namespace BECore::Tests
