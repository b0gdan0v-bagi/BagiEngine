#include <BECore/Control/ControlActivity.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/ControlTouches.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIControl.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIVars.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/UIInputEvents.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-2f;

        // "Main": output 800 x 600, every canvas in window pixels (scale 1).
        //   Hud (layer 10): Plate (10, 10, 200 x 100) holds Title (a label with a variable) and Ok (a button, 122, 62, 80 x 40); Off (300, 200, 80 x 40) is a disabled
        //   button; Under (500, 300, 80 x 40) is a button with Cover (490, 290, 100 x 60) drawn over it; Note (300, 300, 80 x 40) is a label.
        //   Hotspot (600, 400, 77 x 40) is an interactive element with no clicker.
        //   Debug (layer 5): Tiny (400, 10, 20 x 20) is a button below the touch size, and a second Ok (500, 100, 80 x 40) with another action.
        //   Menu (layer 20): a modal canvas, hidden at first.
        // "Fit": 400 x 300 units, shown in a 1000 x 600 output: scale 2, offset (100, 0). View is a SceneView canvas with the button Go (10, 10, 50 x 40) and the slider Slide (10, 100, 200 x 40,
        //   range 0..100).
        // "Choice": 800 x 600 in window pixels. Panel holds Side, a UISegmented bound to `uiApiSide` with the buttons Left and Right (the same action, values "left" and "right";
        //   150 x 56 each at x = 100, 252, y = 100), and Plain (100, 300, 80 x 40) is a button without a value.
        // "Values": 800 x 600 in window pixels. Loading is a UIProgressBar (100, 300, 400 x 24, `uiApiLoad`, range 0..100); CSound a
        //   toggle row (100, 500, 300 x 56; `uiApiCSound`, values "on" / "off", the caption child CSoundCaption and the switch child CSoundSwitch), CLocked a disabled one (520, 300, 240 x 56),
        //   CSilent one without an action (520, 400, 240 x 56), CVolume a slider (450, 100, 340 x 56; `uiApiCVolume`, range 0..1, steps of 0.1) and CQuiet a disabled one (450, 500, 300 x 56).
        // "Pick": 800 x 600 in window pixels. Level is a UIDropdown (100, 100, 240 x 56, `uiApiLevel`, action UIControlApiLevel, options 56 high: Easy, Normal, Hard and Locked, a
        //   disabled option), Below (500, 200, 80 x 40) is a button the open list does not cover, PickToggle (500, 300, 200 x 56) a composed toggle and PickSlide (500, 400, 200 x 56) a composed
        //   slider, Frozen (100, 450, 240 x 56) a disabled dropdown with one option.
        // "Icons": 800 x 600 in window pixels. Gear is an icon button with a tooltip that holds a key and a variable, Quiet an icon button without a tooltip, Words a button with a caption only.
        // "Composed": 800 x 600 in window pixels, buttons made of components. Play (10, 10, 120 x 48) has a clicker, an image, a text and a tooltip, Dead (150, 10, 100 x 48) a disabled clicker and
        //   a text, Shade (10, 100, 100 x 40) a clicker with Cover (0, 90, 120 x 60) drawn over it, Wide (10, 200, 300 x 40) a clicker, Badge (300, 10, 48 x 48) an image and a tooltip but no clicker,
        //   Bare (400, 10, 48 x 48) no components.
        //   Buttons with children (y = 300, 48 high): Pad (10, 60 wide) a clicker with the icon child PadIcon, Mix (100, 160 wide) a clicker with a tooltip whose icon MixIcon and caption MixCaption
        //   are in a plain Row, Hollow (300, 100 wide) a clicker with nothing below it, Outer (450, 160 wide) a clicker whose only child Inner is a clicker with a text and an icon.
        // "Choices": 800 x 600 in window pixels, choice groups made of components. CSide (100, 100, 404 x 56, `uiApiCSide`) has the segments CLeft (a clicker and a text), CRight (a clicker, an
        //   icon child CRightIcon and a caption child CRightCaption) and CGone (a disabled clicker); Mix (100, 200, 304 x 56, `uiApiMix`) holds a segment with a text and a clicker and one with the
        //   same parts under other names; CLevel (100, 300, 240 x 56, `uiApiCLevel`, action UIControlApiCLevel) is a dropdown of the composed options CEasy, CHard (icon child and caption child) and
        //   CLocked (a disabled clicker); CBelow (500, 500, 80 x 40) is a button the open list does not cover.
        // "Targets": 800 x 600 in window pixels. A canvas (layer 10) with the buttons Press (100, 100, 80 x 40) and Covered (450, 120, 80 x 40) between two scene targets: Back (layer 5,
        //   0, 0, 300 x 300) lies below the canvas and Press is on it; Front (layer 15, 480, 100, 200 x 100) lies above the canvas and Covered is under it.
        // "Empty": a scene without canvases.
        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIPanel" name="Plate" x="10" y="10" width="200" height="100" color="10,20,30,200">
            <children>
              <element type="UIElement" name="Title" x="8" y="8" width="184" height="30" fontSize="24">
                <components>
                  <component type="UITextComponent" text="Score: {score}"/>
                </components>
              </element>
              <element type="UIElement" name="Ok" anchor="BottomRight" x="-8" y="-8" width="80" height="40" color="40,90,60,255">
                <components>
                  <component type="UITextComponent" text="OK" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="UIControlApiOk"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Off" x="300" y="200" width="80" height="40" color="40,90,60,255">
            <components>
              <component type="UITextComponent" text="Off" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="UIControlApiOff" disabled="true"/>
            </components>
          </element>
          <element type="UIElement" name="Note" x="300" y="300" width="80" height="40">
            <components>
              <component type="UITextComponent" text="Note"/>
            </components>
          </element>
          <element type="UIElement" name="Under" x="500" y="300" width="80" height="40" color="40,90,60,255">
            <components>
              <component type="UITextComponent" text="Under" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="UIControlApiUnder"/>
            </components>
          </element>
          <element type="UIPanel" name="Cover" x="490" y="290" width="100" height="60" blocksInput="true"/>
          <element type="UIElement" name="Hotspot" x="600" y="400" width="77" height="40" interactive="true"/>
        </elements>
      </component>
    </components></node>
    <node name="Debug" layer="5"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Tiny" x="400" y="10" width="20" height="20" color="40,90,60,255">
            <components>
              <component type="UITextComponent" text="T" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="UIControlApiTiny"/>
            </components>
          </element>
          <element type="UIElement" name="Ok" x="500" y="100" width="80" height="40" color="40,90,60,255">
            <components>
              <component type="UITextComponent" text="OK" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="UIControlApiDebugOk"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
    <node name="Menu" layer="20"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize" modal="true" visible="false"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Fit" viewWidth="400" viewHeight="300"><nodes>
    <node name="View"><components>
      <component type="UICanvasComponent" space="SceneView">
        <elements>
          <element type="UIElement" name="Go" x="10" y="10" width="50" height="40" color="40,90,60,255">
            <components>
              <component type="UITextComponent" text="Go" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="UIControlApiGo"/>
            </components>
          </element>
          <element type="UIElement" name="Slide" x="10" y="100" width="200" height="40">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" action="UIControlApiSlide" minValue="0" maxValue="100"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Views" viewWidth="400" viewHeight="300">
    <viewVariants><view name="Portrait" when="orientation:portrait" width="300" height="400"/></viewVariants>
    <nodes>
      <node name="View"><components>
        <component type="UICanvasComponent" space="SceneView">
          <elements>
            <element type="UIElement" name="Go" x="10" y="10" width="50" height="40" color="40,90,60,255">
              <components>
                <component type="UITextComponent" text="Go" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="UIControlApiViewGo"/>
              </components>
            </element>
          </elements>
          <variants>
            <variant when="orientation:portrait">
              <overrides><set element="Go" anchor="Bottom" x="0" y="-10"/></overrides>
            </variant>
          </variants>
        </component>
      </components></node>
    </nodes>
  </scene>
  <scene type="Scene" name="Choice"><nodes>
    <node name="Panel"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UISegmented" name="Side" bind="uiApiSide" x="100" y="100" width="302" height="56" direction="Horizontal" spacing="2" color="30,30,30,255">
            <children>
              <element type="UIElement" name="Left" flex="1" height="56" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Left" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="UIControlApiSide" value="left"/>
                </components>
              </element>
              <element type="UIElement" name="Right" flex="1" height="56" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Right" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="UIControlApiSide" value="right"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Plain" x="100" y="300" width="80" height="40" color="40,90,60,255">
            <components>
              <component type="UITextComponent" text="Plain" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="UIControlApiPlain"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Values"><nodes>
    <node name="Settings"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIProgressBar" name="Loading" bind="uiApiLoad" minValue="0" maxValue="100" x="100" y="300" width="400" height="24"/>
          <element type="UIElement" name="CSound" x="100" y="500" width="300" height="56" color="30,30,30,255" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
            <components>
              <component type="UIInputClicker"/>
              <component type="UIToggleComponent" bind="uiApiCSound" action="UIControlApiCSound" onValue="on" offValue="off"/>
            </components>
            <children>
              <element type="UIElement" name="CSoundCaption" flex="1" height="100%">
                <components><component type="UITextComponent" text="Sound" verticalAlign="Center" wrap="false"/></components>
              </element>
              <element type="UIElement" name="CSoundSwitch" height="100%" aspectRatio="1.25">
                <components><component type="UISwitchComponent"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="CLocked" x="520" y="300" width="240" height="56" color="30,30,30,255" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
            <components>
              <component type="UIInputClicker" disabled="true"/>
              <component type="UIToggleComponent" bind="uiApiCLocked" action="UIControlApiCLocked"/>
            </components>
            <children>
              <element type="UIElement" name="CLockedCaption" flex="1" height="100%">
                <components><component type="UITextComponent" text="Locked" verticalAlign="Center" wrap="false"/></components>
              </element>
              <element type="UIElement" name="CLockedSwitch" height="100%" aspectRatio="1.25">
                <components><component type="UISwitchComponent"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="CSilent" x="520" y="400" width="240" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UIToggleComponent" bind="uiApiCSilent"/>
            </components>
          </element>
          <element type="UIElement" name="CVolume" x="450" y="100" width="340" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" bind="uiApiCVolume" action="UIControlApiCVolume" minValue="0" maxValue="1" step="0.1"/>
            </components>
          </element>
          <element type="UIElement" name="CQuiet" x="450" y="500" width="300" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false" disabled="true"/>
              <component type="UISliderComponent" bind="uiApiCQuiet" action="UIControlApiCQuiet"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Pick"><nodes>
    <node name="Panel"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIDropdown" name="Level" bind="uiApiLevel" action="UIControlApiLevel" x="100" y="100" width="240" height="56" optionHeight="56" color="30,30,30,255">
            <components><component type="UITextComponent" text="Pick" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
            <children>
              <element type="UIElement" name="Easy" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Easy" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="easy"/>
                </components>
              </element>
              <element type="UIElement" name="Normal" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Normal" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="normal"/>
                </components>
              </element>
              <element type="UIElement" name="Hard" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Hard" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="hard"/>
                </components>
              </element>
              <element type="UIElement" name="Locked" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Locked" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="locked" disabled="true"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Below" x="500" y="200" width="80" height="40" color="40,90,60,255">
            <components>
              <component type="UITextComponent" text="Below" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="UIControlApiBelow"/>
            </components>
          </element>
          <element type="UIElement" name="PickToggle" x="500" y="300" width="200" height="56" color="30,30,30,255" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
            <components>
              <component type="UIInputClicker"/>
              <component type="UIToggleComponent" action="UIControlApiPickToggle"/>
            </components>
            <children>
              <element type="UIElement" name="PickToggleCaption" flex="1" height="100%">
                <components><component type="UITextComponent" text="Toggle" verticalAlign="Center" wrap="false"/></components>
              </element>
              <element type="UIElement" name="PickToggleSwitch" height="100%" aspectRatio="1.25">
                <components><component type="UISwitchComponent"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="PickSlide" x="500" y="400" width="200" height="56">
            <components>
              <component type="UIInputClicker" stateLook="false"/>
              <component type="UISliderComponent" action="UIControlApiPickSlide"/>
            </components>
          </element>
          <element type="UIDropdown" name="Frozen" bind="uiApiFrozen" action="UIControlApiFrozen" disabled="true" x="100" y="450" width="240" height="56" optionHeight="56" color="30,30,30,255">
            <components><component type="UITextComponent" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
            <children>
              <element type="UIElement" name="Only" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Only" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="only"/>
                </components>
              </element>
            </children>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Choices"><nodes>
    <node name="Panel"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UISegmented" name="CSide" bind="uiApiCSide" x="100" y="100" width="404" height="56" direction="Horizontal" spacing="4">
            <children>
              <element type="UIElement" name="CLeft" flex="1" height="56" color="70,70,100,255" fontSize="24">
                <components>
                  <component type="UITextComponent" text="Left" align="Center" verticalAlign="Center"/>
                  <component type="UIInputClicker" action="UIControlApiCSide" value="left"/>
                </components>
              </element>
              <element type="UIElement" name="CRight" flex="1" height="56" color="70,70,100,255" layout="Stack" direction="Horizontal" spacing="6" crossAlign="Center">
                <components>
                  <component type="UIInputClicker" action="UIControlApiCSide" value="right"/>
                  <component type="UITooltipComponent" text="The right one"/>
                </components>
                <children>
                  <element type="UIElement" name="CRightIcon" width="24" height="24"><components><component type="UIImageComponent" sprite="SideIcon"/></components></element>
                  <element type="UIElement" name="CRightCaption" flex="1" height="100%" fontSize="24">
                    <components><component type="UITextComponent" text="Right" align="Center" verticalAlign="Center"/></components>
                  </element>
                </children>
              </element>
              <element type="UIElement" name="CGone" flex="1" height="56" color="70,70,100,255" fontSize="24">
                <components>
                  <component type="UITextComponent" text="Gone" align="Center" verticalAlign="Center"/>
                  <component type="UIInputClicker" action="UIControlApiCSide" value="gone" disabled="true"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UISegmented" name="Mix" bind="uiApiMix" x="100" y="200" width="304" height="56" direction="Horizontal" spacing="4">
            <children>
              <element type="UIElement" name="MixButton" flex="1" height="56" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Button" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="UIControlApiMix" value="button"/>
                </components>
              </element>
              <element type="UIElement" name="MixComposed" flex="1" height="56" color="70,70,100,255" fontSize="24">
                <components>
                  <component type="UITextComponent" text="Composed" align="Center" verticalAlign="Center"/>
                  <component type="UIInputClicker" action="UIControlApiMix" value="composed"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIDropdown" name="CLevel" bind="uiApiCLevel" action="UIControlApiCLevel" x="100" y="300" width="240" height="56" optionHeight="56" color="30,30,30,255">
            <components><component type="UITextComponent" text="Pick" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
            <children>
              <element type="UIElement" name="CEasy" color="70,70,100,255" fontSize="24">
                <components>
                  <component type="UITextComponent" text="Easy" align="Center" verticalAlign="Center"/>
                  <component type="UIInputClicker" value="easy"/>
                </components>
              </element>
              <element type="UIElement" name="CHard" color="70,70,100,255" layout="Stack" direction="Horizontal" spacing="6" crossAlign="Center">
                <components><component type="UIInputClicker" value="hard"/></components>
                <children>
                  <element type="UIElement" name="CHardIcon" width="24" height="24"><components><component type="UIImageComponent" sprite="HardIcon"/></components></element>
                  <element type="UIElement" name="CHardCaption" flex="1" height="100%" fontSize="24">
                    <components><component type="UITextComponent" text="Hard" align="Center" verticalAlign="Center"/></components>
                  </element>
                </children>
              </element>
              <element type="UIElement" name="CLocked" color="70,70,100,255" fontSize="24">
                <components>
                  <component type="UITextComponent" text="Locked" align="Center" verticalAlign="Center"/>
                  <component type="UIInputClicker" value="locked" disabled="true"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="CBelow" x="500" y="500" width="80" height="40" color="40,90,60,255">
            <components>
              <component type="UITextComponent" text="Below" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="UIControlApiCBelow"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Boxes"><nodes>
    <node name="Panel"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIPanel" name="Flat" x="10" y="10" width="100" height="50" color="10,20,30,255"/>
          <element type="UIPanel" name="Fancy" x="10" y="100" width="200" height="100" color="10,20,30,255" cornerRadius="16" borderWidth="2" borderColor="255,255,255,255"
                   gradientDirection="Vertical" gradientColor="50,60,70,255" shadow="0,4,8,0,#00000080" glow="0,0,12,2,#FFD70040" highlight="255,255,255,60"/>
          <element type="UIElement" name="Round" x="10" y="300" width="80" height="40" color="40,90,60,255" cornerRadius="4,8,12,0">
            <components>
              <component type="UITextComponent" text="Round" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="UIControlApiRound"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Icons"><nodes>
    <node name="Bar"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Gear" x="10" y="10" width="48" height="48" color="40,90,60,255">
            <components>
              <component type="UIInputClicker" action="UIControlApiGear"/>
              <component type="UIImageComponent" sprite="GearIcon"/>
              <component type="UITooltipComponent" text="Open {#tip.gear} for {user}"/>
            </components>
          </element>
          <element type="UIElement" name="Quiet" x="70" y="10" width="48" height="48" color="40,90,60,255">
            <components>
              <component type="UIInputClicker" action="UIControlApiQuiet"/>
              <component type="UIImageComponent" sprite="GearIcon"/>
            </components>
          </element>
          <element type="UIElement" name="Words" x="130" y="10" width="80" height="48" color="40,90,60,255">
            <components>
              <component type="UITextComponent" text="Words" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="UIControlApiWords"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Composed"><nodes>
    <node name="Bar"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Play" x="10" y="10" width="120" height="48" color="40,90,60,255" fontSize="24">
            <components>
              <component type="UIInputClicker" action="UIControlApiPlay" value="easy"/>
              <component type="UIImageComponent" sprite="PlayIcon"/>
              <component type="UITextComponent" text="Play {user}"/>
              <component type="UITooltipComponent" text="Start {#tip.play}"/>
            </components>
          </element>
          <element type="UIElement" name="Dead" x="150" y="10" width="100" height="48" color="40,90,60,255" fontSize="24">
            <components>
              <component type="UIInputClicker" action="UIControlApiDead" value="v" disabled="true"/>
              <component type="UITextComponent" text="Dead"/>
            </components>
          </element>
          <element type="UIElement" name="Shade" x="10" y="100" width="100" height="40" color="40,90,60,255">
            <components><component type="UIInputClicker" action="UIControlApiShade"/></components>
          </element>
          <element type="UIPanel" name="Cover" x="0" y="90" width="120" height="60" blocksInput="true"/>
          <element type="UIElement" name="Wide" x="10" y="200" width="300" height="40" color="40,90,60,255">
            <components><component type="UIInputClicker" action="UIControlApiWide" value="wide"/></components>
          </element>
          <element type="UIElement" name="Badge" x="300" y="10" width="48" height="48">
            <components><component type="UIImageComponent" sprite="BadgeIcon"/><component type="UITooltipComponent" text="Badge"/></components>
          </element>
          <element type="UIElement" name="Bare" x="400" y="10" width="48" height="48" color="40,90,60,255"/>
          <element type="UIElement" name="Pad" x="10" y="300" width="60" height="48" color="40,90,60,255">
            <components><component type="UIInputClicker" action="UIControlApiPad"/></components>
            <children>
              <element type="UIElement" name="PadIcon" anchor="Center" width="60%" height="60%">
                <components><component type="UIImageComponent" sprite="PadSprite"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Mix" x="100" y="300" width="160" height="48" color="40,90,60,255">
            <components><component type="UIInputClicker" action="UIControlApiMix" value="m"/><component type="UITooltipComponent" text="Mix tip"/></components>
            <children>
              <element type="UIElement" name="Row" width="100%" height="100%">
                <children>
                  <element type="UIElement" name="MixIcon" x="4" y="4" width="40" height="40">
                    <components><component type="UIImageComponent" sprite="MixSprite"/></components>
                  </element>
                  <element type="UIElement" name="MixCaption" x="50" y="4" width="100" height="40">
                    <components><component type="UITextComponent" text="Mix {user}"/></components>
                  </element>
                </children>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Hollow" x="300" y="300" width="100" height="48" color="40,90,60,255">
            <components><component type="UIInputClicker" action="UIControlApiHollow"/></components>
          </element>
          <element type="UIElement" name="Outer" x="450" y="300" width="160" height="48" color="40,90,60,255">
            <components><component type="UIInputClicker" action="UIControlApiOuter"/></components>
            <children>
              <element type="UIElement" name="Inner" x="4" y="4" width="100" height="40">
                <components>
                  <component type="UIInputClicker" action="UIControlApiInner"/>
                  <component type="UITextComponent" text="Inner"/>
                  <component type="UIImageComponent" sprite="InnerSprite"/>
                </components>
              </element>
            </children>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Targets"><nodes>
    <node name="Back" layer="5"><components>
      <component type="TransformComponent" x="0" y="0" width="300" height="300"/>
      <component type="ClickableComponent" actionName="UIControlApiBack"/>
    </components></node>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Press" x="100" y="100" width="80" height="40" color="40,90,60,255">
            <components>
              <component type="UIInputClicker" action="UIControlApiPress"/>
            </components>
          </element>
          <element type="UIElement" name="Covered" x="450" y="120" width="80" height="40" color="40,90,60,255">
            <components>
              <component type="UIInputClicker" action="UIControlApiCovered"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
    <node name="Front" layer="15"><components>
      <component type="TransformComponent" x="480" y="100" width="200" height="100"/>
      <component type="ClickableComponent" actionName="UIControlApiFront"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Empty"><nodes/></scene>
