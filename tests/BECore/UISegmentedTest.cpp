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
#include <BECore/UI/UIChoice.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UILabel.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UIStack.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // A Screen canvas in window pixels (output 800 x 600, scale 1):
        //   Mode    (10, 10, 304 x 48): two segments by flex, 150 wide each with the spacing of 4
        //   Level   (10, 100, 412 x 48): four segments, 100 wide each
        //   Mixed   (10, 200, 500 x 48) bound to `mode`: a label, a segment and a nested group `Inner` bound to `inner` (its buttons are not Mixed's segments)
        //   Plain   (10, 300, 304 x 48): a UIStack, not a group, with a button whose value is "two"
        //   Unbound (10, 400, 304 x 48): no `bind`; one segment has value "two", the other no value
        //   Comp    (10, 470, 412 x 48): composed segments bound to `comp`: a caption on the element, an icon-left one (clicker, an image child, a caption child), a disabled one
        //   Forms   (10, 530, 500 x 48) bound to `comp`: a label, a UIButton, a composed segment and a nested group `FormsInner` of a composed segment bound to `inner`
        constexpr const char* kSegmentedXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UISegmented" name="Mode" bind="mode" x="10" y="10" width="304" height="48" direction="Horizontal" spacing="4" color="30,30,30,255">
            <children>
              <element type="UIButton" name="ModeTwo" flex="1" height="48" text="Two players" value="two" action="SegTestMode" color="70,70,100,255"/>
              <element type="UIButton" name="ModeComputer" flex="1" height="48" text="Computer" value="computer" action="SegTestMode" color="70,70,100,255"/>
            </children>
          </element>
          <element type="UISegmented" name="Level" bind="level" x="10" y="100" width="412" height="48" direction="Horizontal" spacing="4">
            <children>
              <element type="UIButton" name="Easy" flex="1" height="48" text="Easy" value="easy" action="SegTestLevel" color="70,70,100,255"/>
              <element type="UIButton" name="Medium" flex="1" height="48" text="Medium" value="medium" action="SegTestLevel" color="70,70,100,255"/>
              <element type="UIButton" name="Hard" flex="1" height="48" text="Hard" value="hard" action="SegTestLevel" color="70,70,100,255"/>
              <element type="UIButton" name="Max" flex="1" height="48" text="Max" value="max" action="SegTestLevel" color="70,70,100,255"/>
            </children>
          </element>
          <element type="UISegmented" name="Mixed" bind="mode" x="10" y="200" width="500" height="48" direction="Horizontal" spacing="4">
            <children>
              <element type="UILabel" name="Caption" width="100" height="48" text="Mode"/>
              <element type="UIButton" name="MixedTwo" width="100" height="48" text="Two" value="two" action="SegTestMixed" color="70,70,100,255"/>
              <element type="UISegmented" name="Inner" bind="inner" width="200" height="48" direction="Horizontal" spacing="4">
                <children>
                  <element type="UIButton" name="InnerTwo" flex="1" height="48" text="Two" value="two" action="SegTestInner" color="70,70,100,255"/>
                  <element type="UIButton" name="InnerX" flex="1" height="48" text="X" value="x" action="SegTestInner" color="70,70,100,255"/>
                </children>
              </element>
            </children>
          </element>
          <element type="UIStack" name="Plain" x="10" y="300" width="304" height="48" direction="Horizontal">
            <children>
              <element type="UIButton" name="PlainTwo" width="100" height="48" text="Two" value="two" action="SegTestPlain" color="70,70,100,255"/>
            </children>
          </element>
          <element type="UISegmented" name="Unbound" x="10" y="400" width="304" height="48" direction="Horizontal" spacing="4">
            <children>
              <element type="UIButton" name="UnboundTwo" flex="1" height="48" text="Two" value="two" action="SegTestUnbound" color="70,70,100,255"/>
              <element type="UIButton" name="UnboundNone" flex="1" height="48" text="None" action="SegTestUnbound" color="70,70,100,255"/>
            </children>
          </element>
          <element type="UISegmented" name="Comp" bind="comp" x="10" y="470" width="412" height="48" direction="Horizontal" spacing="4">
            <children>
              <element type="UIElement" name="CompPlain" flex="1" height="48" color="70,70,100,255" selectedColor="40,110,60,255">
                <components>
                  <component type="UITextComponent" text="Plain" align="Center" verticalAlign="Center" textColor="10,200,30,255"/>
                  <component type="UIInputClicker" action="SegTestComp" value="plain"/>
                </components>
              </element>
              <element type="UIElement" name="CompIcon" flex="1" height="48" color="70,70,100,255" layout="Stack" direction="Horizontal" spacing="6" crossAlign="Center">
                <components><component type="UIInputClicker" action="SegTestComp" value="icon"/></components>
                <children>
                  <element type="UIElement" name="CompIconImage" width="24" height="24"><components><component type="UIImageComponent"/></components></element>
                  <element type="UIElement" name="CompIconCaption" flex="1" height="100%">
                    <components><component type="UITextComponent" text="Icon" align="Center" verticalAlign="Center" textColor="200,100,50,255"/></components>
                  </element>
                </children>
              </element>
              <element type="UIElement" name="CompOff" flex="1" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Off" align="Center" verticalAlign="Center"/>
                  <component type="UIInputClicker" action="SegTestComp" value="off" disabled="true"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UISegmented" name="Forms" bind="comp" x="10" y="530" width="500" height="48" direction="Horizontal" spacing="4">
            <children>
              <element type="UILabel" name="FormsCaption" width="60" height="48" text="Form"/>
              <element type="UIButton" name="FormsButton" width="100" height="48" text="Button" value="plain" action="SegTestForms" color="70,70,100,255"/>
              <element type="UIElement" name="FormsComposed" width="100" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Composed" align="Center" verticalAlign="Center"/>
                  <component type="UIInputClicker" action="SegTestForms" value="icon"/>
                </components>
              </element>
              <element type="UISegmented" name="FormsInner" bind="inner" width="200" height="48" direction="Horizontal" spacing="4">
                <children>
                  <element type="UIElement" name="FormsInnerOne" flex="1" height="48" color="70,70,100,255">
                    <components>
                      <component type="UITextComponent" text="One" align="Center" verticalAlign="Center"/>
                      <component type="UIInputClicker" action="SegTestFormsInner" value="plain"/>
                    </components>
                  </element>
                </children>
              </element>
            </children>
          </element>
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

        bool Near(const Rect& a, const Rect& b, float tolerance = 1e-2f) {
            return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.w - b.w) <= tolerance && std::abs(a.h - b.h) <= tolerance;
        }

        /// The actions the buttons emit, with the value each carried, in order
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

    class UISegmentedTest : public ::testing::Test {
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
            ASSERT_TRUE(deserializer.LoadFromString(kSegmentedXml));
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

        template <typename T>
        T& Get(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            T* typed = element ? element->Cast<T>() : nullptr;
            EXPECT_NE(typed, nullptr) << name << " is not the expected control";
            return *typed;
        }

        bool Selected(const char* name) const {
            return Get<UIButton>(name).selected;
        }

        /// True when the bar of a selected button was drawn at the foot of the rect of @p name in the last draw
        bool BarDrawn(const char* name) const {
            const UIButton& button = Get<UIButton>(name);
            const Rect bar{button.rect.x, button.rect.y + button.rect.h - UIButton::kSelectedBarHeight, button.rect.w, UIButton::kSelectedBarHeight};
            return std::any_of(_renderer->calls.begin(), _renderer->calls.end(), [&](const DrawCall& call) { return call.kind == DrawKind::Rect && Near(call.rect, bar); });
        }

        /// Any element by name, whatever its class (`Get<T>` wants the exact type)
        UIElement& Element(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return *element;
        }

        bool SelectedElement(const char* name) const {
            return Element(name).selected;
        }

        /// The colour of the bar of the selected-state look drawn at the foot of the rect of @p name in the last draw, none when there was no bar
        std::optional<Color> BarColorOf(const char* name) const {
            const UIElement& element = Element(name);
            const Rect bar{element.rect.x, element.rect.y + element.rect.h - UIStateLook::kSelectedBarHeight, element.rect.w, UIStateLook::kSelectedBarHeight};
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && Near(call.rect, bar)) {
                    return call.color;
                }
            }
            return std::nullopt;
        }

        void Click(const char* name) {
            const UIElement* found = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            ASSERT_NE(found, nullptr) << name;
            const UIElement& element = *found;
            const float x = element.rect.x + element.rect.w * 0.5f;
            const float y = element.rect.y + element.rect.h * 0.5f;
            ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true)));
            ASSERT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false)));
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
    };

    // ---- the selection follows the variable -------------------------------------------------------------------------------

    TEST_F(UISegmentedTest, AGroupBuiltFromAFileIsAStackWithTheButtonsAsSegments) {
        const auto& mode = Get<UISegmented>("Mode");
        static_assert(std::is_base_of_v<UIStack, UISegmented>);
        EXPECT_EQ(mode.layout, UILayoutKind::Stack);
        EXPECT_EQ(mode.bind, PoolString::Intern("mode"));
        EXPECT_FALSE(mode.disabled);
        ASSERT_EQ(mode.children.size(), 2u);
        EXPECT_TRUE(mode.children[0]->Is<UIButton>());

        const auto made = AbstractFactory<UIElement>::GetInstance().Create("UISegmented");
        ASSERT_TRUE(made);
        EXPECT_EQ(made->layout, UILayoutKind::Stack) << "a stack from the start, as a UIStack is";
    }

    TEST_F(UISegmentedTest, NothingIsSelectedWhileTheVariableIsUnset) {
        Draw();
        EXPECT_FALSE(Selected("ModeTwo"));
        EXPECT_FALSE(Selected("ModeComputer"));
        EXPECT_FALSE(Selected("Easy"));
        EXPECT_FALSE(Selected("Max"));
        EXPECT_TRUE(Get<UISegmented>("Mode").GetSelectedValue().empty());
    }

    TEST_F(UISegmentedTest, TheSegmentWhoseValueIsTheVariableIsSelected) {
        SetVar("mode", "computer");
        SetVar("level", "hard");
        Draw();
        EXPECT_FALSE(Selected("ModeTwo"));
        EXPECT_TRUE(Selected("ModeComputer"));
        EXPECT_FALSE(Selected("Easy"));
        EXPECT_FALSE(Selected("Medium"));
        EXPECT_TRUE(Selected("Hard"));
        EXPECT_FALSE(Selected("Max"));
        EXPECT_EQ(Get<UISegmented>("Mode").GetSelectedValue(), "computer");
        EXPECT_EQ(Get<UISegmented>("Level").GetSelectedValue(), "hard");
    }

    TEST_F(UISegmentedTest, ChangingTheVariableMovesTheSelectionAtTheNextDraw) {
        SetVar("level", "easy");
        Draw();
        EXPECT_TRUE(Selected("Easy"));

        SetVar("level", "max");
        EXPECT_TRUE(Selected("Easy")) << "nothing moves before the next draw";
        Draw();
        EXPECT_FALSE(Selected("Easy"));
        EXPECT_TRUE(Selected("Max"));

        CoreManager::GetUIVars().Remove(PoolString::Intern("level"));
        Draw();
        EXPECT_FALSE(Selected("Max")) << "a variable that goes away selects nothing";
    }

    TEST_F(UISegmentedTest, AValueThatMatchesNoSegmentSelectsNothing) {
        SetVar("level", "impossible");
        Draw();
        for (const char* name : {"Easy", "Medium", "Hard", "Max"}) {
            EXPECT_FALSE(Selected(name)) << name;
        }
        SetVar("level", "Hard");
        Draw();
        EXPECT_FALSE(Selected("Hard")) << "the match is exact, case included";
    }

    TEST_F(UISegmentedTest, SyncChildrenNeedsNoDraw) {
        SetVar("mode", "two");
        auto& mode = Get<UISegmented>("Mode");
        EXPECT_FALSE(Selected("ModeTwo"));
        mode.SyncChildren();
        EXPECT_TRUE(Selected("ModeTwo"));
        EXPECT_FALSE(Selected("ModeComputer"));
    }

    TEST_F(UISegmentedTest, AnEmptyBindAnEmptyValueAndAnEmptyVariableSelectNothing) {
        // `Unbound` has no bind: even a variable with the name of a value does not select
        SetVar("two", "two");
        Draw();
        EXPECT_FALSE(Selected("UnboundTwo"));
        EXPECT_FALSE(Selected("UnboundNone"));
        EXPECT_TRUE(Get<UISegmented>("Unbound").GetSelectedValue().empty());

        // A variable that is set but empty does not select the segment that has no value either
        auto& unbound = Get<UISegmented>("Unbound");
        unbound.bind = PoolString::Intern("choice");
        SetVar("choice", "");
        Draw();
        EXPECT_FALSE(Selected("UnboundTwo"));
        EXPECT_FALSE(Selected("UnboundNone")) << "an empty value is never the chosen one";

        SetVar("choice", "two");
        Draw();
        EXPECT_TRUE(Selected("UnboundTwo"));
        EXPECT_FALSE(Selected("UnboundNone"));
    }

    TEST_F(UISegmentedTest, OnlyTheDirectButtonChildrenAreSegments) {
        SetVar("mode", "two");
        SetVar("inner", "x");
        Draw();
        EXPECT_TRUE(Selected("MixedTwo")) << "the button next to a label is a segment";
        EXPECT_EQ(Get<UILabel>("Caption").text, "Mode") << "the label is left alone";

        // A nested group manages its own children: `Inner` is bound to `inner`, not to `mode`
        EXPECT_FALSE(Selected("InnerTwo")) << "its value is the one of Mixed, but the nested group has its own variable";
        EXPECT_TRUE(Selected("InnerX"));

        // The outer group's disabled flag reaches its own segments only
        Get<UISegmented>("Mixed").disabled = true;
        Draw();
        EXPECT_TRUE(Get<UIButton>("MixedTwo").groupDisabled);
        EXPECT_FALSE(Get<UIButton>("InnerX").groupDisabled);
    }

    TEST_F(UISegmentedTest, APlainStackIsUntouched) {
        SetVar("mode", "two");
        SetVar("plain", "two");
        Draw();
        EXPECT_FALSE(Selected("PlainTwo"));
        EXPECT_FALSE(Get<UIButton>("PlainTwo").groupDisabled);
        EXPECT_FALSE(Get<UIStack>("Plain").Is<UISegmented>());
    }

    // ---- drawing and layout -----------------------------------------------------------------------------------------------

    TEST_F(UISegmentedTest, OnlyTheSelectedSegmentDrawsItsBar) {
        SetVar("level", "medium");
        Draw();
        EXPECT_TRUE(BarDrawn("Medium"));
        EXPECT_FALSE(BarDrawn("Easy"));
        EXPECT_FALSE(BarDrawn("Hard"));
        EXPECT_FALSE(BarDrawn("Max"));

        SetVar("level", "max");
        Draw();
        EXPECT_FALSE(BarDrawn("Medium"));
        EXPECT_TRUE(BarDrawn("Max"));

        SetVar("level", "");
        Draw();
        for (const char* name : {"Easy", "Medium", "Hard", "Max"}) {
            EXPECT_FALSE(BarDrawn(name)) << name;
        }
    }

    TEST_F(UISegmentedTest, TheSegmentsShareTheWidthByFlex) {
        Draw();
        const Rect two = Get<UIButton>("ModeTwo").rect;
        const Rect computer = Get<UIButton>("ModeComputer").rect;
        EXPECT_TRUE(Near(two, {10.0f, 10.0f, 150.0f, 48.0f})) << two.x << " " << two.y << " " << two.w << " " << two.h;
        EXPECT_TRUE(Near(computer, {164.0f, 10.0f, 150.0f, 48.0f})) << computer.x << " " << computer.w;

        const char* const levels[] = {"Easy", "Medium", "Hard", "Max"};
        for (size_t i = 0; i < 4; ++i) {
            EXPECT_TRUE(Near(Get<UIButton>(levels[i]).rect, {10.0f + 104.0f * static_cast<float>(i), 100.0f, 100.0f, 48.0f})) << levels[i];
        }
    }

    TEST_F(UISegmentedTest, TheSelectionIsDrawnWithoutLayingTheLabelsOutAgain) {
        SetVar("level", "easy");
        Draw();
        const uint32_t layouts = Get<UIButton>("Easy").GetLayoutCount() + Get<UIButton>("Max").GetLayoutCount();
        Draw();
        Draw();
        EXPECT_EQ(Get<UIButton>("Easy").GetLayoutCount() + Get<UIButton>("Max").GetLayoutCount(), layouts) << "an unchanged screen lays nothing out";
    }

    // ---- disabled ----------------------------------------------------------------------------------------------------------

    TEST_F(UISegmentedTest, ADisabledGroupGreysEverySegmentAndTakesNoChoice) {
        SetVar("level", "hard");
        auto& level = Get<UISegmented>("Level");
        level.disabled = true;
        Draw();
        for (const char* name : {"Easy", "Medium", "Hard", "Max"}) {
            EXPECT_TRUE(Get<UIButton>(name).groupDisabled) << name;
        }
        EXPECT_TRUE(Selected("Hard")) << "a disabled group still shows what is chosen";
    }

    TEST_F(UISegmentedTest, ClicksOnADisabledGroupEmitNothingAndEnablingItGivesThemBack) {
        ActionLog actions;
        auto& level = Get<UISegmented>("Level");
        level.disabled = true;
        Draw();
        ASSERT_NO_FATAL_FAILURE(Click("Medium"));
        EXPECT_EQ(actions.Total(), 0u);

        level.disabled = false;
        Draw();
        for (const char* name : {"Easy", "Medium", "Hard", "Max"}) {
            EXPECT_FALSE(Get<UIButton>(name).groupDisabled) << name;
        }
        ASSERT_NO_FATAL_FAILURE(Click("Medium"));
        EXPECT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.LastAction(), PoolString::Intern("SegTestLevel"));
    }

    // ---- the click only reports -------------------------------------------------------------------------------------------

    TEST_F(UISegmentedTest, AClickEmitsTheValueAndSelectsNothing) {
        ActionLog actions;
        Draw();
        ASSERT_NO_FATAL_FAILURE(Click("ModeComputer"));
        EXPECT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.LastAction(), PoolString::Intern("SegTestMode"));
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("computer"));

        Draw();
        EXPECT_FALSE(Selected("ModeComputer")) << "the game owns the truth: the click did not change the variable";
        EXPECT_FALSE(CoreManager::GetUIVars().Has(PoolString::Intern("mode")));

        SetVar("mode", "computer");  // the game's answer
        Draw();
        EXPECT_TRUE(Selected("ModeComputer"));
        EXPECT_FALSE(Selected("ModeTwo"));
    }

    TEST_F(UISegmentedTest, AClickOnTheChosenSegmentReportsItAgain) {
        ActionLog actions;
        SetVar("level", "easy");
        Draw();
        ASSERT_NO_FATAL_FAILURE(Click("Easy"));
        ASSERT_NO_FATAL_FAILURE(Click("Easy"));
        EXPECT_EQ(actions.Total(), 2u) << "the group does not filter: the game decides what a repeated choice means";
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("easy"));
        EXPECT_TRUE(Selected("Easy"));
    }

    // ---- hiding ------------------------------------------------------------------------------------------------------------

    TEST_F(UISegmentedTest, AHiddenGroupDrawsNothingAndFollowsTheVariableWhenShown) {
        ActionLog actions;
        SetVar("level", "easy");
        Draw();
        ASSERT_TRUE(Selected("Easy"));

        ASSERT_TRUE(_canvas->SetElementVisible(PoolString::Intern("Level"), false));
        SetVar("level", "hard");
        Draw();
        EXPECT_TRUE(Get<UIButton>("Hard").rect.IsEmpty());
        EXPECT_FALSE(BarDrawn("Hard"));
        EXPECT_FALSE(BarDrawn("Easy"));

        ASSERT_TRUE(_canvas->SetElementVisible(PoolString::Intern("Level"), true));
        Draw();
        EXPECT_FALSE(Selected("Easy"));
        EXPECT_TRUE(Selected("Hard"));
        EXPECT_TRUE(BarDrawn("Hard"));
        EXPECT_FALSE(Get<UIButton>("Hard").rect.IsEmpty());
    }

    // ---- reflection --------------------------------------------------------------------------------------------------------

    TEST_F(UISegmentedTest, TheBindAndTheDisabledFlagSurviveAnXmlRoundTrip) {
        auto& level = Get<UISegmented>("Level");
        level.disabled = true;

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

        const auto* again = loaded->GetRoot().FindByName(PoolString::Intern("Level"));
        ASSERT_NE(again, nullptr);
        const auto* typed = again->Cast<UISegmented>();
        ASSERT_NE(typed, nullptr);
        EXPECT_EQ(typed->bind, PoolString::Intern("level"));
        EXPECT_TRUE(typed->disabled);
        EXPECT_EQ(typed->children.size(), 4u);
        EXPECT_EQ(typed->layout, UILayoutKind::Stack);
        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
        }
    }

    TEST_F(UISegmentedTest, TheBindAndTheDisabledFlagSurviveABinaryRoundTrip) {
        Get<UISegmented>("Mode").disabled = true;

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

        const auto* typed = loaded->GetRoot().FindByName(PoolString::Intern("Mode"))->Cast<UISegmented>();
        ASSERT_NE(typed, nullptr);
        EXPECT_EQ(typed->bind, PoolString::Intern("mode"));
        EXPECT_TRUE(typed->disabled);
        ASSERT_EQ(typed->children.size(), 2u);
        EXPECT_EQ(typed->children[1]->Cast<UIButton>()->value, PoolString::Intern("computer"));
        ASSERT_EQ(loaded->_elements.size(), _canvas->_elements.size());
        for (size_t i = 0; i < loaded->_elements.size(); ++i) {
            EXPECT_STREQ(ElementToXml(*_canvas->_elements[i]).c_str(), ElementToXml(*loaded->_elements[i]).c_str()) << i;
        }
    }

    // ---- composed segments: an element with a clicker is a segment as a UIButton is ----------------------------------------

    TEST_F(UISegmentedTest, ComposedSegmentsAreSelectedByTheVariableThroughTheValueOfTheirClicker) {
        SetVar("comp", "icon");
        Draw();
        EXPECT_FALSE(SelectedElement("CompPlain"));
        EXPECT_TRUE(SelectedElement("CompIcon"));
        EXPECT_FALSE(SelectedElement("CompOff"));
        EXPECT_FALSE(Element("CompIconCaption").selected) << "only the direct children of the group are segments";

        SetVar("comp", "plain");
        Draw();
        EXPECT_TRUE(SelectedElement("CompPlain"));
        EXPECT_FALSE(SelectedElement("CompIcon"));

        SetVar("comp", "nothing");
        Draw();
        EXPECT_FALSE(SelectedElement("CompPlain"));
        EXPECT_FALSE(SelectedElement("CompIcon"));
    }

    TEST_F(UISegmentedTest, TheComposedSegmentsShareTheWidthByFlexLikeButtons) {
        Draw();
        EXPECT_TRUE(Near(Element("CompPlain").rect, {10.0f, 470.0f, 134.667f, 48.0f}, 0.1f));
        EXPECT_TRUE(Near(Element("CompIcon").rect, {148.667f, 470.0f, 134.667f, 48.0f}, 0.1f));
        EXPECT_TRUE(Near(Element("CompOff").rect, {287.333f, 470.0f, 134.667f, 48.0f}, 0.1f));
    }

    TEST_F(UISegmentedTest, TheSelectedBarOfAComposedSegmentTakesTheColourOfItsCaption) {
        SetVar("comp", "plain");
        Draw();
        ASSERT_TRUE(BarColorOf("CompPlain").has_value());
        EXPECT_EQ(BarColorOf("CompPlain")->g, 200u) << "the caption is on the element itself";
        EXPECT_FALSE(BarColorOf("CompIcon").has_value());

        SetVar("comp", "icon");
        Draw();
        ASSERT_TRUE(BarColorOf("CompIcon").has_value());
        EXPECT_EQ(BarColorOf("CompIcon")->r, 200u) << "an icon-left segment: the caption child";
        EXPECT_EQ(BarColorOf("CompIcon")->g, 100u);
        EXPECT_FALSE(BarColorOf("CompPlain").has_value());
    }

    TEST_F(UISegmentedTest, AGroupThatIsDisabledGreysItsComposedSegments) {
        SetVar("comp", "plain");
        Get<UISegmented>("Comp").disabled = true;
        Draw();
        EXPECT_TRUE(Element("CompPlain").groupDisabled);
        EXPECT_TRUE(Element("CompIcon").IsDisabled());
        EXPECT_TRUE(SelectedElement("CompPlain")) << "a disabled group still shows what is chosen";

        ActionLog actions;
        ASSERT_NO_FATAL_FAILURE(Click("CompIcon"));
        EXPECT_EQ(actions.Total(), 0u);

        Get<UISegmented>("Comp").disabled = false;
        Draw();
        EXPECT_FALSE(Element("CompPlain").groupDisabled);
        EXPECT_FALSE(Element("CompIcon").IsDisabled());
        ASSERT_NO_FATAL_FAILURE(Click("CompIcon"));
        EXPECT_EQ(actions.Total(), 1u);
    }

    TEST_F(UISegmentedTest, AClickOnAComposedSegmentEmitsTheActionAndTheValueOfItsClicker) {
        ActionLog actions;
        Draw();
        ASSERT_NO_FATAL_FAILURE(Click("CompIcon"));
        EXPECT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.LastAction(), PoolString::Intern("SegTestComp"));
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("icon"));

        Draw();
        EXPECT_FALSE(SelectedElement("CompIcon")) << "the group owns no choice: the click changed nothing";
    }

    TEST_F(UISegmentedTest, ADisabledComposedSegmentTakesTheClickAndEmitsNothing) {
        ActionLog actions;
        Draw();
        ASSERT_NO_FATAL_FAILURE(Click("CompOff"));
        EXPECT_EQ(actions.Total(), 0u);

        ASSERT_NO_FATAL_FAILURE(Click("CompPlain"));
        EXPECT_EQ(actions.Total(), 1u) << "the segment next to it still works";
    }

    TEST_F(UISegmentedTest, AUIButtonAndAComposedSegmentLiveInTheSameGroup) {
        SetVar("comp", "plain");
        SetVar("inner", "plain");
        Draw();
        EXPECT_TRUE(Selected("FormsButton"));
        EXPECT_FALSE(SelectedElement("FormsComposed"));
        EXPECT_EQ(Get<UILabel>("FormsCaption").text, "Form") << "the label is no segment";

        SetVar("comp", "icon");
        Draw();
        EXPECT_FALSE(Selected("FormsButton"));
        EXPECT_TRUE(SelectedElement("FormsComposed"));

        ActionLog actions;
        ASSERT_NO_FATAL_FAILURE(Click("FormsComposed"));
        EXPECT_EQ(actions.LastAction(), PoolString::Intern("SegTestForms"));
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("icon"));
        ASSERT_NO_FATAL_FAILURE(Click("FormsButton"));
        EXPECT_EQ(actions.LastAction(), PoolString::Intern("SegTestForms"));
        EXPECT_EQ(actions.LastValue(), PoolString::Intern("plain"));
        EXPECT_EQ(actions.Total(), 2u);
    }

    TEST_F(UISegmentedTest, ANestedGroupOfComposedSegmentsManagesItsOwnChildren) {
        SetVar("comp", "plain");
        SetVar("inner", "plain");
        Draw();
        EXPECT_TRUE(SelectedElement("FormsInnerOne")) << "its variable is `inner`";
        EXPECT_FALSE(SelectedElement("FormsInner")) << "a group is no segment of the outer one";

        SetVar("inner", "other");
        Get<UISegmented>("Forms").disabled = true;
        Draw();
        EXPECT_FALSE(SelectedElement("FormsInnerOne"));
        EXPECT_FALSE(Element("FormsInnerOne").groupDisabled) << "the outer flag reaches its own segments only";
        EXPECT_TRUE(Get<UIButton>("FormsButton").groupDisabled);
        EXPECT_TRUE(Element("FormsComposed").groupDisabled);
    }

}  // namespace BECore::Tests
