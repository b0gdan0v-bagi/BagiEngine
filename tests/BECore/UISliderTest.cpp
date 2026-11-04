#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UISlider.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // A Screen canvas in window pixels (output 800 x 600, scale 1), every slider 56 high unless said:
        //   left column (x 100, 300 wide):
        //     Volume (y 10)   bind volume, 0..1 step 0.1
        //     Fine   (y 100)  bind fine, 0..100 step 5
        //     Free   (y 200)  bind free, 0..1 continuous
        //     Unit   (y 300)  bind unit, 0..10 step 1
        //     Quarter(y 400)  bind quarter, 0..1 step 0.25
        //     Odd    (y 500)  bind odd, 0..1 step 0.3 (not a whole number of steps)
        //   right column (x 450, 300 wide):
        //     Off      (y 10)   disabled
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
          <element type="UISlider" name="Volume" bind="volume" action="SliderTestVolume" minValue="0" maxValue="1" step="0.1" x="100" y="10" width="300" height="56" color="30,30,30,255"/>
          <element type="UISlider" name="Fine" bind="fine" action="SliderTestFine" minValue="0" maxValue="100" step="5" x="100" y="100" width="300" height="56"/>
          <element type="UISlider" name="Free" bind="free" action="SliderTestFree" x="100" y="200" width="300" height="56"
                   trackColor="10,20,30,255" fillColor="40,50,60,255" thumbColor="70,80,90,255"/>
          <element type="UISlider" name="Unit" bind="unit" action="SliderTestUnit" minValue="0" maxValue="10" step="1" x="100" y="300" width="300" height="56"/>
          <element type="UISlider" name="Quarter" bind="quarter" action="SliderTestQuarter" step="0.25" x="100" y="400" width="300" height="56"/>
          <element type="UISlider" name="Odd" bind="odd" action="SliderTestOdd" step="0.3" x="100" y="500" width="300" height="56"/>
          <element type="UISlider" name="Off" bind="volume" action="SliderTestOff" step="0.1" x="450" y="10" width="300" height="56" disabled="true"/>
          <element type="UISlider" name="NoAction" bind="volume" step="0.1" x="450" y="100" width="300" height="56"/>
          <element type="UISlider" name="Other" bind="other" action="SliderTestOther" step="0.1" x="450" y="200" width="300" height="56"/>
          <element type="UISlider" name="Short" bind="volume" action="SliderTestShort" x="450" y="300" width="300" height="30"/>
          <element type="UISlider" name="Reversed" bind="reversed" action="SliderTestReversed" minValue="1" maxValue="0" x="450" y="400" width="300" height="56"/>
          <element type="UISlider" name="Narrow" bind="narrow" action="SliderTestNarrow" x="450" y="500" width="40" height="56"/>
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

        bool Near(const Rect& a, const Rect& b, float tolerance = 1e-2f) {
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
            PoolString LastAction() const {
                return _actions.empty() ? PoolString{} : _actions.back();
            }
            eastl::string LastValue() const {
                return _values.empty() ? eastl::string{} : eastl::string(_values.back().ToStringView().data(), _values.back().ToStringView().size());
            }
            /// The values of one action, joined with spaces
            eastl::string Values(const char* action) const {
                eastl::string joined;
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

    class UISliderTest : public ::testing::Test {
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

        UISlider& Slider(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            UISlider* typed = element ? element->Cast<UISlider>() : nullptr;
            EXPECT_NE(typed, nullptr) << name << " is not a slider";
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

        /// The x of the pointer that asks for @p fraction of the travel of a slider
        float XAt(const char* name, float fraction) const {
            const auto parts = UISlider::GetParts(Slider(name).rect, 0.0f);
            return parts.travelStart + (parts.travelEnd - parts.travelStart) * fraction;
        }

        float MiddleY(const char* name) const {
            const Rect& rect = Slider(name).rect;
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

    TEST_F(UISliderTest, ASliderBuiltFromAFileHasItsFields) {
        static_assert(std::is_base_of_v<UIElement, UISlider>);
        const auto& volume = Slider("Volume");
        EXPECT_EQ(volume.bind, PoolString::Intern("volume"));
        EXPECT_EQ(volume.action, PoolString::Intern("SliderTestVolume"));
        EXPECT_FLOAT_EQ(volume.minValue, 0.0f);
        EXPECT_FLOAT_EQ(volume.maxValue, 1.0f);
        EXPECT_FLOAT_EQ(volume.step, 0.1f);
        EXPECT_TRUE(volume.interactive);
        EXPECT_FALSE(volume.disabled);
        EXPECT_TRUE(Slider("Off").disabled);

        const auto made = AbstractFactory<UIElement>::GetInstance().Create("UISlider");
        ASSERT_TRUE(made);
        const auto* typed = made->Cast<UISlider>();
        ASSERT_NE(typed, nullptr);
        EXPECT_FLOAT_EQ(typed->minValue, 0.0f);
        EXPECT_FLOAT_EQ(typed->maxValue, 1.0f);
        EXPECT_FLOAT_EQ(typed->step, 0.0f);
        EXPECT_TRUE(typed->interactive);
    }

    TEST_F(UISliderTest, TheThumbStandsAtTheFractionOfTheTravel) {
        const auto& volume = Slider("Volume");
        for (const float fraction : {0.0f, 0.25f, 1.0f}) {
            SetVar("volume", fraction == 0.0f ? "0" : fraction == 0.25f ? "0.25" : "1");
            Draw();
            EXPECT_NEAR(volume.GetFraction(), fraction, 1e-6f);
            const auto parts = UISlider::GetParts(volume.rect, fraction);
            const DrawCall* thumb = FindRect(parts.thumb);
            ASSERT_NE(thumb, nullptr) << fraction;
            // The centre of the thumb is at the fraction of the travel: 56 units of thumb, so the travel is 28 units in from each end
            EXPECT_NEAR(thumb->rect.x + thumb->rect.w * 0.5f, volume.rect.x + 28.0f + (volume.rect.w - 56.0f) * fraction, 1e-2f) << fraction;
        }
    }

    TEST_F(UISliderTest, AnUnsetOrMalformedVariableIsTheMinimumAndOutOfRangeIsClamped) {
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

    TEST_F(UISliderTest, TheThumbIsASquareOfTheHeightAndNeverLeavesTheRect) {
        for (const char* name : {"Volume", "Fine", "Short", "Narrow", "Reversed"}) {
            const auto& slider = Slider(name);
            for (const float fraction : {0.0f, 0.5f, 1.0f}) {
                const auto parts = UISlider::GetParts(slider.rect, fraction);
                EXPECT_NEAR(parts.thumb.w, parts.thumb.h, 1e-4f) << name;
                EXPECT_NEAR(parts.thumb.h, std::min(slider.rect.h, slider.rect.w), 1e-4f) << name;
                EXPECT_TRUE(Inside(parts.thumb, slider.rect)) << name << " at " << fraction;
                EXPECT_TRUE(Inside(parts.track, slider.rect)) << name;
                EXPECT_LE(parts.track.h, UISlider::kTrackHeight + 1e-4f) << name;
            }
        }
        const auto& volume = Slider("Volume");
        EXPECT_NEAR(UISlider::GetParts(volume.rect, 0.0f).thumb.x, volume.rect.x, 1e-4f);
        EXPECT_NEAR(UISlider::GetParts(volume.rect, 1.0f).thumb.x + 56.0f, volume.rect.x + volume.rect.w, 1e-4f);
        EXPECT_NEAR(UISlider::GetParts(volume.rect, 0.5f).track.y + UISlider::kTrackHeight * 0.5f, volume.rect.y + 28.0f, 1e-4f) << "the track is in the middle of the height";
    }

    TEST_F(UISliderTest, TheFilledPartRunsToTheCentreOfTheThumb) {
        SetVar("free", "0.5");
        Draw();
        const auto& free = Slider("Free");
        const auto parts = UISlider::GetParts(free.rect, 0.5f);
        const DrawCall* track = FindRect(parts.track);
        const DrawCall* filled = FindRect(parts.filled);
        const DrawCall* thumb = FindRect(parts.thumb);
        ASSERT_TRUE(track && filled && thumb);
        EXPECT_EQ(track->color.r, 10);
        EXPECT_EQ(filled->color.r, 40);
        EXPECT_EQ(thumb->color.r, 70);
        EXPECT_NEAR(filled->rect.x + filled->rect.w, thumb->rect.x + thumb->rect.w * 0.5f, 1e-2f);

        // Derived colors: a slider with none of its own still has three looks
        const auto& volume = Slider("Volume");
        EXPECT_GT(volume.GetCurrentTrack().a, 0);
        EXPECT_GT(volume.GetCurrentFill().a, 0);
        EXPECT_GT(volume.GetCurrentThumb().a, 0);
    }

    // ---- a press asks for the value under the pointer ----------------------------------------------------------------------------

    TEST_F(UISliderTest, APressAtTheEdgesAndTheMiddleAsksForTheMinimumTheMiddleAndTheMaximum) {
        ActionLog actions;
        const auto& volume = Slider("Volume");

        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.0f));
        EXPECT_EQ(actions.LastValue(), "0");
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.5f));
        EXPECT_EQ(actions.LastValue(), "0.5");
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 1.0f));
        EXPECT_EQ(actions.LastValue(), "1");
        EXPECT_EQ(actions.Count("SliderTestVolume"), 3u);

        // The edges of the rect and everything within half a thumb of them are the ends
        for (const float x : {volume.rect.x, volume.rect.x + 1.0f, volume.rect.x + 27.0f}) {
            ASSERT_TRUE(Down("Volume", x));
            ASSERT_TRUE(Up(x, MiddleY("Volume")));
            EXPECT_EQ(actions.LastValue(), "0") << x;
        }
        for (const float x : {volume.rect.x + volume.rect.w - 27.0f, volume.rect.x + volume.rect.w - 1.0f, volume.rect.x + volume.rect.w - 0.01f}) {
            ASSERT_TRUE(Down("Volume", x));
            ASSERT_TRUE(Up(x, MiddleY("Volume")));
            EXPECT_EQ(actions.LastValue(), "1") << x;
        }
    }

    TEST_F(UISliderTest, ThePressChangesNothingByItselfAndTheGamesAnswerMovesTheThumb) {
        ActionLog actions;
        const auto& volume = Slider("Volume");
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.3f));
        ASSERT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.LastValue(), "0.3");

        Draw();
        EXPECT_FALSE(CoreManager::GetUIVars().Has(PoolString::Intern("volume"))) << "the slider does not set the variable";
        EXPECT_FLOAT_EQ(volume.GetFraction(), 0.0f);
        EXPECT_NE(FindRect(UISlider::GetParts(volume.rect, 0.0f).thumb), nullptr) << "no answer: the thumb stays";
        EXPECT_EQ(FindRect(UISlider::GetParts(volume.rect, 0.3f).thumb), nullptr);

        SetVar("volume", "0.3");  // the game's answer
        Draw();
        EXPECT_NE(FindRect(UISlider::GetParts(volume.rect, 0.3f).thumb), nullptr);
        EXPECT_EQ(FindRect(UISlider::GetParts(volume.rect, 0.0f).thumb), nullptr);
    }

    TEST_F(UISliderTest, ValuesAreSnappedToTheStepAndWrittenShortAndExact) {
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

    TEST_F(UISliderTest, ARangeThatIsNotAWholeNumberOfStepsStillReachesItsMaximum) {
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

    TEST_F(UISliderTest, AValueReadBackFromTheVariableLandsOnTheSameThumb) {
        for (const char* name : {"Volume", "Fine", "Unit", "Quarter", "Free", "Odd"}) {
            const auto& slider = Slider(name);
            for (int i = 0; i <= 100; ++i) {
                const float value = slider.ValueAt(XAt(name, static_cast<float>(i) / 100.0f));
                const eastl::string text = UISlider::FormatValue(value);
                EXPECT_LE(text.size(), 9u) << name << " " << text.c_str();
                CoreManager::GetUIVars().Set(slider.bind, eastl::string_view(text.data(), text.size()));
                const float expected = UIText::FractionInRange(value, slider.minValue, slider.maxValue);
                EXPECT_NEAR(slider.GetFraction(), expected, 1e-6f) << name << " " << text.c_str();
            }
        }
    }

    TEST_F(UISliderTest, TheTextOfAValueIsShortAndExact) {
        EXPECT_EQ(UISlider::FormatValue(0.3f), "0.3");
        EXPECT_EQ(UISlider::FormatValue(0.1f * 3.0f), "0.3");
        EXPECT_EQ(UISlider::FormatValue(0.30000001f), "0.3");
        EXPECT_EQ(UISlider::FormatValue(7.0f), "7");
        EXPECT_EQ(UISlider::FormatValue(35.0f), "35");
        EXPECT_EQ(UISlider::FormatValue(100.0f), "100");
        EXPECT_EQ(UISlider::FormatValue(0.0f), "0");
        EXPECT_EQ(UISlider::FormatValue(-0.0f), "0");
        EXPECT_EQ(UISlider::FormatValue(-1.5f), "-1.5");
        EXPECT_EQ(UISlider::FormatValue(0.000123f), "0.000123");
        EXPECT_EQ(UISlider::FormatValue(12345.67f), "12345.7");
        EXPECT_EQ(UISlider::FormatValue(0.01000001f), "0.01");
        EXPECT_EQ(UISlider::FormatValue(1234567.0f), "1234567");
        EXPECT_EQ(UISlider::FormatValue(std::numeric_limits<float>::quiet_NaN()), "0");
        EXPECT_EQ(UISlider::FormatValue(std::numeric_limits<float>::infinity()), "0");
        EXPECT_EQ(UISlider::FormatValue(1e30f).empty(), false);
    }

    // ---- a drag keeps asking ------------------------------------------------------------------------------------------------------

    TEST_F(UISliderTest, ADragEmitsOncePerChangeOfTheValue) {
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
        EXPECT_EQ(actions.Values("SliderTestVolume"), "0 0.1 0.2 0.5 0.4");
        EXPECT_EQ(actions.Total(), 5u) << "the release adds nothing";
        EXPECT_EQ(Slider("Volume").GetLastEmittedValue(), PoolString::Intern("0.4"));
    }

    TEST_F(UISliderTest, ADragThatLeavesTheSliderKeepsFollowingAndClamps) {
        ActionLog actions;
        const auto& volume = Slider("Volume");
        ASSERT_TRUE(Down("Volume", XAt("Volume", 0.5f)));
        Drag(XAt("Volume", 0.8f), volume.rect.y - 40.0f);                     // above
        Drag(XAt("Volume", 0.6f), volume.rect.y + volume.rect.h + 200.0f);    // far below, over other sliders
        Drag(volume.rect.x + volume.rect.w + 200.0f, volume.rect.y + 10.0f);  // past the end
        Drag(volume.rect.x - 90.0f, volume.rect.y + 10.0f);                   // before the start
        Drag(volume.rect.x - 90.0f, volume.rect.y + 12.0f);                   // and a little more: the same value
        ASSERT_TRUE(Up(volume.rect.x - 90.0f, volume.rect.y + 12.0f));
        EXPECT_EQ(actions.Values("SliderTestVolume"), "0.5 0.8 0.6 1 0");
        EXPECT_EQ(actions.Total(), 5u);
    }

    TEST_F(UISliderTest, AReleaseFiresNoSecondActionAndTwoSlidersDoNotDisturbEachOther) {
        ActionLog actions;
        ASSERT_TRUE(Down("Volume", XAt("Volume", 0.5f)));
        // Dragged over the other slider (which is to the right, so the pressed one clamps at its end): only the pressed one follows
        Drag(XAt("Other", 0.9f), MiddleY("Other"));
        ASSERT_TRUE(Up(XAt("Other", 0.9f), MiddleY("Other")));
        EXPECT_EQ(actions.Count("SliderTestOther"), 0u);
        EXPECT_EQ(actions.Values("SliderTestVolume"), "0.5 1");
        EXPECT_FALSE(Slider("Other").pressed);

        ASSERT_NO_FATAL_FAILURE(Tap("Other", 0.2f));
        EXPECT_EQ(actions.Values("SliderTestOther"), "0.2");
        EXPECT_EQ(actions.Count("SliderTestVolume"), 2u);

        // A press on the same value as the last gesture ended on still emits
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.9f));
        EXPECT_EQ(actions.Values("SliderTestVolume"), "0.5 1 0.9");
        ASSERT_NO_FATAL_FAILURE(Tap("Volume", 0.9f));
        EXPECT_EQ(actions.Values("SliderTestVolume"), "0.5 1 0.9 0.9");

        // Motion with no press held does nothing
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(XAt("Volume", 0.1f), MiddleY("Volume")));
        EXPECT_EQ(actions.Count("SliderTestVolume"), 4u);
    }

    TEST_F(UISliderTest, ADisabledSliderTakesThePressAndDoesNothing) {
        ActionLog actions;
        const auto& off = Slider("Off");
        ASSERT_TRUE(Down("Off", XAt("Off", 0.5f))) << "the press does not fall through to what is below";
        Drag(XAt("Off", 0.9f), MiddleY("Off"));
        ASSERT_TRUE(Up(XAt("Off", 0.9f), MiddleY("Off")));
        EXPECT_EQ(actions.Total(), 0u);
        EXPECT_TRUE(off.GetLastEmittedValue().Empty());

        SetVar("volume", "0.5");
        Draw();
        EXPECT_NE(FindRect(UISlider::GetParts(off.rect, 0.5f).thumb), nullptr) << "it still shows the value";
        EXPECT_LT(off.GetCurrentThumb().a, Slider("Volume").GetCurrentThumb().a) << "and is drawn grey";
        EXPECT_LT(off.GetCurrentTrack().a, Slider("Volume").GetCurrentTrack().a);
        EXPECT_LT(off.GetCurrentFill().a, Slider("Volume").GetCurrentFill().a);
    }

    TEST_F(UISliderTest, AnEmptyActionEmitsNothing) {
        ActionLog actions;
        ASSERT_TRUE(Down("NoAction", XAt("NoAction", 0.5f)));
        Drag(XAt("NoAction", 0.9f), MiddleY("NoAction"));
        ASSERT_TRUE(Up(XAt("NoAction", 0.9f), MiddleY("NoAction")));
        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UISliderTest, AnEmptyOrReversedRangeAsksForTheMinimumOnly) {
        ActionLog actions;
        ASSERT_NO_FATAL_FAILURE(Tap("Reversed", 0.7f));
        EXPECT_EQ(actions.LastValue(), "1") << "the minimum value of the range as written";
        EXPECT_FLOAT_EQ(Slider("Reversed").GetFraction(), 0.0f);
        SetVar("reversed", "0.5");
        EXPECT_FLOAT_EQ(Slider("Reversed").GetFraction(), 0.0f);
        const auto parts = UISlider::GetParts(Slider("Reversed").rect, Slider("Reversed").GetFraction());
        EXPECT_TRUE(Inside(parts.thumb, Slider("Reversed").rect));
    }

    TEST_F(UISliderTest, ASliderNarrowerThanItsThumbStillWorks) {
        ActionLog actions;
        const auto& narrow = Slider("Narrow");
        EXPECT_TRUE(Inside(UISlider::GetParts(narrow.rect, 1.0f).thumb, narrow.rect));
        ASSERT_NO_FATAL_FAILURE(Tap("Narrow", 1.0f));
        EXPECT_EQ(actions.LastValue(), "0");
        const float x = narrow.rect.x + narrow.rect.w - 1.0f;
        ASSERT_TRUE(Down("Narrow", x));
        ASSERT_TRUE(Up(x, MiddleY("Narrow")));
        EXPECT_EQ(actions.Count("SliderTestNarrow"), 2u);
    }

    TEST_F(UISliderTest, ThePressedThumbIsDarkerAndTheHoveredOneLighter) {
        const auto& free = Slider("Free");
        const Color rest = free.GetCurrentThumb();
        ASSERT_TRUE(Down("Free", XAt("Free", 0.5f)));
        EXPECT_LT(free.GetCurrentThumb().r, rest.r);
        Drag(700.0f, 550.0f);  // the press is held wherever the pointer went
        EXPECT_LT(free.GetCurrentThumb().r, rest.r);
        ASSERT_TRUE(Up(700.0f, 550.0f));
        EXPECT_EQ(free.GetCurrentThumb().r, rest.r);

        CoreManager::GetUIInput().DispatchMouseMotion(Motion(XAt("Free", 0.5f), MiddleY("Free")));
        EXPECT_GT(free.GetCurrentThumb().r, rest.r);
    }

    // ---- the validator -----------------------------------------------------------------------------------------------------------

    TEST_F(UISliderTest, ASliderBelowTheTouchSizeIsCaught) {
        UILayoutValidatorOptions options;
        size_t small = 0;
        for (const UILayoutIssue& issue : _canvas->GetLayout().Validate(options)) {
            if (issue.kind == UILayoutIssueKind::TouchTargetTooSmall) {
                ++small;
                EXPECT_EQ(issue.element, PoolString::Intern("Short"));
            }
        }
        EXPECT_EQ(small, 1u) << "only the 30-unit slider; the 56-unit ones hold the touch size by their height";
    }

    // ---- files --------------------------------------------------------------------------------------------------------------------

    TEST_F(UISliderTest, TheFieldsSurviveAnXmlRoundTrip) {
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

        const auto* fine = loaded->GetRoot().FindByName(PoolString::Intern("Fine"))->Cast<UISlider>();
        ASSERT_NE(fine, nullptr);
        EXPECT_EQ(fine->bind, PoolString::Intern("fine"));
        EXPECT_EQ(fine->action, PoolString::Intern("SliderTestFine"));
        EXPECT_FLOAT_EQ(fine->maxValue, 100.0f);
        EXPECT_FLOAT_EQ(fine->step, 5.0f);
        const auto* free = loaded->GetRoot().FindByName(PoolString::Intern("Free"))->Cast<UISlider>();
        ASSERT_NE(free, nullptr);
        EXPECT_EQ(free->trackColor.g, 20);
        EXPECT_EQ(free->fillColor.g, 50);
        EXPECT_EQ(free->thumbColor.g, 80);
        EXPECT_TRUE(loaded->GetRoot().FindByName(PoolString::Intern("Off"))->Cast<UISlider>()->disabled);
        EXPECT_FLOAT_EQ(loaded->GetRoot().FindByName(PoolString::Intern("Reversed"))->Cast<UISlider>()->minValue, 1.0f);
        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
        }
    }

    TEST_F(UISliderTest, TheFieldsSurviveABinaryRoundTrip) {
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

        const auto* free = loaded->GetRoot().FindByName(PoolString::Intern("Free"))->Cast<UISlider>();
        ASSERT_NE(free, nullptr);
        EXPECT_EQ(free->thumbColor.b, 90);
        EXPECT_FLOAT_EQ(loaded->GetRoot().FindByName(PoolString::Intern("Volume"))->Cast<UISlider>()->step, 0.05f);
        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
        }
    }

}  // namespace BECore::Tests