</scenes></SceneManager>)";

        /// Counts the actions the buttons emit and the mouse events that reach game code
        class Recorder : public SubscriptionHolder {
        public:
            Recorder() {
                Subscribe<InputEvents::ActionEvent, &Recorder::OnAction>(this);
                Subscribe<SDLEvents::MouseButtonDownEvent, &Recorder::OnDown>(this);
                Subscribe<SDLEvents::MouseButtonUpEvent, &Recorder::OnUp>(this);
            }

            int Count(const char* action) const {
                return static_cast<int>(std::count(_actions.begin(), _actions.end(), PoolString::Intern(action)));
            }
            size_t Total() const {
                return _actions.size();
            }
            /// The value of the last action ("<none>" when there was none)
            String LastValue() const {
                return _values.empty() ? String("<none>") : String(_values.back().ToStringView().data(), _values.back().ToStringView().size());
            }

            int downs = 0;
            int ups = 0;

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                _actions.push_back(event.action);
                _values.push_back(event.value);
            }
            void OnDown(const SDLEvents::MouseButtonDownEvent&) {
                ++downs;
            }
            void OnUp(const SDLEvents::MouseButtonUpEvent&) {
                ++ups;
            }

            Vector<PoolString> _actions;
            Vector<PoolString> _values;
        };

        /// The text of a number as the writer makes it (`1.0` stays a double, which `Format` would make `1`)
        String NumberText(double value) {
            String text;
            JsonWriter writer(text);
            writer.Value(value);
            return text;
        }

        String StringMember(JsonView object, StringView key) {
            String text;
            return MemberOf(object, key).ReadText(text) ? text : String("<missing>");
        }

        double NumberMember(JsonView object, StringView key) {
            return MemberOf(object, key).AsDouble().value_or(-12345.0);
        }

        bool BoolMember(JsonView object, StringView key) {
            return BoolOf(object, key).value_or(false);
        }

        JsonView Member(JsonView object, StringView key) {
            return MemberOf(object, key);
        }

        void ExpectRect(JsonView rect, float x, float y, float w, float h) {
            ASSERT_TRUE(rect.IsValid());
            EXPECT_NEAR(NumberMember(rect, "x"), x, kTolerance);
            EXPECT_NEAR(NumberMember(rect, "y"), y, kTolerance);
            EXPECT_NEAR(NumberMember(rect, "w"), w, kTolerance);
            EXPECT_NEAR(NumberMember(rect, "h"), h, kTolerance);
        }

    }  // namespace

    class UIControlApiTest : public ::testing::Test {
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
            ASSERT_TRUE(deserializer.LoadFromString(kScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);
            RegisterUIControl(_service);
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        /// Shows the scene and draws a frame
        void Show(const char* sceneName, float width = 800.0f, float height = 600.0f) {
            _renderer->width = width;
            _renderer->height = height;
            _manager->SetActiveScene(PoolString::Intern(sceneName));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        IntrusivePtr<UICanvasComponent> Canvas(const char* sceneName, const char* nodeName) const {
            for (const auto& node : _manager->GetSceneByName(PoolString::Intern(sceneName))->GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    return node->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        /// Sends a request, runs the frame stage that answers it and returns the whole response
        ControlReply Call(StringView method, StringView paramsText = {}) {
            return CallControlText(_service, ++_nextId, method, paramsText);
        }

        /// The params of a click on @p name; @p more is more members as written (`"at":0.5`)
        static String Click(StringView name, StringView more = {}) {
            return more.empty() ? Format(R"({{"name":"{}"}})", name) : Format(R"({{"name":"{}",{}}})", name, more);
        }

        /// The canvas of the result named after a scene node; not valid when there is none
        static JsonView FindCanvas(JsonView result, StringView node) {
            for (const JsonView canvas : Member(result, "canvases").Items()) {
                if (StringMember(canvas, "node") == node) {
                    return canvas;
                }
            }
            return JsonView();
        }

        /// The rect of the region of that name in the `regions` array of a canvas, not valid when there is none
        static JsonView RegionRect(JsonView canvas, StringView name) {
            for (const JsonView region : Member(canvas, "regions").Items()) {
                if (StringMember(region, "name") == name) {
                    return Member(region, "rect");
                }
            }
            return JsonView();
        }

        /// The element of a canvas, searched through the whole tree
        static JsonView FindElement(JsonView parent, StringView name) {
            for (const JsonView element : Member(parent, Member(parent, "elements").IsValid() ? "elements" : "children").Items()) {
                if (StringMember(element, "name") == name) {
                    return element;
                }
                if (const JsonView inside = FindElement(element, name); inside.IsValid()) {
                    return inside;
                }
            }
            return JsonView();
        }

        ControlService _service;
        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtr<SceneManager> _manager;
        int64_t _nextId = 0;
    };

    TEST_F(UIControlApiTest, TheMethodsAreRegistered) {
        const ControlReply tree = Call("ui_tree");
        EXPECT_NE(tree.ErrorCode(), "unknown_method");

        const ControlReply click = Call("ui_click", Click("Ok"));
        EXPECT_NE(click.ErrorCode(), "unknown_method");
    }

    // ---- ui_tree ----------------------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, ASceneWithoutCanvasesHasAnEmptyList) {
        _manager->SetActiveScene("Empty"_intern);

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView result = response.Result();
        ASSERT_TRUE(result.IsValid());
        EXPECT_EQ(StringMember(result, "scene"), "Empty");
        const JsonView canvases = Member(result, "canvases");
        ASSERT_TRUE(canvases.IsValid());
        EXPECT_TRUE(canvases.IsArray());
        EXPECT_EQ(canvases.Size(), 0u);
    }

    TEST_F(UIControlApiTest, BothMethodsNeedAnActiveScene) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString("<SceneManager><scenes/></SceneManager>"));
        auto bare = New<SceneManager>();
        bare->Deserialize(deserializer);
        bare->Initialize();
        ASSERT_EQ(bare->GetActiveScene(), nullptr);
        EngineEnvironment::UseSceneManager(bare);

        EXPECT_EQ(Call("ui_tree").ErrorCode(), "no_scene");
        EXPECT_EQ(Call("ui_click", Click("Ok")).ErrorCode(), "no_scene");
    }

    TEST_F(UIControlApiTest, ACanvasThatWasNeverDrawnHasItsStructureAndNoRects) {
        _manager->SetActiveScene("Main"_intern);

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView hud = FindCanvas(response.Result(), "Hud");
        ASSERT_TRUE(hud.IsValid());
        EXPECT_FALSE(BoolMember(hud, "laidOut"));
        EXPECT_FALSE(Member(hud, "bounds").IsValid());
        const JsonView ok = FindElement(hud, "Ok");
        ASSERT_TRUE(ok.IsValid());
        EXPECT_EQ(StringMember(ok, "type"), "UIElement");
        EXPECT_FALSE(Member(ok, "rect").IsValid());
        EXPECT_FALSE(Member(ok, "visible").IsValid());
        const JsonView issues = Member(hud, "issues");
        ASSERT_TRUE(issues.IsValid());
        EXPECT_EQ(issues.Size(), 0u);
    }

    TEST_F(UIControlApiTest, ATreeReportsCanvasesElementsAndTheirRects) {
        Show("Main");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView result = response.Result();
        EXPECT_EQ(StringMember(result, "scene"), "Main");
        const JsonView canvases = Member(result, "canvases");
        ASSERT_TRUE(canvases.IsValid());
        EXPECT_EQ(canvases.Size(), 3u);

        const JsonView hud = FindCanvas(result, "Hud");
        ASSERT_TRUE(hud.IsValid());
        EXPECT_EQ(StringMember(hud, "space"), "Screen");
        EXPECT_TRUE(BoolMember(hud, "visible"));
        EXPECT_FALSE(BoolMember(hud, "modal"));
        EXPECT_TRUE(BoolMember(hud, "inputEnabled"));
        EXPECT_TRUE(BoolMember(hud, "takesInput"));
        EXPECT_TRUE(BoolMember(hud, "laidOut"));
        EXPECT_NEAR(NumberMember(hud, "scale"), 1.0, kTolerance);
        ExpectRect(Member(hud, "bounds"), 0.0f, 0.0f, 800.0f, 600.0f);
        ExpectRect(Member(hud, "contentBounds"), 0.0f, 0.0f, 800.0f, 600.0f);

        // Rects are in canvas coordinates, not relative to the parent
        const JsonView plate = FindElement(hud, "Plate");
        ASSERT_TRUE(plate.IsValid());
        EXPECT_EQ(StringMember(plate, "type"), "UIPanel");
        ExpectRect(Member(plate, "rect"), 10.0f, 10.0f, 200.0f, 100.0f);
        const JsonView ok = FindElement(plate, "Ok");
        ASSERT_TRUE(ok.IsValid());
        EXPECT_EQ(StringMember(ok, "type"), "UIElement");
        ExpectRect(Member(ok, "rect"), 122.0f, 62.0f, 80.0f, 40.0f);
        ExpectRect(Member(ok, "outputRect"), 122.0f, 62.0f, 80.0f, 40.0f);
        EXPECT_TRUE(BoolMember(ok, "visible"));
        EXPECT_TRUE(BoolMember(ok, "interactive"));
        EXPECT_EQ(StringMember(ok, "text"), "OK");
        EXPECT_EQ(StringMember(ok, "action"), "UIControlApiOk");
        EXPECT_FALSE(BoolMember(ok, "disabled"));

        const JsonView off = FindElement(hud, "Off");
        ASSERT_TRUE(off.IsValid());
        EXPECT_TRUE(BoolMember(off, "disabled"));

        const JsonView note = FindElement(hud, "Note");
        ASSERT_TRUE(note.IsValid());
        EXPECT_EQ(StringMember(note, "type"), "UIElement");
        EXPECT_EQ(StringMember(note, "text"), "Note") << "a plain text is an element with a text component";
        EXPECT_FALSE(BoolMember(note, "interactive"));
        EXPECT_FALSE(Member(note, "action").IsValid());

        const JsonView menu = FindCanvas(result, "Menu");
        ASSERT_TRUE(menu.IsValid());
        EXPECT_TRUE(BoolMember(menu, "modal"));
        EXPECT_FALSE(BoolMember(menu, "visible"));
        EXPECT_FALSE(BoolMember(menu, "takesInput"));
    }

    TEST_F(UIControlApiTest, AnElementWithNoBoxStyleHasNoBoxKey) {
        Show("Boxes");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView canvas = FindCanvas(response.Result(), "Panel");
        ASSERT_TRUE(canvas.IsValid());
        const JsonView flat = FindElement(canvas, "Flat");
        ASSERT_TRUE(flat.IsValid());
        EXPECT_FALSE(Member(flat, "box").IsValid());
        Show("Main");
        const ControlReply main = Call("ui_tree");
        EXPECT_EQ(String(main.Text()).find("\"box\""), String::npos);
    }

    TEST_F(UIControlApiTest, AnElementWithEveryBoxFieldReportsThemAll) {
        Show("Boxes");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView canvas = FindCanvas(response.Result(), "Panel");
        ASSERT_TRUE(canvas.IsValid());
        const JsonView fancy = FindElement(canvas, "Fancy");
        ASSERT_TRUE(fancy.IsValid());
        const JsonView box = Member(fancy, "box");
        ASSERT_TRUE(box.IsValid());
        const JsonView corners = Member(box, "cornerRadius");
        ASSERT_TRUE(corners.IsValid());
        ASSERT_TRUE(corners.IsArray());
        ASSERT_EQ(corners.Size(), 4u);
        for (const JsonView radius : corners.Items()) {
            EXPECT_NEAR(radius.AsDouble().value_or(-1.0), 16.0, kTolerance);
        }
        EXPECT_NEAR(NumberMember(box, "borderWidth"), 2.0, kTolerance);
        EXPECT_EQ(StringMember(box, "borderColor"), "255,255,255,255");
        const JsonView gradient = Member(box, "gradient");
        ASSERT_TRUE(gradient.IsValid());
        EXPECT_EQ(StringMember(gradient, "direction"), "Vertical");
        EXPECT_EQ(StringMember(gradient, "color"), "50,60,70,255");
        const JsonView shadow = Member(box, "shadow");
        ASSERT_TRUE(shadow.IsValid());
        EXPECT_NEAR(NumberMember(shadow, "offsetX"), 0.0, kTolerance);
        EXPECT_NEAR(NumberMember(shadow, "offsetY"), 4.0, kTolerance);
        EXPECT_NEAR(NumberMember(shadow, "blur"), 8.0, kTolerance);
        EXPECT_NEAR(NumberMember(shadow, "spread"), 0.0, kTolerance);
        EXPECT_EQ(StringMember(shadow, "color"), "0,0,0,128");
        const JsonView glow = Member(box, "glow");
        ASSERT_TRUE(glow.IsValid());
        EXPECT_NEAR(NumberMember(glow, "blur"), 12.0, kTolerance);
        EXPECT_NEAR(NumberMember(glow, "spread"), 2.0, kTolerance);
        EXPECT_EQ(StringMember(glow, "color"), "255,215,0,64");
        EXPECT_EQ(StringMember(box, "highlight"), "255,255,255,60");
    }

    TEST_F(UIControlApiTest, ABoxReportsOnlyWhatIsSetAndABoxOfAButtonIsTheAuthoredOne) {
        Show("Boxes");
        const ControlReply response = Call("ui_tree");
        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView canvas = FindCanvas(response.Result(), "Panel");
        ASSERT_TRUE(canvas.IsValid());
        const JsonView round = FindElement(canvas, "Round");
        ASSERT_TRUE(round.IsValid());
        const JsonView box = Member(round, "box");
        ASSERT_TRUE(box.IsValid());
        const JsonView corners = Member(box, "cornerRadius");
        ASSERT_TRUE(corners.IsValid());
        ASSERT_EQ(corners.Size(), 4u);
        EXPECT_NEAR(ItemAt(corners, 0).AsDouble().value_or(-1.0), 4.0, kTolerance);
        EXPECT_NEAR(ItemAt(corners, 1).AsDouble().value_or(-1.0), 8.0, kTolerance);
        EXPECT_NEAR(ItemAt(corners, 2).AsDouble().value_or(-1.0), 12.0, kTolerance);
        EXPECT_NEAR(ItemAt(corners, 3).AsDouble().value_or(-1.0), 0.0, kTolerance);
        for (const char* key : {"borderWidth", "borderColor", "gradient", "shadow", "glow", "highlight"}) {
            EXPECT_FALSE(Member(box, key).IsValid()) << key;
        }
    }

    TEST_F(UIControlApiTest, ALabelShowsItsTextWithTheVariablesReplaced) {
        CoreManager::GetUIVars().Set("score"_intern, 42);
        Show("Main");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView hud = FindCanvas(response.Result(), "Hud");
        ASSERT_TRUE(hud.IsValid());
        const JsonView title = FindElement(hud, "Title");
        ASSERT_TRUE(title.IsValid());
        EXPECT_EQ(StringMember(title, "text"), "Score: 42");
    }

    TEST_F(UIControlApiTest, ALabelShowsTheStringOfTheCurrentLanguageInTheTree) {
        UIStrings& strings = CoreManager::GetUIStrings();
        strings.Set("en"_intern, "uiApi.title", "Score");
        strings.Set("de"_intern, "uiApi.title", "Punkte");
        CoreManager::GetUIVars().Set("score"_intern, 42);
        Show("Main");
        auto* title = Require(_manager->GetSceneByName("Main"_intern)->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>()).GetRoot().FindByName("Title"_intern);
        ASSERT_NE(title, nullptr);
        Require(title->GetComponent<UITextComponent>()).text = "{#uiApi.title}: {score}";

        const auto shownText = [this]() {
            const ControlReply response = Call("ui_tree");
            EXPECT_TRUE(response.IsOk()) << response.ErrorMessage();
            const JsonView hud = FindCanvas(response.Result(), "Hud");
            const JsonView label = FindElement(hud, "Title");
            return label.IsValid() ? StringMember(label, "text") : String("<no such element>");
        };

        EXPECT_EQ(shownText(), "Score: 42");
        strings.SetLanguage("de"_intern);
        EXPECT_EQ(shownText(), "Punkte: 42") << "the tree reads the text as of now, with no draw in between";
    }

    TEST_F(UIControlApiTest, AHiddenElementIsNotVisible) {
        auto hudCanvas = Canvas("Main", "Hud");
        ASSERT_TRUE(hudCanvas->SetElementVisible("Note"_intern, false));
        Show("Main");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView hud = FindCanvas(response.Result(), "Hud");
        ASSERT_TRUE(hud.IsValid());
        const JsonView note = FindElement(hud, "Note");
        ASSERT_TRUE(note.IsValid());
        EXPECT_FALSE(BoolMember(note, "visible"));
        const JsonView rect = Member(note, "rect");
        ASSERT_TRUE(rect.IsValid());
        EXPECT_TRUE(NumberMember(rect, "w") == 0.0 || NumberMember(rect, "h") == 0.0) << "a hidden element has an empty rect";
    }

    TEST_F(UIControlApiTest, TheValidatorsFindingsAreInTheTree) {
        Show("Main");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView result = response.Result();
        const JsonView hud = FindCanvas(result, "Hud");
        const JsonView debug = FindCanvas(result, "Debug");
        ASSERT_TRUE(hud.IsValid() && debug.IsValid());
        const JsonView hudIssues = Member(hud, "issues");
        ASSERT_TRUE(hudIssues.IsValid());
        EXPECT_EQ(hudIssues.Size(), 0u) << String(hudIssues.Raw()).c_str();

        const JsonView debugIssues = Member(debug, "issues");
        ASSERT_TRUE(debugIssues.IsValid());
        ASSERT_EQ(debugIssues.Size(), 1u) << String(debugIssues.Raw()).c_str();
        const JsonView issue = ItemAt(debugIssues, 0);
        EXPECT_EQ(StringMember(issue, "kind"), "TouchTargetTooSmall");
        EXPECT_EQ(StringMember(issue, "element"), "Tiny");
        ExpectRect(Member(issue, "rect"), 400.0f, 10.0f, 20.0f, 20.0f);
    }

    TEST_F(UIControlApiTest, AButtonTellsItsIconAndItsTooltipSourceOnlyWhenTheyAreSet) {
        CoreManager::GetUIVars().Set("user"_intern, "Ann");
        Show("Icons");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView bar = FindCanvas(response.Result(), "Bar");
        ASSERT_TRUE(bar.IsValid());
        const JsonView gear = FindElement(bar, "Gear");
        ASSERT_TRUE(gear.IsValid());
        EXPECT_EQ(StringMember(gear, "icon"), "GearIcon");
        EXPECT_EQ(StringMember(gear, "tooltip"), "Open {#tip.gear} for {user}") << "the source text, neither strings nor variables expanded";

        const JsonView quiet = FindElement(bar, "Quiet");
        ASSERT_TRUE(quiet.IsValid());
        EXPECT_EQ(StringMember(quiet, "icon"), "GearIcon");
        EXPECT_FALSE(Member(quiet, "tooltip").IsValid());

        const JsonView words = FindElement(bar, "Words");
        ASSERT_TRUE(words.IsValid());
        EXPECT_FALSE(Member(words, "icon").IsValid());
        EXPECT_FALSE(Member(words, "tooltip").IsValid());
        EXPECT_EQ(StringMember(words, "action"), "UIControlApiWords") << "a button without words still names its action";
    }

    TEST_F(UIControlApiTest, ACanvasWithNoRegionsHasAnEmptyArrayAndItsElementsHaveNoRegionMember) {
        Show("Main");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView hud = FindCanvas(response.Result(), "Hud");
        ASSERT_TRUE(hud.IsValid());
        const JsonView regions = Member(hud, "regions");
        ASSERT_TRUE(regions.IsValid());
        EXPECT_TRUE(regions.IsArray());
        EXPECT_EQ(regions.Size(), 0u);
        const JsonView ok = FindElement(hud, "Ok");
        ASSERT_TRUE(ok.IsValid());
        EXPECT_FALSE(Member(ok, "region").IsValid());
    }

    TEST_F(UIControlApiTest, ARegionIsListedWithItsRectAndTheElementThatNamesItShowsTheName) {
        auto hudCanvas = Canvas("Main", "Hud");
        hudCanvas->SetRegion("hud.corner"_intern, {10.0f, 20.0f, 300.0f, 100.0f});
        Require(hudCanvas->GetLayout().root->FindByName("Ok"_intern)).style.rect.region = "hud.corner"_intern;
        hudCanvas->Invalidate();
        Show("Main");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView hud = FindCanvas(response.Result(), "Hud");
        ASSERT_TRUE(hud.IsValid());
        const JsonView regions = Member(hud, "regions");
        ASSERT_TRUE(regions.IsValid());
        EXPECT_EQ(regions.Size(), 1u);
        ExpectRect(RegionRect(hud, "hud.corner"), 10.0f, 20.0f, 300.0f, 100.0f);
        const JsonView ok = FindElement(hud, "Ok");
        ASSERT_TRUE(ok.IsValid());
        EXPECT_EQ(StringMember(ok, "region"), "hud.corner");
        const JsonView issues = Member(hud, "issues");
        ASSERT_TRUE(issues.IsValid());
        for (const JsonView issue : issues.Items()) {
            EXPECT_NE(StringMember(issue, "kind"), "MissingRegion") << String(issues.Raw()).c_str();
        }
    }

    TEST_F(UIControlApiTest, AnUnpublishedRegionIsNamedOnTheElementAndReportedAsMissing) {
        auto hudCanvas = Canvas("Main", "Hud");
        Require(hudCanvas->GetLayout().root->FindByName("Ok"_intern)).style.rect.region = "hud.gone"_intern;
        hudCanvas->Invalidate();
        Show("Main");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView hud = FindCanvas(response.Result(), "Hud");
        ASSERT_TRUE(hud.IsValid());
        EXPECT_EQ(StringMember(FindElement(hud, "Ok"), "region"), "hud.gone");
        const JsonView issues = Member(hud, "issues");
        ASSERT_TRUE(issues.IsValid());
        ASSERT_EQ(issues.Size(), 1u) << String(issues.Raw()).c_str();
        EXPECT_EQ(StringMember(ItemAt(issues, 0), "kind"), "MissingRegion");
        EXPECT_EQ(StringMember(ItemAt(issues, 0), "element"), "Ok");
        EXPECT_EQ(StringMember(ItemAt(issues, 0), "other"), "hud.gone");
    }

    TEST_F(UIControlApiTest, TheRegionsAreAnArraySortedByNameWhateverTheOrderTheyWerePublishedIn) {
        auto hudCanvas = Canvas("Main", "Hud");
        hudCanvas->SetRegion("z.last"_intern, {1.0f, 2.0f, 3.0f, 4.0f});
        hudCanvas->SetRegion("a.first"_intern, {5.0f, 6.0f, 7.0f, 8.0f});
        Show("Main");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView hud = FindCanvas(response.Result(), "Hud");
        ASSERT_TRUE(hud.IsValid());
        const JsonView regions = Member(hud, "regions");
        ASSERT_TRUE(regions.IsValid());
        ASSERT_TRUE(regions.IsArray());
        ASSERT_EQ(regions.Size(), 2u);
        EXPECT_EQ(StringMember(ItemAt(regions, 0), "name"), "a.first");
        EXPECT_EQ(StringMember(ItemAt(regions, 1), "name"), "z.last");
        ExpectRect(Member(ItemAt(regions, 0), "rect"), 5.0f, 6.0f, 7.0f, 8.0f);
    }

    TEST_F(UIControlApiTest, ASlidersNumberIsSliderValueAndAButtonKeepsItsValueAsAString) {
        Show("Fit");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView view = FindCanvas(response.Result(), "View");
        ASSERT_TRUE(view.IsValid());
        const JsonView slider = FindElement(view, "Slide");
        const JsonView number = Member(slider, "sliderValue");
        ASSERT_TRUE(number.IsValid());
        EXPECT_TRUE(number.IsNumber());
        EXPECT_FALSE(Member(slider, "value").IsValid()) << "one key never holds a string and a number";
        const JsonView buttonValue = Member(FindElement(view, "Go"), "value");
        ASSERT_TRUE(buttonValue.IsValid());
        EXPECT_TRUE(buttonValue.IsString());
    }

    TEST_F(UIControlApiTest, ATreeDeeperThanTheCapIsCutAtTheCapAndStillAnswers) {
        constexpr int kLevels = 130;
        auto hudCanvas = Canvas("Main", "Hud");
        IntrusivePtr<UIElement> parent = hudCanvas->GetLayout().root;
        for (int level = 1; level <= kLevels; ++level) {
            parent = parent->AddChild(PoolString::Intern(Format("Deep{}", level)));
        }
        hudCanvas->Invalidate();
        Show("Main");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView hud = FindCanvas(response.Result(), "Hud");
        ASSERT_TRUE(hud.IsValid());
        const JsonView first = FindElement(hud, "Deep1");
        ASSERT_TRUE(first.IsValid());
        int depth = 1;
        JsonView current = first;
        while (true) {
            const JsonView children = Member(current, "children");
            ASSERT_TRUE(children.IsValid());
            if (children.Size() == 0) {
                break;
            }
            current = ItemAt(children, 0);
            ++depth;
        }
        EXPECT_EQ(StringMember(current, "name"), "Deep100") << "the tree is cut at the cap, not before or after it";
        EXPECT_TRUE(BoolMember(current, "truncated"));
        EXPECT_FALSE(Member(first, "truncated").IsValid());
    }

    TEST_F(UIControlApiTest, ALeafAtTheCapIsNotTruncated) {
        constexpr int kLevels = 100;
        auto hudCanvas = Canvas("Main", "Hud");
        IntrusivePtr<UIElement> parent = hudCanvas->GetLayout().root;
        for (int level = 1; level <= kLevels; ++level) {
            parent = parent->AddChild(PoolString::Intern(Format("Deep{}", level)));
        }
        hudCanvas->Invalidate();
        Show("Main");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView hud = FindCanvas(response.Result(), "Hud");
        ASSERT_TRUE(hud.IsValid());
        const JsonView leaf = FindElement(hud, "Deep100");
        ASSERT_TRUE(leaf.IsValid());
        EXPECT_FALSE(Member(leaf, "truncated").IsValid());
    }

    namespace {

        /// A text well past the small-string buffer, so a copy of it takes a block
        constexpr StringView kLongCaption = "a caption that is much longer than twenty-three bytes";

        /// What one `ui_tree` Drain allocated on this thread; the reply keeps nothing, so only the service and the answer are counted
        AllocCount CountTreeAllocations(ControlService& service, int64_t id) {
            size_t replyBytes = 0;
            service.Post(MakeRequestText(id, "ui_tree"), [&replyBytes](const String& text) { replyBytes += text.size(); });
            const AllocCount counted = CountAllocations([&service] { service.Drain(); });
            EXPECT_GT(replyBytes, 0U);
            return counted;
        }

    }  // namespace

    TEST_F(UIControlApiTest, ARepeatedTreeAllocatesNothingPerElement) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        auto hudCanvas = Canvas("Main", "Hud");
        const auto addCaptions = [&](int first, int count) {
            for (int index = first; index < first + count; ++index) {
                auto element = hudCanvas->GetLayout().root->AddChild(PoolString::Intern(Format("Caption{}", index)));
                element->AddComponent<UITextComponent>().text = String(kLongCaption.data(), kLongCaption.size());
            }
            hudCanvas->Invalidate();
            Show("Main");
        };
        int64_t id = 0;
        const auto measure = [&] {
            CountTreeAllocations(_service, ++id);  // the arena grows to the size of this answer once
            return CountTreeAllocations(_service, ++id);
        };

        addCaptions(0, 2);
        const AllocCount small = measure();
        constexpr int kManyCaptions = 300;
        addCaptions(2, kManyCaptions);
        const AllocCount large = measure();

        // What is left is a constant of the service and the validator, not of the number of elements
        EXPECT_EQ(large.count, small.count) << "the elements of the answer take their texts and lists from the arena";
    }

    TEST_F(UIControlApiTest, TheRegionsOfACanvasThatWasNeverDrawnAreAlreadyListed) {
        _manager->SetActiveScene("Main"_intern);
        Canvas("Main", "Hud")->SetRegion("hud.corner"_intern, {1.0f, 2.0f, 3.0f, 4.0f});

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView hud = FindCanvas(response.Result(), "Hud");
        ASSERT_TRUE(hud.IsValid());
        EXPECT_FALSE(BoolMember(hud, "laidOut"));
        ExpectRect(RegionRect(hud, "hud.corner"), 1.0f, 2.0f, 3.0f, 4.0f);
    }

    TEST_F(UIControlApiTest, OutputRectsAreInTheOutputPixelsOfTheCanvasTransform) {
        Show("Fit", 1000.0f, 600.0f);

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView view = FindCanvas(response.Result(), "View");
        ASSERT_TRUE(view.IsValid());
        EXPECT_EQ(StringMember(view, "space"), "SceneView");
        EXPECT_NEAR(NumberMember(view, "scale"), 2.0, kTolerance);
        const JsonView go = FindElement(view, "Go");
        ASSERT_TRUE(go.IsValid());
        ExpectRect(Member(go, "rect"), 10.0f, 10.0f, 50.0f, 40.0f);
        ExpectRect(Member(go, "outputRect"), 120.0f, 20.0f, 100.0f, 80.0f);
    }

    // ---- the choice group ---------------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, ATreeShowsTheValueAndTheSelectionOfAChoiceGroup) {
        CoreManager::GetUIVars().Set("uiApiSide"_intern, "right");
        Show("Choice");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView panel = FindCanvas(response.Result(), "Panel");
        ASSERT_TRUE(panel.IsValid());
        const JsonView side = FindElement(panel, "Side");
        ASSERT_TRUE(side.IsValid());
        EXPECT_EQ(StringMember(side, "type"), "UISegmented");
        EXPECT_EQ(StringMember(side, "bind"), "uiApiSide");
        EXPECT_EQ(StringMember(side, "selectedValue"), "right");
        EXPECT_FALSE(BoolMember(side, "disabled"));
        const JsonView left = FindElement(side, "Left");
        const JsonView right = FindElement(side, "Right");
        ASSERT_TRUE(left.IsValid() && right.IsValid());
        EXPECT_EQ(StringMember(left, "value"), "left");
        EXPECT_EQ(StringMember(right, "value"), "right");
        EXPECT_EQ(StringMember(left, "action"), "UIControlApiSide");
        EXPECT_FALSE(BoolMember(left, "selected"));
        EXPECT_TRUE(BoolMember(right, "selected"));
        ExpectRect(Member(left, "rect"), 100.0f, 100.0f, 150.0f, 56.0f);
        ExpectRect(Member(right, "rect"), 252.0f, 100.0f, 150.0f, 56.0f);
    }

    TEST_F(UIControlApiTest, TheSelectionFollowsTheVariableBetweenTwoTreeCalls) {
        CoreManager::GetUIVars().Set("uiApiSide"_intern, "left");
        Show("Choice");
        const ControlReply first = Call("ui_tree");

        CoreManager::GetUIVars().Set("uiApiSide"_intern, "right");  // no draw in between: the tree must not show the last draw's selection
        const ControlReply second = Call("ui_tree");

        ASSERT_TRUE(first.IsOk() && second.IsOk());
        const JsonView before = FindElement(FindCanvas(first.Result(), "Panel"), "Side");
        const JsonView after = FindElement(FindCanvas(second.Result(), "Panel"), "Side");
        ASSERT_TRUE(before.IsValid() && after.IsValid());
        EXPECT_EQ(StringMember(before, "selectedValue"), "left");
        EXPECT_TRUE(BoolMember(FindElement(before, "Left"), "selected"));
        EXPECT_FALSE(BoolMember(FindElement(before, "Right"), "selected"));
        EXPECT_EQ(StringMember(after, "selectedValue"), "right");
        EXPECT_FALSE(BoolMember(FindElement(after, "Left"), "selected"));
        EXPECT_TRUE(BoolMember(FindElement(after, "Right"), "selected"));
    }

    TEST_F(UIControlApiTest, AnUnsetVariableSelectsNothingAndATreeBeforeTheFirstDrawStillSaysSo) {
        _manager->SetActiveScene("Choice"_intern);

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView side = FindElement(FindCanvas(response.Result(), "Panel"), "Side");
        ASSERT_TRUE(side.IsValid());
        EXPECT_EQ(StringMember(side, "selectedValue"), "");
        EXPECT_FALSE(BoolMember(FindElement(side, "Left"), "selected"));
        EXPECT_FALSE(BoolMember(FindElement(side, "Right"), "selected"));
    }

    TEST_F(UIControlApiTest, ADisabledGroupShowsItsSegmentsAsDisabled) {
        auto* side = Canvas("Choice", "Panel")->GetLayout().root->children[0]->Cast<UISegmented>();
        ASSERT_NE(side, nullptr);
        side->disabled = true;
        Show("Choice");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView group = FindElement(FindCanvas(response.Result(), "Panel"), "Side");
        ASSERT_TRUE(group.IsValid());
        EXPECT_TRUE(BoolMember(group, "disabled"));
        EXPECT_TRUE(BoolMember(FindElement(group, "Left"), "disabled")) << "a click on it does nothing, which is what the field says";
        EXPECT_TRUE(BoolMember(FindElement(group, "Right"), "disabled"));
        EXPECT_FALSE(BoolMember(FindElement(FindCanvas(response.Result(), "Panel"), "Plain"), "disabled"));
    }

    TEST_F(UIControlApiTest, AClickThatFindsItsElementTouchesTheNodeOfTheCanvasAndAMissDoesNot) {
        Show("Main");
        const IntrusivePtr<ControlClient> client = _service.OpenLocalClient("tool");
        const auto click = [&](StringView name) {
            _service.Post(MakeRequestText(++_nextId, "ui_click", Click(name)), [](const String&) {}, client);
            _service.Drain();
        };

        click("Note");  // not interactive
        click("Nothing");
        EXPECT_EQ(_service.Touches().Size(), 0u);

        click("Ok");  // the first canvas in scene order that has it: Hud
        ASSERT_EQ(_service.Touches().Size(), 1u);
        const ControlTouch& touch = _service.Touches().At(0);
        EXPECT_EQ(touch.nodeId, Require(Canvas("Main", "Hud").Get()).GetNode().GetRuntimeId());
        EXPECT_EQ(touch.component, "UICanvasComponent");
        EXPECT_EQ(touch.field, "");
        EXPECT_EQ(touch.client.Get(), client.Get());
        EXPECT_EQ(_service.Activity().At(_service.Activity().Size() - 1).node, touch.nodeId);
    }

    TEST_F(UIControlApiTest, AButtonAboveASceneTargetTakesTheClickAndTheTargetGetsNothing) {
        Show("Targets");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("Press"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        EXPECT_TRUE(BoolMember(clicked.Result(), "consumed"));
        EXPECT_EQ(recorder.Count("UIControlApiPress"), 1);
        EXPECT_EQ(recorder.Count("UIControlApiBack"), 0) << "the target below the canvas shielded by the button took the press";
        EXPECT_EQ(StringMember(clicked.Result(), "takenBy"), "element");
        EXPECT_EQ(StringMember(clicked.Result(), "action"), "UIControlApiPress");
    }

    TEST_F(UIControlApiTest, TextThatNoDoubleCanHoldIsReadByTheTape) {
        Show("Targets");
        Recorder recorder;
        EXPECT_EQ(Call("ui_click", R"({"name":"Press","at":1e999})").ErrorCode(), "invalid_params") << "an `at` past a double is refused, not a parse error";

        const ControlReply clicked = Call("ui_click", R"({"name":"Pre\u0073s"})");
        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        EXPECT_EQ(recorder.Count("UIControlApiPress"), 1) << "an escape in `name` is decoded";
    }

    TEST_F(UIControlApiTest, ASceneTargetAboveAButtonTakesTheClickAsItWouldFromAPerson) {
        Show("Targets");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("Covered"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        EXPECT_TRUE(BoolMember(clicked.Result(), "consumed"));
        EXPECT_EQ(recorder.Count("UIControlApiFront"), 1);
        EXPECT_EQ(recorder.Count("UIControlApiCovered"), 0) << "the button under the target got the click";
        EXPECT_EQ(recorder.downs, 0) << "the press did not reach the game";
        EXPECT_EQ(recorder.ups, 0);
        EXPECT_EQ(StringMember(clicked.Result(), "takenBy"), "target");
        EXPECT_EQ(StringMember(clicked.Result(), "action"), "") << "the element emitted nothing, the target did";
        EXPECT_EQ(StringMember(clicked.Result(), "value"), "");
    }

    TEST_F(UIControlApiTest, AClickAtAPointThatNoTargetCoversStillReachesTheButton) {
        Show("Targets");
        Recorder recorder;
        const String params = Click("Covered", R"("at":0.0)");  // the left edge of Covered, which Front does not reach

        const ControlReply clicked = Call("ui_click", params);

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        EXPECT_EQ(recorder.Count("UIControlApiCovered"), 1);
        EXPECT_EQ(recorder.Count("UIControlApiFront"), 0);
        EXPECT_EQ(StringMember(clicked.Result(), "takenBy"), "element");
        EXPECT_EQ(StringMember(clicked.Result(), "action"), "UIControlApiCovered");
    }

    TEST_F(UIControlApiTest, AClickOnASegmentAnswersWithTheActionAndTheValueAndTheListenerGetsThem) {
        CoreManager::GetUIVars().Set("uiApiSide"_intern, "left");
        Show("Choice");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("Right"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        const JsonView result = clicked.Result();
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiSide");
        EXPECT_EQ(StringMember(result, "value"), "right");
        EXPECT_EQ(recorder.Count("UIControlApiSide"), 1);
        EXPECT_EQ(recorder.LastValue(), "right");
    }

    TEST_F(UIControlApiTest, AChoiceCanBeCheckedEndToEnd) {
        CoreManager::GetUIVars().Set("uiApiSide"_intern, "left");
        Show("Choice");
        const ControlReply before = Call("ui_tree");
        ASSERT_TRUE(before.IsOk());
        EXPECT_TRUE(BoolMember(FindElement(FindCanvas(before.Result(), "Panel"), "Left"), "selected"));

        // The click only reports; the game (here the test) answers by setting the variable
        ASSERT_TRUE(Call("ui_click", Click("Right")).IsOk());
        const ControlReply unchanged = Call("ui_tree");
        ASSERT_TRUE(unchanged.IsOk());
        EXPECT_TRUE(BoolMember(FindElement(FindCanvas(unchanged.Result(), "Panel"), "Left"), "selected")) << "a click selects nothing by itself";
        EXPECT_FALSE(BoolMember(FindElement(FindCanvas(unchanged.Result(), "Panel"), "Right"), "selected"));

        CoreManager::GetUIVars().Set("uiApiSide"_intern, "right");
        const ControlReply after = Call("ui_tree");
        ASSERT_TRUE(after.IsOk());
        const JsonView panel = FindCanvas(after.Result(), "Panel");
        ASSERT_TRUE(panel.IsValid());
        EXPECT_FALSE(BoolMember(FindElement(panel, "Left"), "selected"));
        EXPECT_TRUE(BoolMember(FindElement(panel, "Right"), "selected"));
    }

    TEST_F(UIControlApiTest, ADisabledGroupTakesTheClickAndEmitsNothing) {
        auto* side = Canvas("Choice", "Panel")->GetLayout().root->children[0]->Cast<UISegmented>();
        ASSERT_NE(side, nullptr);
        side->disabled = true;
        Show("Choice");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("Left"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, APlainButtonHasAnEmptyValueAndNeverSelectedAndALabelHasNeither) {
        Show("Choice");
        Recorder recorder;

        const ControlReply tree = Call("ui_tree");
        const ControlReply clicked = Call("ui_click", Click("Plain"));

        ASSERT_TRUE(tree.IsOk() && clicked.IsOk());
        const JsonView plain = FindElement(FindCanvas(tree.Result(), "Panel"), "Plain");
        ASSERT_TRUE(plain.IsValid());
        EXPECT_TRUE(Member(plain, "value").IsValid());
        EXPECT_EQ(StringMember(plain, "value"), "");
        EXPECT_FALSE(BoolMember(plain, "selected"));
        EXPECT_FALSE(Member(plain, "bind").IsValid()) << "only a choice group has a bind";
        EXPECT_EQ(StringMember(clicked.Result(), "value"), "");
        EXPECT_EQ(recorder.Count("UIControlApiPlain"), 1);
        EXPECT_EQ(recorder.LastValue(), "");

        Show("Main");
        const ControlReply main = Call("ui_tree");
        const JsonView note = FindElement(FindCanvas(main.Result(), "Hud"), "Note");
        ASSERT_TRUE(note.IsValid());
        EXPECT_FALSE(Member(note, "value").IsValid());
        EXPECT_FALSE(Member(note, "selected").IsValid());
    }

    // ---- the dropdown -----------------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, ATreeShowsAClosedDropdownAndItsOptionsAsHidden) {
        CoreManager::GetUIVars().Set("uiApiLevel"_intern, "normal");
        Show("Pick");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView panel = FindCanvas(response.Result(), "Panel");
        ASSERT_TRUE(panel.IsValid());
        const JsonView level = FindElement(panel, "Level");
        ASSERT_TRUE(level.IsValid());
        EXPECT_EQ(StringMember(level, "type"), "UIDropdown");
        EXPECT_EQ(StringMember(level, "bind"), "uiApiLevel");
        EXPECT_EQ(StringMember(level, "selectedValue"), "normal");
        EXPECT_EQ(StringMember(level, "action"), "UIControlApiLevel");
        EXPECT_EQ(StringMember(level, "text"), "Normal") << "the header shows the chosen option";
        EXPECT_FALSE(BoolMember(level, "open"));
        EXPECT_FALSE(BoolMember(level, "disabled"));
        EXPECT_TRUE(BoolMember(level, "interactive"));
        EXPECT_TRUE(BoolMember(level, "visible"));
        ExpectRect(Member(level, "rect"), 100.0f, 100.0f, 240.0f, 56.0f);
        for (const char* option : {"Easy", "Normal", "Hard", "Locked"}) {
            const JsonView item = FindElement(level, option);
            ASSERT_TRUE(item.IsValid()) << option;
            EXPECT_FALSE(BoolMember(item, "visible")) << option << " is hidden while the list is closed";
        }
        EXPECT_TRUE(BoolMember(FindElement(level, "Normal"), "selected"));
        EXPECT_FALSE(BoolMember(FindElement(level, "Easy"), "selected"));
        EXPECT_TRUE(BoolMember(FindElement(level, "Locked"), "disabled"));
        EXPECT_EQ(StringMember(FindElement(level, "Hard"), "value"), "hard");
        EXPECT_EQ(StringMember(FindElement(level, "Hard"), "action"), "") << "the options keep no action of their own";
        EXPECT_FALSE(Member(FindElement(panel, "Below"), "open").IsValid()) << "only a dropdown has open";
    }

    TEST_F(UIControlApiTest, TheSelectionOfADropdownFollowsTheVariableWithNoDraw) {
        CoreManager::GetUIVars().Set("uiApiLevel"_intern, "easy");
        Show("Pick");
        CoreManager::GetUIVars().Set("uiApiLevel"_intern, "hard");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk());
        const JsonView level = FindElement(FindCanvas(response.Result(), "Panel"), "Level");
        ASSERT_TRUE(level.IsValid());
        EXPECT_EQ(StringMember(level, "selectedValue"), "hard");
        EXPECT_TRUE(BoolMember(FindElement(level, "Hard"), "selected"));
        EXPECT_FALSE(BoolMember(FindElement(level, "Easy"), "selected"));
    }

    TEST_F(UIControlApiTest, AnUnsetDropdownShowsThePlaceholderAndSelectsNothing) {
        Show("Pick");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk());
        const JsonView level = FindElement(FindCanvas(response.Result(), "Panel"), "Level");
        ASSERT_TRUE(level.IsValid());
        EXPECT_EQ(StringMember(level, "selectedValue"), "");
        EXPECT_EQ(StringMember(level, "text"), "Pick");
        EXPECT_FALSE(BoolMember(FindElement(level, "Easy"), "selected"));
    }

    TEST_F(UIControlApiTest, AClickOnTheHeaderOpensTheListAndEmitsNothing) {
        Show("Pick");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("Level"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        const JsonView result = clicked.Result();
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_FALSE(Member(result, "action").IsValid());
        EXPECT_FALSE(Member(result, "value").IsValid());
        EXPECT_EQ(recorder.Total(), 0u);

        // The open list gets its rects at the next draw
        const ControlReply stale = Call("ui_tree");
        EXPECT_TRUE(BoolMember(FindElement(FindCanvas(stale.Result(), "Panel"), "Level"), "open"));
        Show("Pick");
        const ControlReply tree = Call("ui_tree");
        ASSERT_TRUE(tree.IsOk());
        const JsonView level = FindElement(FindCanvas(tree.Result(), "Panel"), "Level");
        ASSERT_TRUE(level.IsValid());
        EXPECT_TRUE(BoolMember(level, "open"));
        for (const char* option : {"Easy", "Normal", "Hard", "Locked"}) {
            EXPECT_TRUE(BoolMember(FindElement(level, option), "visible")) << option;
        }
        ExpectRect(Member(FindElement(level, "Easy"), "rect"), 100.0f, 156.0f, 240.0f, 56.0f);
        ExpectRect(Member(FindElement(level, "Locked"), "rect"), 100.0f, 324.0f, 240.0f, 56.0f);
    }

    TEST_F(UIControlApiTest, AClickOnAnOptionAnswersWithTheActionAndTheValueAndTheListClosesAtTheNextFrame) {
        CoreManager::GetUIVars().Set("uiApiLevel"_intern, "easy");
        Show("Pick");
        ASSERT_TRUE(Call("ui_click", Click("Level")).IsOk());
        Show("Pick");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("Hard"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        const JsonView result = clicked.Result();
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiLevel");
        EXPECT_EQ(StringMember(result, "value"), "hard");
        EXPECT_EQ(recorder.Count("UIControlApiLevel"), 1);
        EXPECT_EQ(recorder.LastValue(), "hard");

        Show("Pick");
        const ControlReply tree = Call("ui_tree");
        const JsonView level = FindElement(FindCanvas(tree.Result(), "Panel"), "Level");
        ASSERT_TRUE(level.IsValid());
        EXPECT_FALSE(BoolMember(level, "open"));
        EXPECT_FALSE(BoolMember(FindElement(level, "Hard"), "visible"));
        EXPECT_EQ(StringMember(level, "selectedValue"), "easy") << "the choice is the game's: a click sets no variable";
        EXPECT_EQ(StringMember(level, "text"), "Easy");

        CoreManager::GetUIVars().Set("uiApiLevel"_intern, "hard");  // the game answers
        const ControlReply after = Call("ui_tree");
        EXPECT_EQ(StringMember(FindElement(FindCanvas(after.Result(), "Panel"), "Level"), "text"), "Hard");
        const ControlReply again = Call("ui_click", Click("Hard"));
        EXPECT_EQ(again.ErrorCode(), "not_clickable") << "the list is closed again";
    }

    TEST_F(UIControlApiTest, AnOptionIsNotClickableWhileTheListIsClosed) {
        Show("Pick");

        const ControlReply response = Call("ui_click", Click("Easy"));

        EXPECT_FALSE(response.IsOk());
        EXPECT_EQ(response.ErrorCode(), "not_clickable");
        EXPECT_NE(response.ErrorMessage().find("hidden"), String::npos) << response.ErrorMessage();
    }

    TEST_F(UIControlApiTest, ADisabledOptionTakesTheClickAndTheListStaysOpen) {
        Show("Pick");
        ASSERT_TRUE(Call("ui_click", Click("Level")).IsOk());
        Show("Pick");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("Locked"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        const JsonView result = clicked.Result();
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "") << "nothing was emitted";
        EXPECT_EQ(StringMember(result, "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
        Show("Pick");
        const ControlReply tree = Call("ui_tree");
        EXPECT_TRUE(BoolMember(FindElement(FindCanvas(tree.Result(), "Panel"), "Level"), "open"));
    }

    TEST_F(UIControlApiTest, AClickElsewhereWhileTheListIsOpenClosesItAndIsConsumed) {
        Show("Pick");
        ASSERT_TRUE(Call("ui_click", Click("Level")).IsOk());
        Show("Pick");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("Below"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        EXPECT_TRUE(BoolMember(clicked.Result(), "consumed"));
        EXPECT_EQ(StringMember(clicked.Result(), "action"), "") << "the answer must not report what the press did not emit";
        EXPECT_EQ(StringMember(clicked.Result(), "value"), "");
        EXPECT_EQ(recorder.Count("UIControlApiBelow"), 0) << "the press only closed the list";
        EXPECT_EQ(recorder.Total(), 0u);
        Show("Pick");
        const ControlReply tree = Call("ui_tree");
        EXPECT_FALSE(BoolMember(FindElement(FindCanvas(tree.Result(), "Panel"), "Level"), "open"));
    }

    TEST_F(UIControlApiTest, ADisabledDropdownTakesTheClickAndNeverOpens) {
        Show("Pick");

        const ControlReply clicked = Call("ui_click", Click("Frozen"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        EXPECT_FALSE(Member(clicked.Result(), "action").IsValid());
        Show("Pick");
        const ControlReply tree = Call("ui_tree");
        const JsonView frozen = FindElement(FindCanvas(tree.Result(), "Panel"), "Frozen");
        ASSERT_TRUE(frozen.IsValid());
        EXPECT_TRUE(BoolMember(frozen, "disabled"));
        EXPECT_FALSE(BoolMember(frozen, "open"));
        EXPECT_FALSE(BoolMember(FindElement(frozen, "Only"), "visible"));
    }

    TEST_F(UIControlApiTest, ADropdownCanBeDrivenEndToEnd) {
        Show("Pick");
        Recorder recorder;

        ASSERT_TRUE(Call("ui_click", Click("Level")).IsOk());
        Show("Pick");  // the "step"
        const ControlReply open = Call("ui_tree");
        ASSERT_TRUE(open.IsOk());
        EXPECT_TRUE(BoolMember(FindElement(FindCanvas(open.Result(), "Panel"), "Normal"), "visible"));
        const ControlReply picked = Call("ui_click", Click("Normal"));
        ASSERT_TRUE(picked.IsOk()) << picked.ErrorMessage();
        EXPECT_EQ(StringMember(picked.Result(), "value"), "normal");
        Show("Pick");

        EXPECT_EQ(recorder.Count("UIControlApiLevel"), 1);
        EXPECT_EQ(recorder.LastValue(), "normal");
    }

    // ---- the progress bar ------------------------------------------------------------------------

    TEST_F(UIControlApiTest, ATreeShowsTheFieldsOfTheProgressBar) {
        CoreManager::GetUIVars().Set("uiApiLoad"_intern, "25");
        Show("Values");

        const ControlReply tree = Call("ui_tree");

        ASSERT_TRUE(tree.IsOk()) << tree.ErrorMessage();
        const JsonView canvas = FindCanvas(tree.Result(), "Settings");
        ASSERT_TRUE(canvas.IsValid());

        const JsonView loading = FindElement(canvas, "Loading");
        ASSERT_TRUE(loading.IsValid());
        EXPECT_EQ(StringMember(loading, "type"), "UIProgressBar");
        EXPECT_EQ(StringMember(loading, "bind"), "uiApiLoad");
        EXPECT_NEAR(NumberMember(loading, "minValue"), 0.0, kTolerance);
        EXPECT_NEAR(NumberMember(loading, "maxValue"), 100.0, kTolerance);
        EXPECT_NEAR(NumberMember(loading, "fraction"), 0.25, kTolerance);
        EXPECT_FALSE(BoolMember(loading, "interactive"));
        EXPECT_FALSE(Member(loading, "text").IsValid());
        EXPECT_FALSE(Member(loading, "action").IsValid());
        EXPECT_FALSE(Member(loading, "checked").IsValid());
    }

    TEST_F(UIControlApiTest, TheFractionOfAProgressBarFollowsTheVariableBetweenTwoTreeCallsWithNoDraw) {
        Show("Values");
        const ControlReply before = Call("ui_tree");
        ASSERT_TRUE(before.IsOk());
        JsonView canvas = FindCanvas(before.Result(), "Settings");
        ASSERT_TRUE(canvas.IsValid());
        EXPECT_NEAR(NumberMember(FindElement(canvas, "Loading"), "fraction"), 0.0, kTolerance);

        CoreManager::GetUIVars().Set("uiApiLoad"_intern, "50");
        const ControlReply after = Call("ui_tree");

        ASSERT_TRUE(after.IsOk());
        canvas = FindCanvas(after.Result(), "Settings");
        EXPECT_NEAR(NumberMember(FindElement(canvas, "Loading"), "fraction"), 0.5, kTolerance);

        CoreManager::GetUIVars().Set("uiApiLoad"_intern, "not a number");
        const ControlReply bad = Call("ui_tree");
        EXPECT_NEAR(NumberMember(FindElement(FindCanvas(bad.Result(), "Settings"), "Loading"), "fraction"), 0.0, kTolerance);
    }

    TEST_F(UIControlApiTest, ASliderWithoutAtIsPressedInTheMiddleAndAnIntegerAtIsAccepted) {
        Show("Values");
        Recorder recorder;

        const ControlReply middle = Call("ui_click", Click("CVolume"));
        ASSERT_TRUE(middle.IsOk()) << middle.ErrorMessage();
        EXPECT_EQ(StringMember(middle.Result(), "value"), "0.5");

        const String one = Click("CVolume", R"("at":1)");  // an integer in the JSON text
        const ControlReply end = Call("ui_click", one);
        ASSERT_TRUE(end.IsOk()) << end.ErrorMessage();
        EXPECT_EQ(StringMember(end.Result(), "value"), "1");
    }

    TEST_F(UIControlApiTest, AClientSetsASliderEndToEnd) {
        Show("Values");
        const String params = Click("CVolume", R"("at":1.0)");
        ASSERT_TRUE(Call("ui_click", params).IsOk());

        const ControlReply unchanged = Call("ui_tree");
        EXPECT_NEAR(NumberMember(FindElement(FindCanvas(unchanged.Result(), "Settings"), "CVolume"), "sliderValue"), 0.0, kTolerance) << "the game has not answered";

        CoreManager::GetUIVars().Set("uiApiCVolume"_intern, "1");
        const ControlReply answered = Call("ui_tree");
        const JsonView volume = FindElement(FindCanvas(answered.Result(), "Settings"), "CVolume");
        ASSERT_TRUE(volume.IsValid());
        EXPECT_NEAR(NumberMember(volume, "sliderValue"), 1.0, kTolerance);
        EXPECT_NEAR(NumberMember(volume, "fraction"), 1.0, kTolerance);
    }

    TEST_F(UIControlApiTest, AnAtOutsideZeroToOneOrNotANumberIsInvalidAndNothingIsPressed) {
        Show("Values");
        Recorder recorder;

        for (const char* json : {"1.5", "-0.1", "\"0.5\"", "null", "true", "[0.5]", "2", "-1"}) {
            const String params = Click("CVolume", Format(R"("at":{})", json));
            const ControlReply response = Call("ui_click", params);

            EXPECT_FALSE(response.IsOk()) << json;
            EXPECT_EQ(response.ErrorCode(), "invalid_params") << json;
            EXPECT_NE(response.ErrorMessage().find("'at'"), String::npos) << json << ": " << response.ErrorMessage();
        }
        // Checked before the scene and the element are looked at
        const String missing = Click("NoSuchElementAtAll", R"("at":3)");
        EXPECT_EQ(Call("ui_click", missing).ErrorCode(), "invalid_params");

        EXPECT_EQ(recorder.Total(), 0u);
        EXPECT_EQ(recorder.downs, 0);
    }

    TEST_F(UIControlApiTest, AtLeavesTheLookupOfAButtonAndTheHitTestWhereThePressLands) {
        Show("Main");
        Recorder recorder;

        for (double at : {0.0, 1.0}) {
            const String params = Click("Ok", Format(R"("at":{},"canvas":"Hud")", NumberText(at)));
            const ControlReply response = Call("ui_click", params);
            ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
            EXPECT_TRUE(BoolMember(response.Result(), "consumed"));
        }
        EXPECT_EQ(recorder.Count("UIControlApiOk"), 2);

        // Under is covered in the middle, and so at every point of its width up to the cover's edge: at 1 (x = 580) is under Cover (490..590) as well
        const String covered = Click("Under", R"("at":1.0)");
        EXPECT_EQ(Call("ui_click", covered).ErrorCode(), "not_clickable");
    }

    // ---- the composed toggle and slider ------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, ATreeShowsTheFieldsOfAComposedToggleAndSliderAsOfTheClasses) {
        CoreManager::GetUIVars().Set("uiApiCSound"_intern, "on");
        CoreManager::GetUIVars().Set("uiApiCVolume"_intern, "0.3");
        Show("Values");

        const ControlReply tree = Call("ui_tree");

        ASSERT_TRUE(tree.IsOk()) << tree.ErrorMessage();
        const JsonView canvas = FindCanvas(tree.Result(), "Settings");
        ASSERT_TRUE(canvas.IsValid());

        const JsonView sound = FindElement(canvas, "CSound");
        ASSERT_TRUE(sound.IsValid());
        EXPECT_EQ(StringMember(sound, "type"), "UIElement");
        EXPECT_EQ(StringMember(sound, "bind"), "uiApiCSound");
        EXPECT_EQ(StringMember(sound, "onValue"), "on");
        EXPECT_EQ(StringMember(sound, "offValue"), "off");
        EXPECT_TRUE(BoolMember(sound, "checked"));
        EXPECT_EQ(StringMember(sound, "action"), "UIControlApiCSound") << "the toggle's action replaces the empty one of the clicker";
        EXPECT_FALSE(BoolMember(sound, "disabled"));
        EXPECT_EQ(StringMember(sound, "text"), "Sound") << "the caption child";
        EXPECT_TRUE(BoolMember(sound, "interactive"));
        ExpectRect(Member(sound, "rect"), 100.0f, 500.0f, 300.0f, 56.0f);
        const JsonView types = Member(sound, "components");
        ASSERT_TRUE(types.IsValid());
        EXPECT_EQ(types.Size(), 2u);

        const JsonView locked = FindElement(canvas, "CLocked");
        ASSERT_TRUE(locked.IsValid());
        EXPECT_TRUE(BoolMember(locked, "disabled"));
        EXPECT_FALSE(BoolMember(locked, "checked"));
        EXPECT_EQ(StringMember(locked, "onValue"), "true") << "the defaults of the component";
        EXPECT_EQ(StringMember(locked, "offValue"), "false");

        const JsonView volume = FindElement(canvas, "CVolume");
        ASSERT_TRUE(volume.IsValid());
        EXPECT_EQ(StringMember(volume, "type"), "UIElement");
        EXPECT_EQ(StringMember(volume, "bind"), "uiApiCVolume");
        EXPECT_NEAR(NumberMember(volume, "minValue"), 0.0, kTolerance);
        EXPECT_NEAR(NumberMember(volume, "maxValue"), 1.0, kTolerance);
        EXPECT_NEAR(NumberMember(volume, "step"), 0.1, kTolerance);
        EXPECT_NEAR(NumberMember(volume, "sliderValue"), 0.3, kTolerance);
        EXPECT_NEAR(NumberMember(volume, "fraction"), 0.3, kTolerance);
        EXPECT_EQ(StringMember(volume, "action"), "UIControlApiCVolume");
        EXPECT_FALSE(BoolMember(volume, "disabled"));
        EXPECT_FALSE(Member(volume, "text").IsValid());
        EXPECT_TRUE(BoolMember(volume, "interactive"));
        EXPECT_TRUE(BoolMember(FindElement(canvas, "CQuiet"), "disabled"));
    }

    TEST_F(UIControlApiTest, TheComposedValuesFollowTheVariablesBetweenTwoTreeCallsWithNoDraw) {
        Show("Values");
        const ControlReply before = Call("ui_tree");
        ASSERT_TRUE(before.IsOk());
        JsonView canvas = FindCanvas(before.Result(), "Settings");
        ASSERT_TRUE(canvas.IsValid());
        EXPECT_FALSE(BoolMember(FindElement(canvas, "CSound"), "checked"));
        EXPECT_NEAR(NumberMember(FindElement(canvas, "CVolume"), "sliderValue"), 0.0, kTolerance);

        CoreManager::GetUIVars().Set("uiApiCSound"_intern, "on");
        CoreManager::GetUIVars().Set("uiApiCVolume"_intern, "0.7");
        const ControlReply after = Call("ui_tree");

        ASSERT_TRUE(after.IsOk());
        canvas = FindCanvas(after.Result(), "Settings");
        EXPECT_TRUE(BoolMember(FindElement(canvas, "CSound"), "checked"));
        EXPECT_NEAR(NumberMember(FindElement(canvas, "CVolume"), "sliderValue"), 0.7, kTolerance);
        EXPECT_NEAR(NumberMember(FindElement(canvas, "CVolume"), "fraction"), 0.7, kTolerance);
    }

    TEST_F(UIControlApiTest, AClickOnAComposedToggleAnswersWithTheValueItAsksForAndTheListenerGetsIt) {
        Show("Values");
        Recorder recorder;

        const ControlReply turnOn = Call("ui_click", Click("CSound"));

        ASSERT_TRUE(turnOn.IsOk()) << turnOn.ErrorMessage();
        EXPECT_EQ(StringMember(turnOn.Result(), "action"), "UIControlApiCSound");
        EXPECT_EQ(StringMember(turnOn.Result(), "value"), "on");
        EXPECT_EQ(recorder.Count("UIControlApiCSound"), 1);
        EXPECT_EQ(recorder.LastValue(), "on");

        CoreManager::GetUIVars().Set("uiApiCSound"_intern, "on");
        const ControlReply turnOff = Call("ui_click", Click("CSound"));

        ASSERT_TRUE(turnOff.IsOk());
        EXPECT_EQ(StringMember(turnOff.Result(), "value"), "off");
        EXPECT_EQ(recorder.Count("UIControlApiCSound"), 2);
        EXPECT_EQ(recorder.LastValue(), "off");

        const ControlReply tree = Call("ui_tree");
        EXPECT_TRUE(BoolMember(FindElement(FindCanvas(tree.Result(), "Settings"), "CSound"), "checked")) << "a click changes nothing by itself";
    }

    TEST_F(UIControlApiTest, TheCaptionAndTheSwitchOfAComposedToggleBothClickTheRow) {
        Show("Values");
        Recorder recorder;

        // The row is the target the control API names; a press on the caption's or the switch's middle lands on the row's clicker and toggles
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(120.0f, 528.0f, true));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(120.0f, 528.0f, false));
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(380.0f, 528.0f, true));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(380.0f, 528.0f, false));

        EXPECT_EQ(recorder.Count("UIControlApiCSound"), 2);
        EXPECT_EQ(recorder.LastValue(), "on");
    }

    TEST_F(UIControlApiTest, ADisabledComposedToggleTakesTheClickAndEmitsNothing) {
        Show("Values");
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("CLocked"));

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        EXPECT_TRUE(BoolMember(response.Result(), "consumed"));
        EXPECT_EQ(StringMember(response.Result(), "action"), "");
        EXPECT_EQ(StringMember(response.Result(), "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AToggleWithoutAnActionAnswersNothing) {
        Show("Values");
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("CSilent"));

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        EXPECT_TRUE(BoolMember(response.Result(), "consumed"));
        EXPECT_EQ(StringMember(response.Result(), "action"), "");
        EXPECT_EQ(StringMember(response.Result(), "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AClickOnAComposedSliderWithAtAsksForTheMinimumTheMiddleAndTheMaximum) {
        Show("Values");
        Recorder recorder;
        const struct {
            double at;
            const char* value;
        } cases[] = {{0.0, "0"}, {0.5, "0.5"}, {1.0, "1"}, {0.25, "0.2"}};

        for (const auto& item : cases) {
            const String params = Click("CVolume", Format(R"("at":{})", NumberText(item.at)));
            const ControlReply response = Call("ui_click", params);

            ASSERT_TRUE(response.IsOk()) << "at " << item.at << ": " << response.ErrorMessage();
            const JsonView result = response.Result();
            EXPECT_TRUE(BoolMember(result, "consumed")) << "at " << item.at;
            EXPECT_EQ(StringMember(result, "action"), "UIControlApiCVolume");
            EXPECT_EQ(StringMember(result, "value"), item.value) << "at " << item.at;
            EXPECT_EQ(recorder.LastValue(), item.value) << "at " << item.at;
        }
        EXPECT_EQ(recorder.Count("UIControlApiCVolume"), 4);
    }

    TEST_F(UIControlApiTest, ADisabledComposedSliderTakesThePressAndAnswersWithNothingEmitted) {
        Show("Values");
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("CQuiet"));

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        EXPECT_TRUE(BoolMember(response.Result(), "consumed"));
        EXPECT_EQ(StringMember(response.Result(), "action"), "");
        EXPECT_EQ(StringMember(response.Result(), "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, ASliderDisabledAfterAPressDoesNotAnswerWithThatPress) {
        Show("Values");
        ASSERT_TRUE(Call("ui_click", Click("CVolume")).IsOk());
        UIElement* volume = nullptr;
        for (const auto& child : Canvas("Values", "Settings")->GetLayout().root->children) {
            if (child->name == "CVolume"_intern) {
                volume = child.Get();
            }
        }
        ASSERT_NE(volume, nullptr);
        Require(volume->GetComponent<UIInputClicker>()).disabled = true;
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("CVolume"));

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        EXPECT_EQ(StringMember(response.Result(), "action"), "");
        EXPECT_EQ(StringMember(response.Result(), "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AClickElsewhereWhileTheListIsOpenAnswersNothingForAComposedToggleAndSlider) {
        for (const char* name : {"PickToggle", "PickSlide"}) {
            Show("Pick");
            ASSERT_TRUE(Call("ui_click", Click("Level")).IsOk());
            Show("Pick");
            Recorder recorder;

            const ControlReply clicked = Call("ui_click", Click(name));

            ASSERT_TRUE(clicked.IsOk()) << name << ": " << clicked.ErrorMessage();
            EXPECT_TRUE(BoolMember(clicked.Result(), "consumed")) << name;
            EXPECT_EQ(StringMember(clicked.Result(), "action"), "") << name;
            EXPECT_EQ(StringMember(clicked.Result(), "value"), "") << name;
            EXPECT_EQ(recorder.Total(), 0u) << name << ": the press only closed the list";
        }
    }

    TEST_F(UIControlApiTest, AProgressBarCannotBeClicked) {
        Show("Values");
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("Loading"));

        EXPECT_EQ(response.ErrorCode(), "not_clickable");
        EXPECT_NE(response.ErrorMessage().find("not interactive"), String::npos) << response.ErrorMessage();
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AButtonAndASegmentAreClickedInTheirMiddleWhenAtIsGiven) {
        Show("Choice");
        Recorder recorder;
        const String params = Click("Right", R"("at":0.5)");

        const ControlReply response = Call("ui_click", params);

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        EXPECT_EQ(StringMember(response.Result(), "action"), "UIControlApiSide");
        EXPECT_EQ(StringMember(response.Result(), "value"), "right");
        EXPECT_EQ(recorder.LastValue(), "right");
        // x of the middle of Right (252..402 -> 327), unchanged from the click without `at`
        EXPECT_NEAR(NumberMember(response.Result(), "x"), 327.0, kTolerance);
    }

    TEST_F(UIControlApiTest, ASliderInASceneViewCanvasIsPressedAtTheScaledPoint) {
        Show("Fit", 1000.0f, 600.0f);  // scale 2, offset (100, 0)
        Recorder recorder;
        const String params = Click("Slide", R"("at":0.5)");

        const ControlReply response = Call("ui_click", params);

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        EXPECT_NEAR(NumberMember(response.Result(), "x"), 320.0, kTolerance);  // canvas x 110
        EXPECT_NEAR(NumberMember(response.Result(), "y"), 240.0, kTolerance);  // canvas y 120
        EXPECT_EQ(StringMember(response.Result(), "value"), "50");
        EXPECT_EQ(recorder.LastValue(), "50");
    }

    // ---- ui_click ---------------------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, AClickPressesAButtonLikeAUserWould) {
        Show("Main");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("Ok"));  // the Hud one: the first of the two named Ok in the scene order

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        const JsonView result = clicked.Result();
        EXPECT_EQ(StringMember(result, "clicked"), "Ok");
        EXPECT_EQ(StringMember(result, "canvas"), "Hud");
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiOk");
        EXPECT_NEAR(NumberMember(result, "x"), 162.0, kTolerance);
        EXPECT_NEAR(NumberMember(result, "y"), 82.0, kTolerance);
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(recorder.Count("UIControlApiOk"), 1);
        EXPECT_EQ(recorder.Total(), 1u);
        EXPECT_EQ(recorder.downs, 0) << "the canvas took the press, so the game did not see it";
        EXPECT_EQ(recorder.ups, 0);
    }

    TEST_F(UIControlApiTest, TheClickPointIsInOutputPixelsOfTheCanvasTransform) {
        Show("Fit", 1000.0f, 600.0f);
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("Go"));

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView result = response.Result();
        EXPECT_NEAR(NumberMember(result, "x"), 170.0, kTolerance);  // (10 + 25) * 2 + 100
        EXPECT_NEAR(NumberMember(result, "y"), 60.0, kTolerance);   // (10 + 20) * 2
        EXPECT_EQ(recorder.Count("UIControlApiGo"), 1);
    }

    TEST_F(UIControlApiTest, AnAnswerHasExactlyTheKeysOfItsKindAndAddsActionAndValueOnlyForAClicker) {
        Show("Main");

        const String plainParams = Click("Hotspot", R"("at":0.1)");  // a point that a float cannot hold exactly
        const ControlReply plain = Call("ui_click", plainParams);
        const ControlReply button = Call("ui_click", Click("Ok"));

        ASSERT_TRUE(plain.IsOk()) << plain.ErrorMessage();
        ASSERT_TRUE(button.IsOk()) << button.ErrorMessage();
        const auto keysOf = [](const ControlReply& response) {
            Vector<String> keys;
            for (const JsonMember item : response.Result().Members()) {
                String key;
                item.key.ReadText(key);
                keys.push_back(key);
            }
            return keys;
        };
        EXPECT_EQ(keysOf(plain), (Vector<String>{"canvas", "clicked", "consumed", "takenBy", "x", "y"}));
        EXPECT_EQ(keysOf(button), (Vector<String>{"action", "canvas", "clicked", "consumed", "takenBy", "value", "x", "y"}));
        const double x = NumberMember(plain.Result(), "x");
        EXPECT_EQ(x, static_cast<double>(static_cast<float>(x))) << "a coordinate is a float widened, as the answer of click_at";
    }

    TEST_F(UIControlApiTest, TheClickAndTheOutputRectFollowTheViewTheSceneSwitchedTo) {
        Show("Views", 800.0f, 600.0f);
        const ControlReply landscape = Call("ui_tree");
        Show("Views", 600.0f, 800.0f);
        const ControlReply portrait = Call("ui_tree");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("Go"));

        ASSERT_TRUE(landscape.IsOk() && portrait.IsOk() && clicked.IsOk());
        const JsonView before = FindElement(FindCanvas(landscape.Result(), "View"), "Go");
        const JsonView after = FindElement(FindCanvas(portrait.Result(), "View"), "Go");
        ASSERT_TRUE(before.IsValid() && after.IsValid());
        ExpectRect(Member(before, "outputRect"), 20.0f, 20.0f, 100.0f, 80.0f);
        ExpectRect(Member(after, "rect"), 125.0f, 350.0f, 50.0f, 40.0f);
        ExpectRect(Member(after, "outputRect"), 250.0f, 700.0f, 100.0f, 80.0f);
        const JsonView result = clicked.Result();
        EXPECT_NEAR(NumberMember(result, "x"), 300.0, kTolerance);
        EXPECT_NEAR(NumberMember(result, "y"), 740.0, kTolerance);
        EXPECT_EQ(recorder.Count("UIControlApiViewGo"), 1);
    }

    TEST_F(UIControlApiTest, ASceneViewCanvasReportsTheActiveViewAndOtherCanvasesReportNone) {
        Show("Views", 800.0f, 600.0f);
        const ControlReply landscape = Call("ui_tree");
        Show("Views", 600.0f, 800.0f);
        const ControlReply portrait = Call("ui_tree");

        ASSERT_TRUE(landscape.IsOk() && portrait.IsOk());
        const JsonView before = Member(FindCanvas(landscape.Result(), "View"), "view");
        const JsonView after = Member(FindCanvas(portrait.Result(), "View"), "view");
        ASSERT_TRUE(before.IsValid() && after.IsValid());
        EXPECT_EQ(StringMember(before, "name"), "");
        EXPECT_NEAR(NumberMember(before, "width"), 400.0, kTolerance);
        EXPECT_NEAR(NumberMember(before, "height"), 300.0, kTolerance);
        EXPECT_EQ(StringMember(after, "name"), "Portrait");
        EXPECT_NEAR(NumberMember(after, "width"), 300.0, kTolerance);
        EXPECT_NEAR(NumberMember(after, "height"), 400.0, kTolerance);

        Show("Main");
        const ControlReply main = Call("ui_tree");
        ASSERT_TRUE(main.IsOk());
        EXPECT_FALSE(Member(FindCanvas(main.Result(), "Hud"), "view").IsValid());
    }

    TEST_F(UIControlApiTest, ASceneWithoutViewVariantsReportsItsOneView) {
        Show("Fit", 1000.0f, 600.0f);

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk());
        const JsonView view = Member(FindCanvas(response.Result(), "View"), "view");
        ASSERT_TRUE(view.IsValid());
        EXPECT_EQ(StringMember(view, "name"), "");
        EXPECT_NEAR(NumberMember(view, "width"), 400.0, kTolerance);
        EXPECT_NEAR(NumberMember(view, "height"), 300.0, kTolerance);
    }

    TEST_F(UIControlApiTest, TheDrawnButtonIsWhereTheTreeSaysInTheActiveView) {
        Show("Views", 600.0f, 800.0f);

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk());
        const JsonView go = FindElement(FindCanvas(response.Result(), "View"), "Go");
        ASSERT_TRUE(go.IsValid());
        const JsonView output = Member(go, "outputRect");
        bool found = false;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Rect && std::abs(call.rect.x - static_cast<float>(NumberMember(output, "x"))) < kTolerance &&
                std::abs(call.rect.y - static_cast<float>(NumberMember(output, "y"))) < kTolerance && std::abs(call.rect.w - static_cast<float>(NumberMember(output, "w"))) < kTolerance) {
                found = true;
            }
        }
        EXPECT_TRUE(found) << "no draw call at the outputRect of Go";
    }

    TEST_F(UIControlApiTest, TheCanvasParameterChoosesTheElementWhenNamesRepeat) {
        Show("Main");
        Recorder recorder;
        const String params = Click("Ok", R"("canvas":"Debug")");

        const ControlReply response = Call("ui_click", params);

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        EXPECT_EQ(StringMember(response.Result(), "canvas"), "Debug");
        EXPECT_EQ(recorder.Count("UIControlApiDebugOk"), 1);
        EXPECT_EQ(recorder.Count("UIControlApiOk"), 0);
    }

    TEST_F(UIControlApiTest, ADisabledButtonTakesTheClickAndDoesNothingWithIt) {
        Show("Main");
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("Off"));

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        EXPECT_TRUE(BoolMember(response.Result(), "consumed"));
        EXPECT_EQ(recorder.Total(), 0u);
        EXPECT_EQ(recorder.downs, 0);
    }

    TEST_F(UIControlApiTest, AModalCanvasAboveTakesTheClick) {
        auto menu = Canvas("Main", "Menu");
        menu->_visible = true;
        Show("Main");
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("Ok"));

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        EXPECT_TRUE(BoolMember(response.Result(), "consumed"));
        EXPECT_EQ(recorder.Total(), 0u) << "the modal canvas took the click, as it would take a user's";
        EXPECT_EQ(recorder.downs, 0);
        EXPECT_EQ(StringMember(response.Result(), "takenBy"), "target") << "a canvas above the element's own is a handler above it";
    }

    TEST_F(UIControlApiTest, AnElementThatDoesNotExistIsNotFoundAndItsNameIsNotInterned) {
        Show("Main");
        constexpr const char* kUnknown = "ui_click_no_such_element_77c1d3";
        ASSERT_FALSE(PoolString::Find(kUnknown).has_value());

        const ControlReply response = Call("ui_click", Click(kUnknown));

        EXPECT_FALSE(response.IsOk());
        EXPECT_EQ(response.ErrorCode(), "not_found");
        EXPECT_FALSE(PoolString::Find(kUnknown).has_value()) << "text from a client must not grow the pool";
    }

    TEST_F(UIControlApiTest, ACanvasNameThatNoNodeHasIsNotFound) {
        Show("Main");
        const String params = Click("Ok", R"("canvas":"ui_click_no_such_node_5a90e2")");

        const ControlReply response = Call("ui_click", params);

        EXPECT_EQ(response.ErrorCode(), "not_found");
        EXPECT_FALSE(PoolString::Find(StringView{"ui_click_no_such_node_5a90e2"}).has_value());
    }

    TEST_F(UIControlApiTest, AnElementThatIsNotInteractiveCannotBeClicked) {
        Show("Main");
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("Note"));

        EXPECT_EQ(response.ErrorCode(), "not_clickable");
        EXPECT_NE(response.ErrorMessage().find("not interactive"), String::npos) << response.ErrorMessage();
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AHiddenButtonCannotBeClicked) {
        auto hud = Canvas("Main", "Hud");
        ASSERT_TRUE(hud->SetElementVisible("Off"_intern, false));
        Show("Main");

        const ControlReply response = Call("ui_click", Click("Off"));

        EXPECT_EQ(response.ErrorCode(), "not_clickable");
        EXPECT_NE(response.ErrorMessage().find("hidden"), String::npos) << response.ErrorMessage();
    }

    TEST_F(UIControlApiTest, AButtonCoveredByAnotherElementCannotBeClicked) {
        Show("Main");
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("Under"));

        EXPECT_EQ(response.ErrorCode(), "not_clickable");
        EXPECT_NE(response.ErrorMessage().find("covered by 'Cover'"), String::npos) << response.ErrorMessage();
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AButtonOfACanvasThatTakesNoInputCannotBeClicked) {
        auto hud = Canvas("Main", "Hud");
        hud->_inputEnabled = false;
        Show("Main");

        const ControlReply response = Call("ui_click", Click("Off"));

        EXPECT_EQ(response.ErrorCode(), "not_clickable");
        EXPECT_NE(response.ErrorMessage().find("canvas 'Hud'"), String::npos) << response.ErrorMessage();
    }

    TEST_F(UIControlApiTest, AButtonBeforeTheFirstDrawAsksForAFrame) {
        _manager->SetActiveScene("Main"_intern);

        const ControlReply response = Call("ui_click", Click("Ok"));

        EXPECT_EQ(response.ErrorCode(), "not_clickable");
        EXPECT_NE(response.ErrorMessage().find("step a frame"), String::npos) << response.ErrorMessage();
    }

    TEST_F(UIControlApiTest, TheClickHasToNameAnElement) {
        Show("Main");

        EXPECT_EQ(Call("ui_click", "{}").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("ui_click", R"({"name":""})").ErrorCode(), "invalid_params");
        EXPECT_EQ(Call("ui_click", R"({"name":7})").ErrorCode(), "invalid_params");

        const String badCanvas = Click("Ok", R"("canvas":7)");
        EXPECT_EQ(Call("ui_click", badCanvas).ErrorCode(), "invalid_params");
    }

    // ---- a button made of components -----------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, AComposedButtonTellsItsComponentsAndTheFieldsOfAButton) {
        CoreManager::GetUIVars().Set("user"_intern, "Ann");
        Show("Composed");
        Require(Canvas("Composed", "Bar")->GetRoot().FindByName("Play"_intern)).selected = true;

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView bar = FindCanvas(response.Result(), "Bar");
        ASSERT_TRUE(bar.IsValid());
        const JsonView play = FindElement(bar, "Play");
        ASSERT_TRUE(play.IsValid());
        EXPECT_EQ(StringMember(play, "type"), "UIElement");
        EXPECT_TRUE(BoolMember(play, "interactive")) << "a clicker makes it one";
        const JsonView components = Member(play, "components");
        ASSERT_TRUE(components.IsValid());
        ASSERT_EQ(components.Size(), 4u);
        EXPECT_TRUE(ItemAt(components, 0).TextEquals("UIInputClicker")) << "in the order of the list";
        EXPECT_TRUE(ItemAt(components, 1).TextEquals("UIImageComponent"));
        EXPECT_TRUE(ItemAt(components, 2).TextEquals("UITextComponent"));
        EXPECT_TRUE(ItemAt(components, 3).TextEquals("UITooltipComponent"));
        EXPECT_EQ(StringMember(play, "text"), "Play Ann") << "as shown, variables replaced";
        EXPECT_EQ(StringMember(play, "action"), "UIControlApiPlay");
        EXPECT_EQ(StringMember(play, "value"), "easy");
        EXPECT_FALSE(BoolMember(play, "disabled"));
        EXPECT_TRUE(BoolMember(play, "selected"));
        EXPECT_EQ(StringMember(play, "icon"), "PlayIcon");
        EXPECT_EQ(StringMember(play, "tooltip"), "Start {#tip.play}") << "the source text";
    }

    TEST_F(UIControlApiTest, AButtonMadeOfChildrenAnswersTheTextAndTheIconOfItsChildren) {
        CoreManager::GetUIVars().Set("user"_intern, "Ann");
        Show("Composed");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView bar = FindCanvas(response.Result(), "Bar");
        ASSERT_TRUE(bar.IsValid());

        const JsonView pad = FindElement(bar, "Pad");
        ASSERT_TRUE(pad.IsValid());
        EXPECT_EQ(StringMember(pad, "icon"), "PadSprite") << "the icon-only form: the icon child";
        EXPECT_FALSE(Member(pad, "text").IsValid());
        EXPECT_EQ(StringMember(pad, "action"), "UIControlApiPad");
        const JsonView padIcon = FindElement(pad, "PadIcon");
        ASSERT_TRUE(padIcon.IsValid()) << "the children are still listed";
        EXPECT_EQ(StringMember(padIcon, "icon"), "PadSprite");
        EXPECT_FALSE(Member(padIcon, "action").IsValid());
        EXPECT_FALSE(BoolMember(padIcon, "interactive"));

        const JsonView mix = FindElement(bar, "Mix");
        ASSERT_TRUE(mix.IsValid());
        EXPECT_EQ(StringMember(mix, "text"), "Mix Ann") << "found below a plain child";
        EXPECT_EQ(StringMember(mix, "icon"), "MixSprite");
        EXPECT_EQ(StringMember(mix, "value"), "m");
        EXPECT_EQ(StringMember(mix, "tooltip"), "Mix tip");
        EXPECT_FALSE(BoolMember(mix, "disabled"));
        EXPECT_EQ(StringMember(FindElement(mix, "MixCaption"), "text"), "Mix Ann");
        EXPECT_EQ(StringMember(FindElement(mix, "MixIcon"), "icon"), "MixSprite");
        EXPECT_FALSE(Member(FindElement(mix, "MixCaption"), "icon").IsValid());
        EXPECT_FALSE(Member(FindElement(mix, "MixIcon"), "text").IsValid());
    }

    TEST_F(UIControlApiTest, AClickerWithNothingBelowItAnswersNeitherTextNorIcon) {
        Show("Composed");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView bar = FindCanvas(response.Result(), "Bar");
        ASSERT_TRUE(bar.IsValid());
        const JsonView hollow = FindElement(bar, "Hollow");
        ASSERT_TRUE(hollow.IsValid());
        EXPECT_EQ(StringMember(hollow, "action"), "UIControlApiHollow");
        EXPECT_FALSE(Member(hollow, "text").IsValid());
        EXPECT_FALSE(Member(hollow, "icon").IsValid());
    }

    TEST_F(UIControlApiTest, ANestedClickersTextAndIconAreNotTakenByItsParent) {
        Show("Composed");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView bar = FindCanvas(response.Result(), "Bar");
        ASSERT_TRUE(bar.IsValid());
        const JsonView outer = FindElement(bar, "Outer");
        ASSERT_TRUE(outer.IsValid());
        EXPECT_FALSE(Member(outer, "text").IsValid());
        EXPECT_FALSE(Member(outer, "icon").IsValid());
        const JsonView inner = FindElement(outer, "Inner");
        ASSERT_TRUE(inner.IsValid());
        EXPECT_EQ(StringMember(inner, "text"), "Inner");
        EXPECT_EQ(StringMember(inner, "icon"), "InnerSprite");
        EXPECT_EQ(StringMember(inner, "action"), "UIControlApiInner");
    }

    TEST_F(UIControlApiTest, AClickOnAButtonMadeOfChildrenFindsTheClickerNotTheIcon) {
        Show("Composed");
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("Mix"));

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView result = response.Result();
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiMix");
        EXPECT_EQ(StringMember(result, "value"), "m");
        EXPECT_EQ(recorder.Count("UIControlApiMix"), 1);

        EXPECT_EQ(Call("ui_click", Click("MixIcon")).ErrorCode(), "not_clickable") << "the icon child is no button";
    }

    TEST_F(UIControlApiTest, AComposedButtonIsDisabledByItsClickerAndByItsGroup) {
        Show("Composed");
        UICanvasComponent& bar = *Canvas("Composed", "Bar");
        Require(bar.GetRoot().FindByName("Wide"_intern)).groupDisabled = true;

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView node = FindCanvas(response.Result(), "Bar");
        ASSERT_TRUE(node.IsValid());
        EXPECT_TRUE(BoolMember(FindElement(node, "Dead"), "disabled"));
        EXPECT_TRUE(BoolMember(FindElement(node, "Wide"), "disabled"));
        EXPECT_FALSE(BoolMember(FindElement(node, "Play"), "disabled"));
    }

    TEST_F(UIControlApiTest, AnElementWithNoClickerTellsOnlyWhatItHas) {
        Show("Composed");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView bar = FindCanvas(response.Result(), "Bar");
        ASSERT_TRUE(bar.IsValid());
        const JsonView badge = FindElement(bar, "Badge");
        ASSERT_TRUE(badge.IsValid());
        EXPECT_FALSE(BoolMember(badge, "interactive"));
        EXPECT_EQ(StringMember(badge, "icon"), "BadgeIcon");
        EXPECT_EQ(StringMember(badge, "tooltip"), "Badge");
        EXPECT_FALSE(Member(badge, "action").IsValid());
        EXPECT_FALSE(Member(badge, "disabled").IsValid());
        EXPECT_FALSE(Member(badge, "selected").IsValid());
        EXPECT_FALSE(Member(badge, "text").IsValid());

        const JsonView bare = FindElement(bar, "Bare");
        ASSERT_TRUE(bare.IsValid());
        EXPECT_FALSE(Member(bare, "components").IsValid()) << "an element with no components has no components member";
        EXPECT_FALSE(Member(bare, "icon").IsValid());
        EXPECT_FALSE(Member(bare, "tooltip").IsValid());
    }

    TEST_F(UIControlApiTest, AClickOnAComposedButtonEmitsAndAnswersItsActionAndValue) {
        Show("Composed");
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("Play"));

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView result = response.Result();
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiPlay");
        EXPECT_EQ(StringMember(result, "value"), "easy");
        EXPECT_EQ(recorder.Count("UIControlApiPlay"), 1);
        EXPECT_EQ(recorder.LastValue(), "easy");
        EXPECT_EQ(recorder.downs, 0) << "the canvas took the press, game code did not see it";
    }

    TEST_F(UIControlApiTest, ADisabledComposedButtonTakesTheClickAndAnswersWithNothingEmitted) {
        Show("Composed");
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("Dead"));

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView result = response.Result();
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "");
        EXPECT_EQ(StringMember(result, "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AComposedButtonOfADisabledGroupEmitsNothingEither) {
        Show("Composed");
        Require(Canvas("Composed", "Bar")->GetRoot().FindByName("Wide"_intern)).groupDisabled = true;
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("Wide"));

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        EXPECT_EQ(StringMember(response.Result(), "action"), "");
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AComposedButtonCoveredByAnotherElementIsNotClickable) {
        Show("Composed");
        Recorder recorder;

        const ControlReply response = Call("ui_click", Click("Shade"));

        EXPECT_EQ(response.ErrorCode(), "not_clickable");
        EXPECT_NE(response.ErrorMessage().find("Cover"), String::npos) << response.ErrorMessage();
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AnElementWithNoClickerIsNotClickable) {
        Show("Composed");

        const ControlReply response = Call("ui_click", Click("Badge"));

        EXPECT_EQ(response.ErrorCode(), "not_clickable");
        EXPECT_NE(response.ErrorMessage().find("not interactive"), String::npos) << response.ErrorMessage();
    }

    TEST_F(UIControlApiTest, AtPicksTheHorizontalPointOfAComposedButton) {
        Show("Composed");
        Recorder recorder;
        const String params = Click("Wide", R"("at":0.0)");

        const ControlReply response = Call("ui_click", params);

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView result = response.Result();
        EXPECT_NEAR(NumberMember(result, "x"), 10.0f, 1.0f) << "the left edge, a hair inside";
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiWide");
        EXPECT_EQ(recorder.Count("UIControlApiWide"), 1);
    }

    // ---- choice groups made of components: a segment or an option made of components ----------------------

    TEST_F(UIControlApiTest, ATreeShowsTheSegmentsOfAComposedGroupWithTheFieldsOfAButton) {
        CoreManager::GetUIVars().Set("uiApiCSide"_intern, "right");
        Show("Choices");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView side = FindElement(FindCanvas(response.Result(), "Panel"), "CSide");
        ASSERT_TRUE(side.IsValid());
        EXPECT_EQ(StringMember(side, "type"), "UISegmented");
        EXPECT_EQ(StringMember(side, "bind"), "uiApiCSide");
        EXPECT_EQ(StringMember(side, "selectedValue"), "right");
        EXPECT_FALSE(BoolMember(side, "disabled"));

        const JsonView left = FindElement(side, "CLeft");
        const JsonView right = FindElement(side, "CRight");
        const JsonView gone = FindElement(side, "CGone");
        ASSERT_TRUE(left.IsValid() && right.IsValid() && gone.IsValid());
        EXPECT_EQ(StringMember(left, "text"), "Left");
        EXPECT_EQ(StringMember(left, "action"), "UIControlApiCSide");
        EXPECT_EQ(StringMember(left, "value"), "left");
        EXPECT_FALSE(BoolMember(left, "selected"));
        EXPECT_FALSE(BoolMember(left, "disabled"));
        EXPECT_TRUE(BoolMember(left, "interactive"));
        ExpectRect(Member(left, "rect"), 100.0f, 100.0f, 132.0f, 56.0f);

        EXPECT_TRUE(BoolMember(right, "selected"));
        EXPECT_EQ(StringMember(right, "text"), "Right") << "the caption child";
        EXPECT_EQ(StringMember(right, "icon"), "SideIcon") << "the icon child";
        EXPECT_EQ(StringMember(right, "tooltip"), "The right one");
        EXPECT_EQ(StringMember(right, "value"), "right");

        EXPECT_TRUE(BoolMember(gone, "disabled"));
        EXPECT_FALSE(BoolMember(gone, "selected"));
    }

    TEST_F(UIControlApiTest, TheSelectionOfAComposedGroupFollowsTheVariableBetweenTwoTreeCalls) {
        CoreManager::GetUIVars().Set("uiApiCSide"_intern, "left");
        Show("Choices");
        const ControlReply first = Call("ui_tree");
        EXPECT_TRUE(BoolMember(FindElement(FindCanvas(first.Result(), "Panel"), "CLeft"), "selected"));

        CoreManager::GetUIVars().Set("uiApiCSide"_intern, "right");  // no draw in between
        const ControlReply second = Call("ui_tree");
        const JsonView panel = FindCanvas(second.Result(), "Panel");
        ASSERT_TRUE(panel.IsValid());
        EXPECT_FALSE(BoolMember(FindElement(panel, "CLeft"), "selected"));
        EXPECT_TRUE(BoolMember(FindElement(panel, "CRight"), "selected"));
        EXPECT_EQ(StringMember(FindElement(panel, "CSide"), "selectedValue"), "right");
    }

    TEST_F(UIControlApiTest, AClickOnAComposedSegmentAnswersWithItsActionAndValueAndTheListenerGetsThem) {
        Show("Choices");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("CRight"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        const JsonView result = clicked.Result();
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiCSide");
        EXPECT_EQ(StringMember(result, "value"), "right");
        EXPECT_EQ(recorder.Count("UIControlApiCSide"), 1);
        EXPECT_EQ(recorder.LastValue(), "right");
    }

    TEST_F(UIControlApiTest, AClickOnADisabledComposedSegmentOrOneOfADisabledGroupEmitsNothing) {
        Show("Choices");
        Recorder recorder;

        const ControlReply gone = Call("ui_click", Click("CGone"));
        ASSERT_TRUE(gone.IsOk()) << gone.ErrorMessage();
        EXPECT_TRUE(BoolMember(gone.Result(), "consumed"));
        EXPECT_EQ(StringMember(gone.Result(), "action"), "");
        EXPECT_EQ(StringMember(gone.Result(), "value"), "");

        auto canvas = Canvas("Choices", "Panel");
        ASSERT_TRUE(canvas);
        Require(canvas->GetRoot().FindByName("CSide"_intern)).Cast<UISegmented>()->disabled = true;
        Show("Choices");
        const ControlReply left = Call("ui_click", Click("CLeft"));
        ASSERT_TRUE(left.IsOk()) << left.ErrorMessage();
        EXPECT_EQ(StringMember(left.Result(), "action"), "");
        EXPECT_EQ(StringMember(left.Result(), "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AGroupOfAButtonAndAComposedSegmentIsDescribedAndClickedAlike) {
        CoreManager::GetUIVars().Set("uiApiMix"_intern, "composed");
        Show("Choices");
        Recorder recorder;

        const ControlReply tree = Call("ui_tree");
        const JsonView mix = FindElement(FindCanvas(tree.Result(), "Panel"), "Mix");
        ASSERT_TRUE(mix.IsValid());
        EXPECT_EQ(StringMember(mix, "selectedValue"), "composed");
        const JsonView button = FindElement(mix, "MixButton");
        const JsonView composed = FindElement(mix, "MixComposed");
        ASSERT_TRUE(button.IsValid() && composed.IsValid());
        EXPECT_EQ(StringMember(button, "type"), "UIElement");
        EXPECT_EQ(StringMember(composed, "type"), "UIElement");
        for (const JsonView segment : {button, composed}) {
            EXPECT_EQ(StringMember(segment, "action"), "UIControlApiMix");
            EXPECT_FALSE(StringMember(segment, "text").empty());
            EXPECT_FALSE(StringMember(segment, "value").empty());
            EXPECT_FALSE(BoolMember(segment, "disabled"));
        }
        EXPECT_FALSE(BoolMember(button, "selected"));
        EXPECT_TRUE(BoolMember(composed, "selected"));

        EXPECT_EQ(StringMember(Call("ui_click", Click("MixButton")).Result(), "value"), "button");
        EXPECT_EQ(StringMember(Call("ui_click", Click("MixComposed")).Result(), "value"), "composed");
        EXPECT_EQ(recorder.Count("UIControlApiMix"), 2);
    }

    TEST_F(UIControlApiTest, ATreeShowsAClosedDropdownOfComposedOptionsAndTheOptionsAsHidden) {
        CoreManager::GetUIVars().Set("uiApiCLevel"_intern, "hard");
        Show("Choices");

        const ControlReply response = Call("ui_tree");

        ASSERT_TRUE(response.IsOk()) << response.ErrorMessage();
        const JsonView level = FindElement(FindCanvas(response.Result(), "Panel"), "CLevel");
        ASSERT_TRUE(level.IsValid());
        EXPECT_EQ(StringMember(level, "type"), "UIDropdown");
        EXPECT_EQ(StringMember(level, "selectedValue"), "hard");
        EXPECT_EQ(StringMember(level, "text"), "Hard") << "the header shows the caption child of the chosen option";
        EXPECT_EQ(StringMember(level, "action"), "UIControlApiCLevel");
        EXPECT_FALSE(BoolMember(level, "open"));
        for (const char* option : {"CEasy", "CHard", "CLocked"}) {
            const JsonView item = FindElement(level, option);
            ASSERT_TRUE(item.IsValid()) << option;
            EXPECT_FALSE(BoolMember(item, "visible")) << option << " is hidden while the list is closed";
        }
        EXPECT_TRUE(BoolMember(FindElement(level, "CHard"), "selected"));
        EXPECT_FALSE(BoolMember(FindElement(level, "CEasy"), "selected"));
        EXPECT_TRUE(BoolMember(FindElement(level, "CLocked"), "disabled"));
        EXPECT_EQ(StringMember(FindElement(level, "CHard"), "value"), "hard");
        EXPECT_EQ(StringMember(FindElement(level, "CHard"), "icon"), "HardIcon");
        EXPECT_EQ(StringMember(FindElement(level, "CEasy"), "text"), "Easy");
        EXPECT_EQ(StringMember(FindElement(level, "CHard"), "action"), "") << "the options keep no action of their own";
    }

    TEST_F(UIControlApiTest, AClickOnAComposedOptionAnswersWithTheActionOfTheDropdownAndTheListCloses) {
        CoreManager::GetUIVars().Set("uiApiCLevel"_intern, "easy");
        Show("Choices");
        ASSERT_TRUE(Call("ui_click", Click("CLevel")).IsOk());
        Show("Choices");
        const ControlReply open = Call("ui_tree");
        const JsonView openLevel = FindElement(FindCanvas(open.Result(), "Panel"), "CLevel");
        ASSERT_TRUE(openLevel.IsValid());
        EXPECT_TRUE(BoolMember(openLevel, "open"));
        EXPECT_TRUE(BoolMember(FindElement(openLevel, "CHard"), "visible"));
        ExpectRect(Member(FindElement(openLevel, "CHard"), "rect"), 100.0f, 412.0f, 240.0f, 56.0f);
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("CHard"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        const JsonView result = clicked.Result();
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiCLevel");
        EXPECT_EQ(StringMember(result, "value"), "hard");
        EXPECT_EQ(recorder.Count("UIControlApiCLevel"), 1);
        EXPECT_EQ(recorder.LastValue(), "hard");

        Show("Choices");
        const ControlReply tree = Call("ui_tree");
        const JsonView level = FindElement(FindCanvas(tree.Result(), "Panel"), "CLevel");
        ASSERT_TRUE(level.IsValid());
        EXPECT_FALSE(BoolMember(level, "open"));
        EXPECT_FALSE(BoolMember(FindElement(level, "CHard"), "visible"));
        EXPECT_EQ(StringMember(level, "selectedValue"), "easy") << "the choice is the game's: a click sets no variable";
        EXPECT_FALSE(Member(FindElement(level, "CHard"), "open").IsValid());
        EXPECT_EQ(Call("ui_click", Click("CHard")).ErrorCode(), "not_clickable") << "the list is closed again";
    }

    TEST_F(UIControlApiTest, AComposedOptionIsNotClickableWhileTheListIsClosed) {
        Show("Choices");

        const ControlReply response = Call("ui_click", Click("CEasy"));

        EXPECT_EQ(response.ErrorCode(), "not_clickable");
        EXPECT_NE(response.ErrorMessage().find("hidden"), String::npos) << response.ErrorMessage();
    }

    TEST_F(UIControlApiTest, ADisabledComposedOptionTakesTheClickAndTheListStaysOpen) {
        Show("Choices");
        ASSERT_TRUE(Call("ui_click", Click("CLevel")).IsOk());
        Show("Choices");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("CLocked"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        EXPECT_TRUE(BoolMember(clicked.Result(), "consumed"));
        EXPECT_EQ(StringMember(clicked.Result(), "action"), "");
        EXPECT_EQ(StringMember(clicked.Result(), "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
        Show("Choices");
        const ControlReply tree = Call("ui_tree");
        EXPECT_TRUE(BoolMember(FindElement(FindCanvas(tree.Result(), "Panel"), "CLevel"), "open"));
    }

    TEST_F(UIControlApiTest, AClickElsewhereWhileAComposedListIsOpenClosesItAndIsConsumed) {
        Show("Choices");
        ASSERT_TRUE(Call("ui_click", Click("CLevel")).IsOk());
        Show("Choices");
        Recorder recorder;

        const ControlReply clicked = Call("ui_click", Click("CBelow"));

        ASSERT_TRUE(clicked.IsOk()) << clicked.ErrorMessage();
        EXPECT_TRUE(BoolMember(clicked.Result(), "consumed"));
        EXPECT_EQ(StringMember(clicked.Result(), "action"), "");
        EXPECT_EQ(recorder.Total(), 0u) << "the press only closed the list";
        Show("Choices");
        const ControlReply tree = Call("ui_tree");
        EXPECT_FALSE(BoolMember(FindElement(FindCanvas(tree.Result(), "Panel"), "CLevel"), "open"));
    }

    TEST_F(UIControlApiTest, ADropdownOfComposedOptionsCanBeDrivenEndToEnd) {
        Show("Choices");
        Recorder recorder;

        ASSERT_TRUE(Call("ui_click", Click("CLevel")).IsOk());
        Show("Choices");  // the "step"
        const ControlReply picked = Call("ui_click", Click("CEasy"));
        ASSERT_TRUE(picked.IsOk()) << picked.ErrorMessage();
        EXPECT_EQ(StringMember(picked.Result(), "value"), "easy");
        Show("Choices");

        EXPECT_EQ(recorder.Count("UIControlApiCLevel"), 1);
        EXPECT_EQ(recorder.LastValue(), "easy");
    }

}  // namespace BECore::Tests
