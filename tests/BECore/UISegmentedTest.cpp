#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIChoice.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UIStack.h>
#include <BECore/UI/UIStateLook.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIVars.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/UICanvasRoundTrip.h>
#include <Common/UIInputEvents.h>
#include <Common/UITestSupport.h>
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
        //   Forms   (10, 530, 500 x 48) bound to `comp`: a text, two composed segments and a nested group `FormsInner` of a composed segment bound to `inner`
        constexpr const char* kSegmentedXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UISegmented" name="Mode" bind="mode" x="10" y="10" width="304" height="48" direction="Horizontal" spacing="4" color="30,30,30,255">
            <children>
              <element type="UIElement" name="ModeTwo" flex="1" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Two players" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="SegTestMode" value="two"/>
                </components>
              </element>
              <element type="UIElement" name="ModeComputer" flex="1" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Computer" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="SegTestMode" value="computer"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UISegmented" name="Level" bind="level" x="10" y="100" width="412" height="48" direction="Horizontal" spacing="4">
            <children>
              <element type="UIElement" name="Easy" flex="1" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Easy" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="SegTestLevel" value="easy"/>
                </components>
              </element>
              <element type="UIElement" name="Medium" flex="1" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Medium" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="SegTestLevel" value="medium"/>
                </components>
              </element>
              <element type="UIElement" name="Hard" flex="1" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Hard" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="SegTestLevel" value="hard"/>
                </components>
              </element>
              <element type="UIElement" name="Max" flex="1" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Max" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="SegTestLevel" value="max"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UISegmented" name="Mixed" bind="mode" x="10" y="200" width="500" height="48" direction="Horizontal" spacing="4">
            <children>
              <element type="UIElement" name="Caption" width="100" height="48">
                <components><component type="UITextComponent" text="Mode"/></components>
              </element>
              <element type="UIElement" name="MixedTwo" width="100" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Two" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="SegTestMixed" value="two"/>
                </components>
              </element>
              <element type="UISegmented" name="Inner" bind="inner" width="200" height="48" direction="Horizontal" spacing="4">
                <children>
                  <element type="UIElement" name="InnerTwo" flex="1" height="48" color="70,70,100,255">
                    <components>
                      <component type="UITextComponent" text="Two" align="Center" verticalAlign="Center" wrap="false"/>
                      <component type="UIInputClicker" action="SegTestInner" value="two"/>
                    </components>
                  </element>
                  <element type="UIElement" name="InnerX" flex="1" height="48" color="70,70,100,255">
                    <components>
                      <component type="UITextComponent" text="X" align="Center" verticalAlign="Center" wrap="false"/>
                      <component type="UIInputClicker" action="SegTestInner" value="x"/>
                    </components>
                  </element>
                </children>
              </element>
            </children>
          </element>
          <element type="UIStack" name="Plain" x="10" y="300" width="304" height="48" direction="Horizontal">
            <children>
              <element type="UIElement" name="PlainTwo" width="100" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Two" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="SegTestPlain" value="two"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UISegmented" name="Unbound" x="10" y="400" width="304" height="48" direction="Horizontal" spacing="4">
            <children>
              <element type="UIElement" name="UnboundTwo" flex="1" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Two" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="SegTestUnbound" value="two"/>
                </components>
              </element>
              <element type="UIElement" name="UnboundNone" flex="1" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="None" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="SegTestUnbound"/>
                </components>
              </element>
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
              <element type="UIElement" name="FormsCaption" width="60" height="48">
                <components><component type="UITextComponent" text="Form"/></components>
              </element>
              <element type="UIElement" name="FormsButton" width="100" height="48" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Button" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="SegTestForms" value="plain"/>
                </components>
              </element>
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

        void SetVar(const char* name, const char* value) {
            CoreManager::GetUIVars().Set(PoolString::Intern(name), value);
        }

    }  // namespace

    class UISegmentedTest : public UISceneFixture {
    protected:
        void SetUp() override {
            UISceneFixture::SetUp();
            ASSERT_NO_FATAL_FAILURE(LoadScene(kSegmentedXml));
            ASSERT_TRUE(_canvas);
        }

        bool Selected(const char* name) const {
            return Element(name).selected;
        }

        /// Any element by name, whatever its class (`Get<T>` wants the exact type)
        UIElement& Element(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return Require(element);
        }

        /// The colour of the bar of the selected-state look drawn at the foot of the rect of @p name in the last draw, none when there was no bar
        Optional<Color> BarColorOf(const char* name) const {
            const UIElement& element = Element(name);
            const Rect bar{element.rect.x, element.rect.y + element.rect.h - UIStateLook::kSelectedBarHeight, element.rect.w, UIStateLook::kSelectedBarHeight};
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && NearRect(call.rect, bar)) {
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
    };

    // ---- the selection follows the variable -------------------------------------------------------------------------------

    TEST_F(UISegmentedTest, AGroupBuiltFromAFileIsAStackWithTheButtonsAsSegments) {
        const auto& mode = Get<UISegmented>("Mode");
        EXPECT_EQ(mode.layout, UILayoutKind::Stack);
        EXPECT_EQ(mode.bind, "mode"_intern);
        EXPECT_FALSE(mode.disabled);
        ASSERT_EQ(mode.children.size(), 2u);
        EXPECT_NE(mode.children[0]->GetComponent<UIInputClicker>(), nullptr);

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

        CoreManager::GetUIVars().Remove("level"_intern);
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
        unbound.bind = "choice"_intern;
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
        EXPECT_EQ(Require(Element("Caption").GetComponent<UITextComponent>()).text, "Mode") << "the label is left alone";

        EXPECT_FALSE(Selected("InnerTwo")) << "its value is the one of Mixed, but the nested group has its own variable";
        EXPECT_TRUE(Selected("InnerX"));

        // The outer group's disabled flag reaches its own segments only
        Get<UISegmented>("Mixed").disabled = true;
        Draw();
        EXPECT_TRUE(Element("MixedTwo").groupDisabled);
        EXPECT_FALSE(Element("InnerX").groupDisabled);
    }

    TEST_F(UISegmentedTest, APlainStackIsUntouched) {
        SetVar("mode", "two");
        SetVar("plain", "two");
        Draw();
        EXPECT_FALSE(Selected("PlainTwo"));
        EXPECT_FALSE(Element("PlainTwo").groupDisabled);
        EXPECT_FALSE(Get<UIStack>("Plain").Is<UISegmented>());
    }

    // ---- drawing and layout -----------------------------------------------------------------------------------------------

    TEST_F(UISegmentedTest, OnlyTheSelectedSegmentDrawsItsBar) {
        SetVar("level", "medium");
        Draw();
        EXPECT_TRUE(BarColorOf("Medium").has_value());
        EXPECT_FALSE(BarColorOf("Easy").has_value());
        EXPECT_FALSE(BarColorOf("Hard").has_value());
        EXPECT_FALSE(BarColorOf("Max").has_value());

        SetVar("level", "max");
        Draw();
        EXPECT_FALSE(BarColorOf("Medium").has_value());
        EXPECT_TRUE(BarColorOf("Max").has_value());

        SetVar("level", "");
        Draw();
        for (const char* name : {"Easy", "Medium", "Hard", "Max"}) {
            EXPECT_FALSE(BarColorOf(name).has_value()) << name;
        }
    }

    TEST_F(UISegmentedTest, TheSegmentsShareTheWidthByFlex) {
        Draw();
        const Rect two = Element("ModeTwo").rect;
        const Rect computer = Element("ModeComputer").rect;
        EXPECT_TRUE(NearRect(two, {10.0f, 10.0f, 150.0f, 48.0f})) << two.x << " " << two.y << " " << two.w << " " << two.h;
        EXPECT_TRUE(NearRect(computer, {164.0f, 10.0f, 150.0f, 48.0f})) << computer.x << " " << computer.w;

        const char* const levels[] = {"Easy", "Medium", "Hard", "Max"};
        for (size_t i = 0; i < 4; ++i) {
            EXPECT_TRUE(NearRect(Element(levels[i]).rect, {10.0f + 104.0f * static_cast<float>(i), 100.0f, 100.0f, 48.0f})) << levels[i];
        }
    }

    TEST_F(UISegmentedTest, TheSelectionIsDrawnWithoutLayingTheLabelsOutAgain) {
        SetVar("level", "easy");
        Draw();
        const auto layoutsOfTheTexts = [this] {
            return Require(Element("Easy").GetComponent<UITextComponent>()).GetLayoutCount() + Require(Element("Max").GetComponent<UITextComponent>()).GetLayoutCount();
        };
        const uint32_t layouts = layoutsOfTheTexts();
        Draw();
        Draw();
        EXPECT_EQ(layoutsOfTheTexts(), layouts) << "an unchanged screen lays nothing out";
    }

    // ---- disabled ----------------------------------------------------------------------------------------------------------

    TEST_F(UISegmentedTest, ADisabledGroupGreysEverySegmentAndTakesNoChoice) {
        SetVar("level", "hard");
        auto& level = Get<UISegmented>("Level");
        level.disabled = true;
        Draw();
        for (const char* name : {"Easy", "Medium", "Hard", "Max"}) {
            EXPECT_TRUE(Element(name).groupDisabled) << name;
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
            EXPECT_FALSE(Element(name).groupDisabled) << name;
        }
        ASSERT_NO_FATAL_FAILURE(Click("Medium"));
        EXPECT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.LastAction(), "SegTestLevel"_intern);
    }

    // ---- the click only reports -------------------------------------------------------------------------------------------

    TEST_F(UISegmentedTest, AClickEmitsTheValueAndSelectsNothing) {
        ActionLog actions;
        Draw();
        ASSERT_NO_FATAL_FAILURE(Click("ModeComputer"));
        EXPECT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.LastAction(), "SegTestMode"_intern);
        EXPECT_EQ(actions.LastValue(), "computer"_intern);

        Draw();
        EXPECT_FALSE(Selected("ModeComputer")) << "the game owns the truth: the click did not change the variable";
        EXPECT_FALSE(CoreManager::GetUIVars().Has("mode"_intern));

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
        EXPECT_EQ(actions.LastValue(), "easy"_intern);
        EXPECT_TRUE(Selected("Easy"));
    }

    // ---- hiding ------------------------------------------------------------------------------------------------------------

    TEST_F(UISegmentedTest, AHiddenGroupDrawsNothingAndFollowsTheVariableWhenShown) {
        ActionLog actions;
        SetVar("level", "easy");
        Draw();
        ASSERT_TRUE(Selected("Easy"));

        ASSERT_TRUE(_canvas->SetElementVisible("Level"_intern, false));
        SetVar("level", "hard");
        Draw();
        EXPECT_TRUE(Element("Hard").rect.IsEmpty());
        EXPECT_FALSE(BarColorOf("Hard").has_value());
        EXPECT_FALSE(BarColorOf("Easy").has_value());

        ASSERT_TRUE(_canvas->SetElementVisible("Level"_intern, true));
        Draw();
        EXPECT_FALSE(Selected("Easy"));
        EXPECT_TRUE(Selected("Hard"));
        EXPECT_TRUE(BarColorOf("Hard").has_value());
        EXPECT_FALSE(Element("Hard").rect.IsEmpty());
    }

    // ---- reflection --------------------------------------------------------------------------------------------------------

    TEST_F(UISegmentedTest, TheBindAndTheDisabledFlagSurviveAnXmlRoundTrip) {
        auto& level = Get<UISegmented>("Level");
        level.disabled = true;

        String xml;
        const auto loaded = XmlRoundTrip(*_canvas, &xml);
        ASSERT_TRUE(loaded);

        const auto* again = loaded->GetRoot().FindByName("Level"_intern);
        ASSERT_NE(again, nullptr);
        const auto* typed = again->Cast<UISegmented>();
        ASSERT_NE(typed, nullptr);
        EXPECT_EQ(typed->bind, "level"_intern);
        EXPECT_TRUE(typed->disabled);
        EXPECT_EQ(typed->children.size(), 4u);
        EXPECT_EQ(typed->layout, UILayoutKind::Stack);
        ExpectSameElementXml(*_canvas, *loaded);
    }

    TEST_F(UISegmentedTest, TheBindAndTheDisabledFlagSurviveABinaryRoundTrip) {
        Get<UISegmented>("Mode").disabled = true;

        const auto loaded = BinaryRoundTrip(*_canvas);
        ASSERT_TRUE(loaded);

        const auto* typed = Require(loaded->GetRoot().FindByName("Mode"_intern)).Cast<UISegmented>();
        ASSERT_NE(typed, nullptr);
        EXPECT_EQ(typed->bind, "mode"_intern);
        EXPECT_TRUE(typed->disabled);
        ASSERT_EQ(typed->children.size(), 2u);
        EXPECT_EQ(Require(typed->children[1]->GetComponent<UIInputClicker>()).value, "computer"_intern);
        ExpectSameElementXml(*_canvas, *loaded);
    }

    // ---- composed segments: an element with a clicker is a segment ----------------------------------------

    TEST_F(UISegmentedTest, ComposedSegmentsAreSelectedByTheVariableThroughTheValueOfTheirClicker) {
        SetVar("comp", "icon");
        Draw();
        EXPECT_FALSE(Selected("CompPlain"));
        EXPECT_TRUE(Selected("CompIcon"));
        EXPECT_FALSE(Selected("CompOff"));
        EXPECT_FALSE(Element("CompIconCaption").selected) << "only the direct children of the group are segments";

        SetVar("comp", "plain");
        Draw();
        EXPECT_TRUE(Selected("CompPlain"));
        EXPECT_FALSE(Selected("CompIcon"));

        SetVar("comp", "nothing");
        Draw();
        EXPECT_FALSE(Selected("CompPlain"));
        EXPECT_FALSE(Selected("CompIcon"));
    }

    TEST_F(UISegmentedTest, TheComposedSegmentsShareTheWidthByFlexLikeButtons) {
        Draw();
        EXPECT_TRUE(NearRect(Element("CompPlain").rect, {10.0f, 470.0f, 134.667f, 48.0f}, 0.1f));
        EXPECT_TRUE(NearRect(Element("CompIcon").rect, {148.667f, 470.0f, 134.667f, 48.0f}, 0.1f));
        EXPECT_TRUE(NearRect(Element("CompOff").rect, {287.333f, 470.0f, 134.667f, 48.0f}, 0.1f));
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
        EXPECT_TRUE(Selected("CompPlain")) << "a disabled group still shows what is chosen";

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
        EXPECT_EQ(actions.LastAction(), "SegTestComp"_intern);
        EXPECT_EQ(actions.LastValue(), "icon"_intern);

        Draw();
        EXPECT_FALSE(Selected("CompIcon")) << "the group owns no choice: the click changed nothing";
    }

    TEST_F(UISegmentedTest, ADisabledComposedSegmentTakesTheClickAndEmitsNothing) {
        ActionLog actions;
        Draw();
        ASSERT_NO_FATAL_FAILURE(Click("CompOff"));
        EXPECT_EQ(actions.Total(), 0u);

        ASSERT_NO_FATAL_FAILURE(Click("CompPlain"));
        EXPECT_EQ(actions.Total(), 1u) << "the segment next to it still works";
    }

    TEST_F(UISegmentedTest, ComposedSegmentsBesideATextAreChosenEachByItsOwnValue) {
        SetVar("comp", "plain");
        SetVar("inner", "plain");
        Draw();
        EXPECT_TRUE(Selected("FormsButton"));
        EXPECT_FALSE(Selected("FormsComposed"));
        EXPECT_EQ(Require(Element("FormsCaption").GetComponent<UITextComponent>()).text, "Form") << "the label is no segment";

        SetVar("comp", "icon");
        Draw();
        EXPECT_FALSE(Selected("FormsButton"));
        EXPECT_TRUE(Selected("FormsComposed"));

        ActionLog actions;
        ASSERT_NO_FATAL_FAILURE(Click("FormsComposed"));
        EXPECT_EQ(actions.LastAction(), "SegTestForms"_intern);
        EXPECT_EQ(actions.LastValue(), "icon"_intern);
        ASSERT_NO_FATAL_FAILURE(Click("FormsButton"));
        EXPECT_EQ(actions.LastAction(), "SegTestForms"_intern);
        EXPECT_EQ(actions.LastValue(), "plain"_intern);
        EXPECT_EQ(actions.Total(), 2u);
    }

    TEST_F(UISegmentedTest, ANestedGroupOfComposedSegmentsManagesItsOwnChildren) {
        SetVar("comp", "plain");
        SetVar("inner", "plain");
        Draw();
        EXPECT_TRUE(Selected("FormsInnerOne")) << "its variable is `inner`";
        EXPECT_FALSE(Selected("FormsInner")) << "a group is no segment of the outer one";

        SetVar("inner", "other");
        Get<UISegmented>("Forms").disabled = true;
        Draw();
        EXPECT_FALSE(Selected("FormsInnerOne"));
        EXPECT_FALSE(Element("FormsInnerOne").groupDisabled) << "the outer flag reaches its own segments only";
        EXPECT_TRUE(Element("FormsButton").groupDisabled);
        EXPECT_TRUE(Element("FormsComposed").groupDisabled);
    }

    static_assert(std::is_base_of_v<UIStack, UISegmented>);

    TEST_F(UISegmentedTest, CompileTimeChecks) {}

}  // namespace BECore::Tests
