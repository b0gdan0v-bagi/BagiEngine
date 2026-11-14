#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIControl.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UITextComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-2f;

        // "Main": output 800 x 600, every canvas in window pixels (scale 1).
        //   Hud (layer 10): Plate (10, 10, 200 x 100) holds Title (a label with a variable) and Ok (a button, 122, 62, 80 x 40); Off (300, 200, 80 x 40) is a disabled
        //   button; Under (500, 300, 80 x 40) is a button with Cover (490, 290, 100 x 60) drawn over it; Note (300, 300, 80 x 40) is a label.
        //   Debug (layer 5): Tiny (400, 10, 20 x 20) is a button below the touch size, and a second Ok (500, 100, 80 x 40) with another action.
        //   Menu (layer 20): a modal canvas, hidden at first.
        // "Fit": 400 x 300 units, shown in a 1000 x 600 output: scale 2, offset (100, 0). View is a SceneView canvas with the button Go (10, 10, 50 x 40) and the slider Slide (10, 100, 200 x 40,
        // range 0..100). "Choice": 800 x 600 in window pixels. Panel holds Side, a UISegmented bound to `uiApiSide` with the buttons Left and Right (the same action, values "left" and "right"; 150 x
        // 56
        //   and 150 x 56 at x = 100, 252, y = 100), and Plain (100, 300, 80 x 40) is a button without a value.
        // "Values": 800 x 600 in window pixels. Loading is a UIProgressBar (100, 300, 400 x 24, `uiApiLoad`, range 0..100); CSound a
        //   toggle row (100, 500, 300 x 56; `uiApiCSound`, values "on" / "off", the caption child CSoundCaption and the switch child CSoundSwitch), CLocked a disabled one (520, 300, 240 x 56),
        //   CVolume a slider (450, 100, 340 x 56; `uiApiCVolume`, range 0..1, steps of 0.1) and CQuiet a disabled one (450, 500, 300 x 56).
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
  <scene type="Scene" name="Empty"><nodes/></scene>
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
            std::string LastValue() const {
                return _values.empty() ? std::string("<none>") : std::string(_values.back().ToStringView().data(), _values.back().ToStringView().size());
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

        std::string Text(const String& text) {
            return std::string(text.c_str(), text.size());
        }

        std::string StringMember(const Json& object, StringView key) {
            const auto value = GetJsonString(object, key);
            return value ? Text(*value) : std::string("<missing>");
        }

        double NumberMember(const Json& object, StringView key) {
            return GetJsonNumber(object, key).value_or(-12345.0);
        }

        bool BoolMember(const Json& object, StringView key) {
            return GetJsonBool(object, key).value_or(false);
        }

        const Json* Member(const Json& object, StringView key) {
            return FindJsonMember(object, key);
        }

        void ExpectRect(const Json* rect, float x, float y, float w, float h) {
            ASSERT_NE(rect, nullptr);
            EXPECT_NEAR(NumberMember(*rect, "x"), x, kTolerance);
            EXPECT_NEAR(NumberMember(*rect, "y"), y, kTolerance);
            EXPECT_NEAR(NumberMember(*rect, "w"), w, kTolerance);
            EXPECT_NEAR(NumberMember(*rect, "h"), h, kTolerance);
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
        Json Call(std::string_view method, const Json& params = Json()) {
            Json request = Json::object();
            request["id"] = ++_nextId;
            request["method"] = std::string(method);
            if (!params.is_null()) {
                request["params"] = params;
            }
            Json response;
            _service.Post(DumpJson(request), [&response](String text) {
                auto parsed = ParseJson(text);
                response = parsed ? std::move(*parsed) : Json();
            });
            _service.Drain();
            return response;
        }

        static Json Click(const char* name) {
            Json params = Json::object();
            params["name"] = std::string(name);
            return params;
        }

        static bool IsOk(const Json& response) {
            return BoolMember(response, "ok");
        }

        static std::string ErrorCode(const Json& response) {
            const Json* error = Member(response, "error");
            return error != nullptr ? StringMember(*error, "code") : std::string("<no error>");
        }

        static std::string ErrorMessage(const Json& response) {
            const Json* error = Member(response, "error");
            return error != nullptr ? StringMember(*error, "message") : std::string("<no error>");
        }

        /// The canvas of the result named after a scene node
        static const Json* FindCanvas(const Json& result, std::string_view node) {
            const Json* canvases = Member(result, "canvases");
            if (canvases == nullptr || !canvases->is_array()) {
                return nullptr;
            }
            for (const Json& canvas : *canvases) {
                if (StringMember(canvas, "node") == node) {
                    return &canvas;
                }
            }
            return nullptr;
        }

        /// The element of a canvas, searched through the whole tree
        static const Json* FindElement(const Json& parent, std::string_view name) {
            const Json* list = Member(parent, parent.contains("elements") ? "elements" : "children");
            if (list == nullptr || !list->is_array()) {
                return nullptr;
            }
            for (const Json& element : *list) {
                if (StringMember(element, "name") == name) {
                    return &element;
                }
                if (const Json* inside = FindElement(element, name)) {
                    return inside;
                }
            }
            return nullptr;
        }

        ControlService _service;
        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        int64_t _nextId = 0;
    };

    TEST_F(UIControlApiTest, TheMethodsAreRegistered) {
        const Json tree = Call("ui_tree");
        EXPECT_NE(ErrorCode(tree), "unknown_method");

        const Json click = Call("ui_click", Click("Ok"));
        EXPECT_NE(ErrorCode(click), "unknown_method");
    }

    // ---- ui_tree ----------------------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, ASceneWithoutCanvasesHasAnEmptyList) {
        _manager->SetActiveScene(PoolString::Intern("Empty"));

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* result = Member(response, "result");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(StringMember(*result, "scene"), "Empty");
        const Json* canvases = Member(*result, "canvases");
        ASSERT_NE(canvases, nullptr);
        EXPECT_TRUE(canvases->is_array());
        EXPECT_TRUE(canvases->empty());
    }

    TEST_F(UIControlApiTest, BothMethodsNeedAnActiveScene) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString("<SceneManager><scenes/></SceneManager>"));
        auto bare = New<SceneManager>();
        bare->Deserialize(deserializer);
        bare->Initialize();
        ASSERT_EQ(bare->GetActiveScene(), nullptr);
        EngineEnvironment::UseSceneManager(bare);

        EXPECT_EQ(ErrorCode(Call("ui_tree")), "no_scene");
        EXPECT_EQ(ErrorCode(Call("ui_click", Click("Ok"))), "no_scene");
    }

    TEST_F(UIControlApiTest, ACanvasThatWasNeverDrawnHasItsStructureAndNoRects) {
        _manager->SetActiveScene(PoolString::Intern("Main"));

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* hud = FindCanvas(*Member(response, "result"), "Hud");
        ASSERT_NE(hud, nullptr);
        EXPECT_FALSE(BoolMember(*hud, "laidOut"));
        EXPECT_EQ(Member(*hud, "bounds"), nullptr);
        const Json* ok = FindElement(*hud, "Ok");
        ASSERT_NE(ok, nullptr);
        EXPECT_EQ(StringMember(*ok, "type"), "UIElement");
        EXPECT_EQ(Member(*ok, "rect"), nullptr);
        EXPECT_EQ(Member(*ok, "visible"), nullptr);
        const Json* issues = Member(*hud, "issues");
        ASSERT_NE(issues, nullptr);
        EXPECT_TRUE(issues->empty());
    }

    TEST_F(UIControlApiTest, ATreeReportsCanvasesElementsAndTheirRects) {
        Show("Main");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json& result = *Member(response, "result");
        EXPECT_EQ(StringMember(result, "scene"), "Main");
        const Json* canvases = Member(result, "canvases");
        ASSERT_NE(canvases, nullptr);
        EXPECT_EQ(canvases->size(), 3u);

        const Json* hud = FindCanvas(result, "Hud");
        ASSERT_NE(hud, nullptr);
        EXPECT_EQ(StringMember(*hud, "space"), "Screen");
        EXPECT_TRUE(BoolMember(*hud, "visible"));
        EXPECT_FALSE(BoolMember(*hud, "modal"));
        EXPECT_TRUE(BoolMember(*hud, "inputEnabled"));
        EXPECT_TRUE(BoolMember(*hud, "takesInput"));
        EXPECT_TRUE(BoolMember(*hud, "laidOut"));
        EXPECT_NEAR(NumberMember(*hud, "scale"), 1.0, kTolerance);
        ExpectRect(Member(*hud, "bounds"), 0.0f, 0.0f, 800.0f, 600.0f);
        ExpectRect(Member(*hud, "contentBounds"), 0.0f, 0.0f, 800.0f, 600.0f);

        // Rects are in canvas coordinates, not relative to the parent
        const Json* plate = FindElement(*hud, "Plate");
        ASSERT_NE(plate, nullptr);
        EXPECT_EQ(StringMember(*plate, "type"), "UIPanel");
        ExpectRect(Member(*plate, "rect"), 10.0f, 10.0f, 200.0f, 100.0f);
        const Json* ok = FindElement(*plate, "Ok");
        ASSERT_NE(ok, nullptr);
        EXPECT_EQ(StringMember(*ok, "type"), "UIElement");
        ExpectRect(Member(*ok, "rect"), 122.0f, 62.0f, 80.0f, 40.0f);
        ExpectRect(Member(*ok, "outputRect"), 122.0f, 62.0f, 80.0f, 40.0f);
        EXPECT_TRUE(BoolMember(*ok, "visible"));
        EXPECT_TRUE(BoolMember(*ok, "interactive"));
        EXPECT_EQ(StringMember(*ok, "text"), "OK");
        EXPECT_EQ(StringMember(*ok, "action"), "UIControlApiOk");
        EXPECT_FALSE(BoolMember(*ok, "disabled"));

        const Json* off = FindElement(*hud, "Off");
        ASSERT_NE(off, nullptr);
        EXPECT_TRUE(BoolMember(*off, "disabled"));

        const Json* note = FindElement(*hud, "Note");
        ASSERT_NE(note, nullptr);
        EXPECT_EQ(StringMember(*note, "type"), "UIElement");
        EXPECT_EQ(StringMember(*note, "text"), "Note") << "a plain text is an element with a text component";
        EXPECT_FALSE(BoolMember(*note, "interactive"));
        EXPECT_EQ(Member(*note, "action"), nullptr);

        const Json* menu = FindCanvas(result, "Menu");
        ASSERT_NE(menu, nullptr);
        EXPECT_TRUE(BoolMember(*menu, "modal"));
        EXPECT_FALSE(BoolMember(*menu, "visible"));
        EXPECT_FALSE(BoolMember(*menu, "takesInput"));
    }

    TEST_F(UIControlApiTest, AnElementWithNoBoxStyleHasNoBoxKey) {
        Show("Boxes");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* canvas = FindCanvas(*Member(response, "result"), "Panel");
        ASSERT_NE(canvas, nullptr);
        const Json* flat = FindElement(*canvas, "Flat");
        ASSERT_NE(flat, nullptr);
        EXPECT_EQ(Member(*flat, "box"), nullptr);
        // Every element of the other scenes has the default style too
        Show("Main");
        const Json main = Call("ui_tree");
        EXPECT_EQ(Text(DumpJson(main)).find("\"box\""), std::string::npos);
    }

    TEST_F(UIControlApiTest, AnElementWithEveryBoxFieldReportsThemAll) {
        Show("Boxes");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* canvas = FindCanvas(*Member(response, "result"), "Panel");
        ASSERT_NE(canvas, nullptr);
        const Json* fancy = FindElement(*canvas, "Fancy");
        ASSERT_NE(fancy, nullptr);
        const Json* box = Member(*fancy, "box");
        ASSERT_NE(box, nullptr);
        const Json* corners = Member(*box, "cornerRadius");
        ASSERT_NE(corners, nullptr);
        ASSERT_TRUE(corners->is_array());
        ASSERT_EQ(corners->size(), 4u);
        for (const Json& radius : *corners) {
            EXPECT_NEAR(radius.get<double>(), 16.0, kTolerance);
        }
        EXPECT_NEAR(NumberMember(*box, "borderWidth"), 2.0, kTolerance);
        EXPECT_EQ(StringMember(*box, "borderColor"), "255,255,255,255");
        const Json* gradient = Member(*box, "gradient");
        ASSERT_NE(gradient, nullptr);
        EXPECT_EQ(StringMember(*gradient, "direction"), "Vertical");
        EXPECT_EQ(StringMember(*gradient, "color"), "50,60,70,255");
        const Json* shadow = Member(*box, "shadow");
        ASSERT_NE(shadow, nullptr);
        EXPECT_NEAR(NumberMember(*shadow, "offsetX"), 0.0, kTolerance);
        EXPECT_NEAR(NumberMember(*shadow, "offsetY"), 4.0, kTolerance);
        EXPECT_NEAR(NumberMember(*shadow, "blur"), 8.0, kTolerance);
        EXPECT_NEAR(NumberMember(*shadow, "spread"), 0.0, kTolerance);
        EXPECT_EQ(StringMember(*shadow, "color"), "0,0,0,128");
        const Json* glow = Member(*box, "glow");
        ASSERT_NE(glow, nullptr);
        EXPECT_NEAR(NumberMember(*glow, "blur"), 12.0, kTolerance);
        EXPECT_NEAR(NumberMember(*glow, "spread"), 2.0, kTolerance);
        EXPECT_EQ(StringMember(*glow, "color"), "255,215,0,64");
        EXPECT_EQ(StringMember(*box, "highlight"), "255,255,255,60");
    }

    TEST_F(UIControlApiTest, ABoxReportsOnlyWhatIsSetAndABoxOfAButtonIsTheAuthoredOne) {
        Show("Boxes");
        const Json response = Call("ui_tree");
        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* canvas = FindCanvas(*Member(response, "result"), "Panel");
        ASSERT_NE(canvas, nullptr);
        const Json* round = FindElement(*canvas, "Round");
        ASSERT_NE(round, nullptr);
        const Json* box = Member(*round, "box");
        ASSERT_NE(box, nullptr);
        const Json* corners = Member(*box, "cornerRadius");
        ASSERT_NE(corners, nullptr);
        ASSERT_EQ(corners->size(), 4u);
        EXPECT_NEAR((*corners)[0].get<double>(), 4.0, kTolerance);
        EXPECT_NEAR((*corners)[1].get<double>(), 8.0, kTolerance);
        EXPECT_NEAR((*corners)[2].get<double>(), 12.0, kTolerance);
        EXPECT_NEAR((*corners)[3].get<double>(), 0.0, kTolerance);
        for (const char* key : {"borderWidth", "borderColor", "gradient", "shadow", "glow", "highlight"}) {
            EXPECT_EQ(Member(*box, key), nullptr) << key;
        }
    }

    TEST_F(UIControlApiTest, ALabelShowsItsTextWithTheVariablesReplaced) {
        CoreManager::GetUIVars().Set("score"_intern, 42);
        Show("Main");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* hud = FindCanvas(*Member(response, "result"), "Hud");
        ASSERT_NE(hud, nullptr);
        const Json* title = FindElement(*hud, "Title");
        ASSERT_NE(title, nullptr);
        EXPECT_EQ(StringMember(*title, "text"), "Score: 42");
    }

    TEST_F(UIControlApiTest, ALabelShowsTheStringOfTheCurrentLanguageInTheTree) {
        UIStrings& strings = CoreManager::GetUIStrings();
        strings.Set("en"_intern, "uiApi.title", "Score");
        strings.Set("de"_intern, "uiApi.title", "Punkte");
        CoreManager::GetUIVars().Set("score"_intern, 42);
        Show("Main");
        auto* title = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>()->GetRoot().FindByName(PoolString::Intern("Title"));
        ASSERT_NE(title, nullptr);
        title->GetComponent<UITextComponent>()->text = "{#uiApi.title}: {score}";

        const auto shownText = [this]() {
            const Json response = Call("ui_tree");
            EXPECT_TRUE(IsOk(response)) << ErrorMessage(response);
            const Json* hud = FindCanvas(*Member(response, "result"), "Hud");
            const Json* label = hud ? FindElement(*hud, "Title") : nullptr;
            return label ? StringMember(*label, "text") : std::string("<no such element>");
        };

        EXPECT_EQ(shownText(), "Score: 42");
        strings.SetLanguage("de"_intern);
        EXPECT_EQ(shownText(), "Punkte: 42") << "the tree reads the text as of now, with no draw in between";
    }

    TEST_F(UIControlApiTest, AHiddenElementIsNotVisible) {
        auto hudCanvas = Canvas("Main", "Hud");
        ASSERT_TRUE(hudCanvas->SetElementVisible("Note"_intern, false));
        Show("Main");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* hud = FindCanvas(*Member(response, "result"), "Hud");
        ASSERT_NE(hud, nullptr);
        const Json* note = FindElement(*hud, "Note");
        ASSERT_NE(note, nullptr);
        EXPECT_FALSE(BoolMember(*note, "visible"));
        const Json* rect = Member(*note, "rect");
        ASSERT_NE(rect, nullptr);
        EXPECT_TRUE(NumberMember(*rect, "w") == 0.0 || NumberMember(*rect, "h") == 0.0) << "a hidden element has an empty rect";
    }

    TEST_F(UIControlApiTest, TheValidatorsFindingsAreInTheTree) {
        Show("Main");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json& result = *Member(response, "result");
        const Json* hud = FindCanvas(result, "Hud");
        const Json* debug = FindCanvas(result, "Debug");
        ASSERT_TRUE(hud != nullptr && debug != nullptr);
        const Json* hudIssues = Member(*hud, "issues");
        ASSERT_NE(hudIssues, nullptr);
        EXPECT_TRUE(hudIssues->empty()) << DumpJson(*hudIssues).c_str();

        const Json* debugIssues = Member(*debug, "issues");
        ASSERT_NE(debugIssues, nullptr);
        ASSERT_EQ(debugIssues->size(), 1u) << DumpJson(*debugIssues).c_str();
        const Json& issue = (*debugIssues)[0];
        EXPECT_EQ(StringMember(issue, "kind"), "TouchTargetTooSmall");
        EXPECT_EQ(StringMember(issue, "element"), "Tiny");
        ExpectRect(Member(issue, "rect"), 400.0f, 10.0f, 20.0f, 20.0f);
    }

    TEST_F(UIControlApiTest, AButtonTellsItsIconAndItsTooltipSourceOnlyWhenTheyAreSet) {
        CoreManager::GetUIVars().Set("user"_intern, "Ann");
        Show("Icons");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* bar = FindCanvas(*Member(response, "result"), "Bar");
        ASSERT_NE(bar, nullptr);
        const Json* gear = FindElement(*bar, "Gear");
        ASSERT_NE(gear, nullptr);
        EXPECT_EQ(StringMember(*gear, "icon"), "GearIcon");
        EXPECT_EQ(StringMember(*gear, "tooltip"), "Open {#tip.gear} for {user}") << "the source text, neither strings nor variables expanded";

        const Json* quiet = FindElement(*bar, "Quiet");
        ASSERT_NE(quiet, nullptr);
        EXPECT_EQ(StringMember(*quiet, "icon"), "GearIcon");
        EXPECT_FALSE(quiet->contains("tooltip"));

        const Json* words = FindElement(*bar, "Words");
        ASSERT_NE(words, nullptr);
        EXPECT_FALSE(words->contains("icon"));
        EXPECT_FALSE(words->contains("tooltip"));
        EXPECT_EQ(StringMember(*words, "action"), "UIControlApiWords") << "the rest of a button's entry is as before";
    }

    TEST_F(UIControlApiTest, ACanvasWithNoRegionsHasAnEmptyObjectAndItsElementsHaveNoRegionMember) {
        Show("Main");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* hud = FindCanvas(*Member(response, "result"), "Hud");
        ASSERT_NE(hud, nullptr);
        const Json* regions = Member(*hud, "regions");
        ASSERT_NE(regions, nullptr);
        EXPECT_TRUE(regions->is_object());
        EXPECT_TRUE(regions->empty());
        const Json* ok = FindElement(*hud, "Ok");
        ASSERT_NE(ok, nullptr);
        EXPECT_EQ(Member(*ok, "region"), nullptr);
    }

    TEST_F(UIControlApiTest, ARegionIsListedWithItsRectAndTheElementThatNamesItShowsTheName) {
        auto hudCanvas = Canvas("Main", "Hud");
        hudCanvas->SetRegion("hud.corner"_intern, {10.0f, 20.0f, 300.0f, 100.0f});
        hudCanvas->GetLayout().root->FindByName("Ok"_intern)->style.rect.region = "hud.corner"_intern;
        hudCanvas->Invalidate();
        Show("Main");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* hud = FindCanvas(*Member(response, "result"), "Hud");
        ASSERT_NE(hud, nullptr);
        const Json* regions = Member(*hud, "regions");
        ASSERT_NE(regions, nullptr);
        EXPECT_EQ(regions->size(), 1u);
        ExpectRect(Member(*regions, "hud.corner"), 10.0f, 20.0f, 300.0f, 100.0f);
        const Json* ok = FindElement(*hud, "Ok");
        ASSERT_NE(ok, nullptr);
        EXPECT_EQ(StringMember(*ok, "region"), "hud.corner");
        const Json* issues = Member(*hud, "issues");
        ASSERT_NE(issues, nullptr);
        for (const Json& issue : *issues) {
            EXPECT_NE(StringMember(issue, "kind"), "MissingRegion") << DumpJson(*issues).c_str();
        }
    }

    TEST_F(UIControlApiTest, AnUnpublishedRegionIsNamedOnTheElementAndReportedAsMissing) {
        auto hudCanvas = Canvas("Main", "Hud");
        hudCanvas->GetLayout().root->FindByName("Ok"_intern)->style.rect.region = "hud.gone"_intern;
        hudCanvas->Invalidate();
        Show("Main");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* hud = FindCanvas(*Member(response, "result"), "Hud");
        ASSERT_NE(hud, nullptr);
        EXPECT_EQ(StringMember(*FindElement(*hud, "Ok"), "region"), "hud.gone");
        const Json* issues = Member(*hud, "issues");
        ASSERT_NE(issues, nullptr);
        ASSERT_EQ(issues->size(), 1u) << DumpJson(*issues).c_str();
        EXPECT_EQ(StringMember((*issues)[0], "kind"), "MissingRegion");
        EXPECT_EQ(StringMember((*issues)[0], "element"), "Ok");
        EXPECT_EQ(StringMember((*issues)[0], "other"), "hud.gone");
    }

    TEST_F(UIControlApiTest, TheRegionsOfACanvasThatWasNeverDrawnAreAlreadyListed) {
        _manager->SetActiveScene(PoolString::Intern("Main"));
        Canvas("Main", "Hud")->SetRegion("hud.corner"_intern, {1.0f, 2.0f, 3.0f, 4.0f});

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* hud = FindCanvas(*Member(response, "result"), "Hud");
        ASSERT_NE(hud, nullptr);
        EXPECT_FALSE(BoolMember(*hud, "laidOut"));
        ExpectRect(Member(*Member(*hud, "regions"), "hud.corner"), 1.0f, 2.0f, 3.0f, 4.0f);
    }

    TEST_F(UIControlApiTest, OutputRectsAreInTheOutputPixelsOfTheCanvasTransform) {
        Show("Fit", 1000.0f, 600.0f);

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* view = FindCanvas(*Member(response, "result"), "View");
        ASSERT_NE(view, nullptr);
        EXPECT_EQ(StringMember(*view, "space"), "SceneView");
        EXPECT_NEAR(NumberMember(*view, "scale"), 2.0, kTolerance);
        const Json* go = FindElement(*view, "Go");
        ASSERT_NE(go, nullptr);
        ExpectRect(Member(*go, "rect"), 10.0f, 10.0f, 50.0f, 40.0f);
        ExpectRect(Member(*go, "outputRect"), 120.0f, 20.0f, 100.0f, 80.0f);
    }

    // ---- the choice group ---------------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, ATreeShowsTheValueAndTheSelectionOfAChoiceGroup) {
        CoreManager::GetUIVars().Set("uiApiSide"_intern, "right");
        Show("Choice");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* panel = FindCanvas(*Member(response, "result"), "Panel");
        ASSERT_NE(panel, nullptr);
        const Json* side = FindElement(*panel, "Side");
        ASSERT_NE(side, nullptr);
        EXPECT_EQ(StringMember(*side, "type"), "UISegmented");
        EXPECT_EQ(StringMember(*side, "bind"), "uiApiSide");
        EXPECT_EQ(StringMember(*side, "selectedValue"), "right");
        EXPECT_FALSE(BoolMember(*side, "disabled"));
        const Json* left = FindElement(*side, "Left");
        const Json* right = FindElement(*side, "Right");
        ASSERT_TRUE(left != nullptr && right != nullptr);
        EXPECT_EQ(StringMember(*left, "value"), "left");
        EXPECT_EQ(StringMember(*right, "value"), "right");
        EXPECT_EQ(StringMember(*left, "action"), "UIControlApiSide");
        EXPECT_FALSE(BoolMember(*left, "selected"));
        EXPECT_TRUE(BoolMember(*right, "selected"));
        ExpectRect(Member(*left, "rect"), 100.0f, 100.0f, 150.0f, 56.0f);
        ExpectRect(Member(*right, "rect"), 252.0f, 100.0f, 150.0f, 56.0f);
    }

    TEST_F(UIControlApiTest, TheSelectionFollowsTheVariableBetweenTwoTreeCalls) {
        CoreManager::GetUIVars().Set("uiApiSide"_intern, "left");
        Show("Choice");
        const Json first = Call("ui_tree");

        CoreManager::GetUIVars().Set("uiApiSide"_intern, "right");  // no draw in between: the tree must not show the last draw's selection
        const Json second = Call("ui_tree");

        ASSERT_TRUE(IsOk(first) && IsOk(second));
        const Json* before = FindElement(*FindCanvas(*Member(first, "result"), "Panel"), "Side");
        const Json* after = FindElement(*FindCanvas(*Member(second, "result"), "Panel"), "Side");
        ASSERT_TRUE(before != nullptr && after != nullptr);
        EXPECT_EQ(StringMember(*before, "selectedValue"), "left");
        EXPECT_TRUE(BoolMember(*FindElement(*before, "Left"), "selected"));
        EXPECT_FALSE(BoolMember(*FindElement(*before, "Right"), "selected"));
        EXPECT_EQ(StringMember(*after, "selectedValue"), "right");
        EXPECT_FALSE(BoolMember(*FindElement(*after, "Left"), "selected"));
        EXPECT_TRUE(BoolMember(*FindElement(*after, "Right"), "selected"));
    }

    TEST_F(UIControlApiTest, AnUnsetVariableSelectsNothingAndATreeBeforeTheFirstDrawStillSaysSo) {
        _manager->SetActiveScene(PoolString::Intern("Choice"));

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* side = FindElement(*FindCanvas(*Member(response, "result"), "Panel"), "Side");
        ASSERT_NE(side, nullptr);
        EXPECT_EQ(StringMember(*side, "selectedValue"), "");
        EXPECT_FALSE(BoolMember(*FindElement(*side, "Left"), "selected"));
        EXPECT_FALSE(BoolMember(*FindElement(*side, "Right"), "selected"));
    }

    TEST_F(UIControlApiTest, ADisabledGroupShowsItsSegmentsAsDisabled) {
        auto* side = Canvas("Choice", "Panel")->GetLayout().root->children[0]->Cast<UISegmented>();
        ASSERT_NE(side, nullptr);
        side->disabled = true;
        Show("Choice");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* group = FindElement(*FindCanvas(*Member(response, "result"), "Panel"), "Side");
        ASSERT_NE(group, nullptr);
        EXPECT_TRUE(BoolMember(*group, "disabled"));
        EXPECT_TRUE(BoolMember(*FindElement(*group, "Left"), "disabled")) << "a click on it does nothing, which is what the field says";
        EXPECT_TRUE(BoolMember(*FindElement(*group, "Right"), "disabled"));
        EXPECT_FALSE(BoolMember(*FindElement(*FindCanvas(*Member(response, "result"), "Panel"), "Plain"), "disabled"));
    }

    TEST_F(UIControlApiTest, AClickOnASegmentAnswersWithTheActionAndTheValueAndTheListenerGetsThem) {
        CoreManager::GetUIVars().Set("uiApiSide"_intern, "left");
        Show("Choice");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("Right"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        const Json& result = *Member(clicked, "result");
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiSide");
        EXPECT_EQ(StringMember(result, "value"), "right");
        EXPECT_EQ(recorder.Count("UIControlApiSide"), 1);
        EXPECT_EQ(recorder.LastValue(), "right");
    }

    TEST_F(UIControlApiTest, AChoiceCanBeCheckedEndToEnd) {
        CoreManager::GetUIVars().Set("uiApiSide"_intern, "left");
        Show("Choice");
        const Json before = Call("ui_tree");
        ASSERT_TRUE(IsOk(before));
        EXPECT_TRUE(BoolMember(*FindElement(*FindCanvas(*Member(before, "result"), "Panel"), "Left"), "selected"));

        // The click only reports; the game (here the test) answers by setting the variable
        ASSERT_TRUE(IsOk(Call("ui_click", Click("Right"))));
        const Json unchanged = Call("ui_tree");
        ASSERT_TRUE(IsOk(unchanged));
        EXPECT_TRUE(BoolMember(*FindElement(*FindCanvas(*Member(unchanged, "result"), "Panel"), "Left"), "selected")) << "a click selects nothing by itself";
        EXPECT_FALSE(BoolMember(*FindElement(*FindCanvas(*Member(unchanged, "result"), "Panel"), "Right"), "selected"));

        CoreManager::GetUIVars().Set("uiApiSide"_intern, "right");
        const Json after = Call("ui_tree");
        ASSERT_TRUE(IsOk(after));
        const Json* panel = FindCanvas(*Member(after, "result"), "Panel");
        EXPECT_FALSE(BoolMember(*FindElement(*panel, "Left"), "selected"));
        EXPECT_TRUE(BoolMember(*FindElement(*panel, "Right"), "selected"));
    }

    TEST_F(UIControlApiTest, ADisabledGroupTakesTheClickAndEmitsNothing) {
        auto* side = Canvas("Choice", "Panel")->GetLayout().root->children[0]->Cast<UISegmented>();
        ASSERT_NE(side, nullptr);
        side->disabled = true;
        Show("Choice");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("Left"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, APlainButtonHasAnEmptyValueAndNeverSelectedAndALabelHasNeither) {
        Show("Choice");
        Recorder recorder;

        const Json tree = Call("ui_tree");
        const Json clicked = Call("ui_click", Click("Plain"));

        ASSERT_TRUE(IsOk(tree) && IsOk(clicked));
        const Json* plain = FindElement(*FindCanvas(*Member(tree, "result"), "Panel"), "Plain");
        ASSERT_NE(plain, nullptr);
        EXPECT_NE(Member(*plain, "value"), nullptr);
        EXPECT_EQ(StringMember(*plain, "value"), "");
        EXPECT_FALSE(BoolMember(*plain, "selected"));
        EXPECT_EQ(Member(*plain, "bind"), nullptr) << "only a choice group has a bind";
        EXPECT_EQ(StringMember(*Member(clicked, "result"), "value"), "");
        EXPECT_EQ(recorder.Count("UIControlApiPlain"), 1);
        EXPECT_EQ(recorder.LastValue(), "");

        Show("Main");
        const Json main = Call("ui_tree");
        const Json* note = FindElement(*FindCanvas(*Member(main, "result"), "Hud"), "Note");
        ASSERT_NE(note, nullptr);
        EXPECT_EQ(Member(*note, "value"), nullptr);
        EXPECT_EQ(Member(*note, "selected"), nullptr);
    }

    // ---- the dropdown -----------------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, ATreeShowsAClosedDropdownAndItsOptionsAsHidden) {
        CoreManager::GetUIVars().Set("uiApiLevel"_intern, "normal");
        Show("Pick");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* panel = FindCanvas(*Member(response, "result"), "Panel");
        ASSERT_NE(panel, nullptr);
        const Json* level = FindElement(*panel, "Level");
        ASSERT_NE(level, nullptr);
        EXPECT_EQ(StringMember(*level, "type"), "UIDropdown");
        EXPECT_EQ(StringMember(*level, "bind"), "uiApiLevel");
        EXPECT_EQ(StringMember(*level, "selectedValue"), "normal");
        EXPECT_EQ(StringMember(*level, "action"), "UIControlApiLevel");
        EXPECT_EQ(StringMember(*level, "text"), "Normal") << "the header shows the chosen option";
        EXPECT_FALSE(BoolMember(*level, "open"));
        EXPECT_FALSE(BoolMember(*level, "disabled"));
        EXPECT_TRUE(BoolMember(*level, "interactive"));
        EXPECT_TRUE(BoolMember(*level, "visible"));
        ExpectRect(Member(*level, "rect"), 100.0f, 100.0f, 240.0f, 56.0f);
        for (const char* option : {"Easy", "Normal", "Hard", "Locked"}) {
            const Json* item = FindElement(*level, option);
            ASSERT_NE(item, nullptr) << option;
            EXPECT_FALSE(BoolMember(*item, "visible")) << option << " is hidden while the list is closed";
        }
        EXPECT_TRUE(BoolMember(*FindElement(*level, "Normal"), "selected"));
        EXPECT_FALSE(BoolMember(*FindElement(*level, "Easy"), "selected"));
        EXPECT_TRUE(BoolMember(*FindElement(*level, "Locked"), "disabled"));
        EXPECT_EQ(StringMember(*FindElement(*level, "Hard"), "value"), "hard");
        EXPECT_EQ(StringMember(*FindElement(*level, "Hard"), "action"), "") << "the options keep no action of their own";
        EXPECT_EQ(Member(*FindElement(*panel, "Below"), "open"), nullptr) << "only a dropdown has open";
    }

    TEST_F(UIControlApiTest, TheSelectionOfADropdownFollowsTheVariableWithNoDraw) {
        CoreManager::GetUIVars().Set("uiApiLevel"_intern, "easy");
        Show("Pick");
        CoreManager::GetUIVars().Set("uiApiLevel"_intern, "hard");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response));
        const Json* level = FindElement(*FindCanvas(*Member(response, "result"), "Panel"), "Level");
        ASSERT_NE(level, nullptr);
        EXPECT_EQ(StringMember(*level, "selectedValue"), "hard");
        EXPECT_TRUE(BoolMember(*FindElement(*level, "Hard"), "selected"));
        EXPECT_FALSE(BoolMember(*FindElement(*level, "Easy"), "selected"));
    }

    TEST_F(UIControlApiTest, AnUnsetDropdownShowsThePlaceholderAndSelectsNothing) {
        Show("Pick");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response));
        const Json* level = FindElement(*FindCanvas(*Member(response, "result"), "Panel"), "Level");
        ASSERT_NE(level, nullptr);
        EXPECT_EQ(StringMember(*level, "selectedValue"), "");
        EXPECT_EQ(StringMember(*level, "text"), "Pick");
        EXPECT_FALSE(BoolMember(*FindElement(*level, "Easy"), "selected"));
    }

    TEST_F(UIControlApiTest, AClickOnTheHeaderOpensTheListAndEmitsNothing) {
        Show("Pick");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("Level"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        const Json& result = *Member(clicked, "result");
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(Member(result, "action"), nullptr);
        EXPECT_EQ(Member(result, "value"), nullptr);
        EXPECT_EQ(recorder.Total(), 0u);

        // The open list gets its rects at the next draw
        const Json stale = Call("ui_tree");
        EXPECT_TRUE(BoolMember(*FindElement(*FindCanvas(*Member(stale, "result"), "Panel"), "Level"), "open"));
        Show("Pick");
        const Json tree = Call("ui_tree");
        ASSERT_TRUE(IsOk(tree));
        const Json* level = FindElement(*FindCanvas(*Member(tree, "result"), "Panel"), "Level");
        ASSERT_NE(level, nullptr);
        EXPECT_TRUE(BoolMember(*level, "open"));
        for (const char* option : {"Easy", "Normal", "Hard", "Locked"}) {
            EXPECT_TRUE(BoolMember(*FindElement(*level, option), "visible")) << option;
        }
        ExpectRect(Member(*FindElement(*level, "Easy"), "rect"), 100.0f, 156.0f, 240.0f, 56.0f);
        ExpectRect(Member(*FindElement(*level, "Locked"), "rect"), 100.0f, 324.0f, 240.0f, 56.0f);
    }

    TEST_F(UIControlApiTest, AClickOnAnOptionAnswersWithTheActionAndTheValueAndTheListClosesAtTheNextFrame) {
        CoreManager::GetUIVars().Set("uiApiLevel"_intern, "easy");
        Show("Pick");
        ASSERT_TRUE(IsOk(Call("ui_click", Click("Level"))));
        Show("Pick");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("Hard"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        const Json& result = *Member(clicked, "result");
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiLevel");
        EXPECT_EQ(StringMember(result, "value"), "hard");
        EXPECT_EQ(recorder.Count("UIControlApiLevel"), 1);
        EXPECT_EQ(recorder.LastValue(), "hard");

        Show("Pick");
        const Json tree = Call("ui_tree");
        const Json* level = FindElement(*FindCanvas(*Member(tree, "result"), "Panel"), "Level");
        ASSERT_NE(level, nullptr);
        EXPECT_FALSE(BoolMember(*level, "open"));
        EXPECT_FALSE(BoolMember(*FindElement(*level, "Hard"), "visible"));
        EXPECT_EQ(StringMember(*level, "selectedValue"), "easy") << "the choice is the game's: a click sets no variable";
        EXPECT_EQ(StringMember(*level, "text"), "Easy");

        CoreManager::GetUIVars().Set("uiApiLevel"_intern, "hard");  // the game answers
        const Json after = Call("ui_tree");
        EXPECT_EQ(StringMember(*FindElement(*FindCanvas(*Member(after, "result"), "Panel"), "Level"), "text"), "Hard");
        const Json again = Call("ui_click", Click("Hard"));
        EXPECT_EQ(ErrorCode(again), "not_clickable") << "the list is closed again";
    }

    TEST_F(UIControlApiTest, AnOptionIsNotClickableWhileTheListIsClosed) {
        Show("Pick");

        const Json response = Call("ui_click", Click("Easy"));

        EXPECT_FALSE(IsOk(response));
        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("hidden"), std::string::npos) << ErrorMessage(response);
    }

    TEST_F(UIControlApiTest, ADisabledOptionTakesTheClickAndTheListStaysOpen) {
        Show("Pick");
        ASSERT_TRUE(IsOk(Call("ui_click", Click("Level"))));
        Show("Pick");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("Locked"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        const Json& result = *Member(clicked, "result");
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "") << "nothing was emitted";
        EXPECT_EQ(StringMember(result, "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
        Show("Pick");
        const Json tree = Call("ui_tree");
        EXPECT_TRUE(BoolMember(*FindElement(*FindCanvas(*Member(tree, "result"), "Panel"), "Level"), "open"));
    }

    TEST_F(UIControlApiTest, AClickElsewhereWhileTheListIsOpenClosesItAndIsConsumed) {
        Show("Pick");
        ASSERT_TRUE(IsOk(Call("ui_click", Click("Level"))));
        Show("Pick");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("Below"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        EXPECT_TRUE(BoolMember(*Member(clicked, "result"), "consumed"));
        EXPECT_EQ(StringMember(*Member(clicked, "result"), "action"), "") << "the answer must not report what the press did not emit";
        EXPECT_EQ(StringMember(*Member(clicked, "result"), "value"), "");
        EXPECT_EQ(recorder.Count("UIControlApiBelow"), 0) << "the press only closed the list";
        EXPECT_EQ(recorder.Total(), 0u);
        Show("Pick");
        const Json tree = Call("ui_tree");
        EXPECT_FALSE(BoolMember(*FindElement(*FindCanvas(*Member(tree, "result"), "Panel"), "Level"), "open"));
    }

    TEST_F(UIControlApiTest, ADisabledDropdownTakesTheClickAndNeverOpens) {
        Show("Pick");

        const Json clicked = Call("ui_click", Click("Frozen"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        EXPECT_EQ(Member(*Member(clicked, "result"), "action"), nullptr);
        Show("Pick");
        const Json tree = Call("ui_tree");
        const Json* frozen = FindElement(*FindCanvas(*Member(tree, "result"), "Panel"), "Frozen");
        ASSERT_NE(frozen, nullptr);
        EXPECT_TRUE(BoolMember(*frozen, "disabled"));
        EXPECT_FALSE(BoolMember(*frozen, "open"));
        EXPECT_FALSE(BoolMember(*FindElement(*frozen, "Only"), "visible"));
    }

    TEST_F(UIControlApiTest, ADropdownCanBeDrivenEndToEnd) {
        Show("Pick");
        Recorder recorder;

        ASSERT_TRUE(IsOk(Call("ui_click", Click("Level"))));
        Show("Pick");  // the "step"
        const Json open = Call("ui_tree");
        ASSERT_TRUE(IsOk(open));
        EXPECT_TRUE(BoolMember(*FindElement(*FindCanvas(*Member(open, "result"), "Panel"), "Normal"), "visible"));
        const Json picked = Call("ui_click", Click("Normal"));
        ASSERT_TRUE(IsOk(picked)) << ErrorMessage(picked);
        EXPECT_EQ(StringMember(*Member(picked, "result"), "value"), "normal");
        Show("Pick");

        EXPECT_EQ(recorder.Count("UIControlApiLevel"), 1);
        EXPECT_EQ(recorder.LastValue(), "normal");
    }

    // ---- the progress bar ------------------------------------------------------------------------

    TEST_F(UIControlApiTest, ATreeShowsTheFieldsOfTheProgressBar) {
        CoreManager::GetUIVars().Set("uiApiLoad"_intern, "25");
        Show("Values");

        const Json tree = Call("ui_tree");

        ASSERT_TRUE(IsOk(tree)) << ErrorMessage(tree);
        const Json* canvas = FindCanvas(*Member(tree, "result"), "Settings");
        ASSERT_NE(canvas, nullptr);

        const Json* loading = FindElement(*canvas, "Loading");
        ASSERT_NE(loading, nullptr);
        EXPECT_EQ(StringMember(*loading, "type"), "UIProgressBar");
        EXPECT_EQ(StringMember(*loading, "bind"), "uiApiLoad");
        EXPECT_NEAR(NumberMember(*loading, "minValue"), 0.0, kTolerance);
        EXPECT_NEAR(NumberMember(*loading, "maxValue"), 100.0, kTolerance);
        EXPECT_NEAR(NumberMember(*loading, "fraction"), 0.25, kTolerance);
        EXPECT_FALSE(BoolMember(*loading, "interactive"));
        EXPECT_EQ(Member(*loading, "text"), nullptr);
        EXPECT_EQ(Member(*loading, "action"), nullptr);
        EXPECT_EQ(Member(*loading, "checked"), nullptr);
    }

    TEST_F(UIControlApiTest, TheFractionOfAProgressBarFollowsTheVariableBetweenTwoTreeCallsWithNoDraw) {
        Show("Values");
        const Json before = Call("ui_tree");
        ASSERT_TRUE(IsOk(before));
        const Json* canvas = FindCanvas(*Member(before, "result"), "Settings");
        EXPECT_NEAR(NumberMember(*FindElement(*canvas, "Loading"), "fraction"), 0.0, kTolerance);

        CoreManager::GetUIVars().Set("uiApiLoad"_intern, "50");
        const Json after = Call("ui_tree");

        ASSERT_TRUE(IsOk(after));
        canvas = FindCanvas(*Member(after, "result"), "Settings");
        EXPECT_NEAR(NumberMember(*FindElement(*canvas, "Loading"), "fraction"), 0.5, kTolerance);

        CoreManager::GetUIVars().Set("uiApiLoad"_intern, "not a number");
        const Json bad = Call("ui_tree");
        EXPECT_NEAR(NumberMember(*FindElement(*FindCanvas(*Member(bad, "result"), "Settings"), "Loading"), "fraction"), 0.0, kTolerance);
    }

    TEST_F(UIControlApiTest, ASliderWithoutAtIsPressedInTheMiddleAndAnIntegerAtIsAccepted) {
        Show("Values");
        Recorder recorder;

        const Json middle = Call("ui_click", Click("CVolume"));
        ASSERT_TRUE(IsOk(middle)) << ErrorMessage(middle);
        EXPECT_EQ(StringMember(*Member(middle, "result"), "value"), "0.5");

        Json one = Click("CVolume");
        one["at"] = 1;  // an integer in the JSON text
        const Json end = Call("ui_click", one);
        ASSERT_TRUE(IsOk(end)) << ErrorMessage(end);
        EXPECT_EQ(StringMember(*Member(end, "result"), "value"), "1");
    }

    TEST_F(UIControlApiTest, AClientSetsASliderEndToEnd) {
        Show("Values");
        Json params = Click("CVolume");
        params["at"] = 1.0;
        ASSERT_TRUE(IsOk(Call("ui_click", params)));

        const Json unchanged = Call("ui_tree");
        EXPECT_NEAR(NumberMember(*FindElement(*FindCanvas(*Member(unchanged, "result"), "Settings"), "CVolume"), "value"), 0.0, kTolerance) << "the game has not answered";

        CoreManager::GetUIVars().Set("uiApiCVolume"_intern, "1");
        const Json answered = Call("ui_tree");
        const Json* volume = FindElement(*FindCanvas(*Member(answered, "result"), "Settings"), "CVolume");
        EXPECT_NEAR(NumberMember(*volume, "value"), 1.0, kTolerance);
        EXPECT_NEAR(NumberMember(*volume, "fraction"), 1.0, kTolerance);
    }

    TEST_F(UIControlApiTest, AnAtOutsideZeroToOneOrNotANumberIsInvalidAndNothingIsPressed) {
        Show("Values");
        Recorder recorder;

        for (const char* json : {"1.5", "-0.1", "\"0.5\"", "null", "true", "[0.5]", "2", "-1"}) {
            Json params = Click("CVolume");
            params["at"] = *ParseJson(json);
            const Json response = Call("ui_click", params);

            EXPECT_FALSE(IsOk(response)) << json;
            EXPECT_EQ(ErrorCode(response), "invalid_params") << json;
            EXPECT_NE(ErrorMessage(response).find("'at'"), std::string::npos) << json << ": " << ErrorMessage(response);
        }
        // Checked before the scene and the element are looked at
        Json missing = Click("NoSuchElementAtAll");
        missing["at"] = 3;
        EXPECT_EQ(ErrorCode(Call("ui_click", missing)), "invalid_params");

        EXPECT_EQ(recorder.Total(), 0u);
        EXPECT_EQ(recorder.downs, 0);
    }

    TEST_F(UIControlApiTest, AtLeavesTheLookupOfAButtonAndTheHitTestWhereThePressLands) {
        Show("Main");
        Recorder recorder;

        // Ok is 80 x 40 at (122, 62): at 0 and at 1 are both on it
        for (double at : {0.0, 1.0}) {
            Json params = Click("Ok");
            params["at"] = at;
            params["canvas"] = std::string("Hud");
            const Json response = Call("ui_click", params);
            ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
            EXPECT_TRUE(BoolMember(*Member(response, "result"), "consumed"));
        }
        EXPECT_EQ(recorder.Count("UIControlApiOk"), 2);

        // Under is covered in the middle, and so at every point of its width up to the cover's edge: at 1 (x = 580) is under Cover (490..590) as well
        Json covered = Click("Under");
        covered["at"] = 1.0;
        EXPECT_EQ(ErrorCode(Call("ui_click", covered)), "not_clickable");
    }

    // ---- the composed toggle and slider ------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, ATreeShowsTheFieldsOfAComposedToggleAndSliderAsOfTheClasses) {
        CoreManager::GetUIVars().Set("uiApiCSound"_intern, "on");
        CoreManager::GetUIVars().Set("uiApiCVolume"_intern, "0.3");
        Show("Values");

        const Json tree = Call("ui_tree");

        ASSERT_TRUE(IsOk(tree)) << ErrorMessage(tree);
        const Json* canvas = FindCanvas(*Member(tree, "result"), "Settings");
        ASSERT_NE(canvas, nullptr);

        const Json* sound = FindElement(*canvas, "CSound");
        ASSERT_NE(sound, nullptr);
        EXPECT_EQ(StringMember(*sound, "type"), "UIElement");
        EXPECT_EQ(StringMember(*sound, "bind"), "uiApiCSound");
        EXPECT_EQ(StringMember(*sound, "onValue"), "on");
        EXPECT_EQ(StringMember(*sound, "offValue"), "off");
        EXPECT_TRUE(BoolMember(*sound, "checked"));
        EXPECT_EQ(StringMember(*sound, "action"), "UIControlApiCSound") << "the toggle's action replaces the empty one of the clicker";
        EXPECT_FALSE(BoolMember(*sound, "disabled"));
        EXPECT_EQ(StringMember(*sound, "text"), "Sound") << "the caption child";
        EXPECT_TRUE(BoolMember(*sound, "interactive"));
        ExpectRect(Member(*sound, "rect"), 100.0f, 500.0f, 300.0f, 56.0f);
        const Json* types = Member(*sound, "components");
        ASSERT_NE(types, nullptr);
        EXPECT_EQ(types->size(), 2u);

        const Json* locked = FindElement(*canvas, "CLocked");
        ASSERT_NE(locked, nullptr);
        EXPECT_TRUE(BoolMember(*locked, "disabled"));
        EXPECT_FALSE(BoolMember(*locked, "checked"));
        EXPECT_EQ(StringMember(*locked, "onValue"), "true") << "the defaults of the component";
        EXPECT_EQ(StringMember(*locked, "offValue"), "false");

        const Json* volume = FindElement(*canvas, "CVolume");
        ASSERT_NE(volume, nullptr);
        EXPECT_EQ(StringMember(*volume, "type"), "UIElement");
        EXPECT_EQ(StringMember(*volume, "bind"), "uiApiCVolume");
        EXPECT_NEAR(NumberMember(*volume, "minValue"), 0.0, kTolerance);
        EXPECT_NEAR(NumberMember(*volume, "maxValue"), 1.0, kTolerance);
        EXPECT_NEAR(NumberMember(*volume, "step"), 0.1, kTolerance);
        EXPECT_NEAR(NumberMember(*volume, "value"), 0.3, kTolerance);
        EXPECT_NEAR(NumberMember(*volume, "fraction"), 0.3, kTolerance);
        EXPECT_EQ(StringMember(*volume, "action"), "UIControlApiCVolume");
        EXPECT_FALSE(BoolMember(*volume, "disabled"));
        EXPECT_EQ(Member(*volume, "text"), nullptr);
        EXPECT_TRUE(BoolMember(*volume, "interactive"));
        EXPECT_TRUE(BoolMember(*FindElement(*canvas, "CQuiet"), "disabled"));
    }

    TEST_F(UIControlApiTest, TheComposedValuesFollowTheVariablesBetweenTwoTreeCallsWithNoDraw) {
        Show("Values");
        const Json before = Call("ui_tree");
        ASSERT_TRUE(IsOk(before));
        const Json* canvas = FindCanvas(*Member(before, "result"), "Settings");
        EXPECT_FALSE(BoolMember(*FindElement(*canvas, "CSound"), "checked"));
        EXPECT_NEAR(NumberMember(*FindElement(*canvas, "CVolume"), "value"), 0.0, kTolerance);

        CoreManager::GetUIVars().Set("uiApiCSound"_intern, "on");
        CoreManager::GetUIVars().Set("uiApiCVolume"_intern, "0.7");
        const Json after = Call("ui_tree");

        ASSERT_TRUE(IsOk(after));
        canvas = FindCanvas(*Member(after, "result"), "Settings");
        EXPECT_TRUE(BoolMember(*FindElement(*canvas, "CSound"), "checked"));
        EXPECT_NEAR(NumberMember(*FindElement(*canvas, "CVolume"), "value"), 0.7, kTolerance);
        EXPECT_NEAR(NumberMember(*FindElement(*canvas, "CVolume"), "fraction"), 0.7, kTolerance);
    }

    TEST_F(UIControlApiTest, AClickOnAComposedToggleAnswersWithTheValueItAsksForAndTheListenerGetsIt) {
        Show("Values");
        Recorder recorder;

        const Json turnOn = Call("ui_click", Click("CSound"));

        ASSERT_TRUE(IsOk(turnOn)) << ErrorMessage(turnOn);
        EXPECT_EQ(StringMember(*Member(turnOn, "result"), "action"), "UIControlApiCSound");
        EXPECT_EQ(StringMember(*Member(turnOn, "result"), "value"), "on");
        EXPECT_EQ(recorder.Count("UIControlApiCSound"), 1);
        EXPECT_EQ(recorder.LastValue(), "on");

        CoreManager::GetUIVars().Set("uiApiCSound"_intern, "on");
        const Json turnOff = Call("ui_click", Click("CSound"));

        ASSERT_TRUE(IsOk(turnOff));
        EXPECT_EQ(StringMember(*Member(turnOff, "result"), "value"), "off");
        EXPECT_EQ(recorder.Count("UIControlApiCSound"), 2);
        EXPECT_EQ(recorder.LastValue(), "off");

        const Json tree = Call("ui_tree");
        EXPECT_TRUE(BoolMember(*FindElement(*FindCanvas(*Member(tree, "result"), "Settings"), "CSound"), "checked")) << "a click changes nothing by itself";
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

        const Json response = Call("ui_click", Click("CLocked"));

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        EXPECT_TRUE(BoolMember(*Member(response, "result"), "consumed"));
        EXPECT_EQ(StringMember(*Member(response, "result"), "action"), "UIControlApiCLocked");
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
            Json params = Click("CVolume");
            params["at"] = item.at;
            const Json response = Call("ui_click", params);

            ASSERT_TRUE(IsOk(response)) << "at " << item.at << ": " << ErrorMessage(response);
            const Json& result = *Member(response, "result");
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

        const Json response = Call("ui_click", Click("CQuiet"));

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        EXPECT_TRUE(BoolMember(*Member(response, "result"), "consumed"));
        EXPECT_EQ(StringMember(*Member(response, "result"), "action"), "UIControlApiCQuiet");
        EXPECT_EQ(StringMember(*Member(response, "result"), "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AClickElsewhereWhileTheListIsOpenAnswersNothingForAComposedToggleAndSlider) {
        for (const char* name : {"PickToggle", "PickSlide"}) {
            Show("Pick");
            ASSERT_TRUE(IsOk(Call("ui_click", Click("Level"))));
            Show("Pick");
            Recorder recorder;

            const Json clicked = Call("ui_click", Click(name));

            ASSERT_TRUE(IsOk(clicked)) << name << ": " << ErrorMessage(clicked);
            EXPECT_TRUE(BoolMember(*Member(clicked, "result"), "consumed")) << name;
            EXPECT_EQ(StringMember(*Member(clicked, "result"), "action"), "") << name;
            EXPECT_EQ(StringMember(*Member(clicked, "result"), "value"), "") << name;
            EXPECT_EQ(recorder.Total(), 0u) << name << ": the press only closed the list";
        }
    }

    TEST_F(UIControlApiTest, AProgressBarCannotBeClicked) {
        Show("Values");
        Recorder recorder;

        const Json response = Call("ui_click", Click("Loading"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("not interactive"), std::string::npos) << ErrorMessage(response);
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AButtonAndASegmentStillAnswerAsBeforeWhenAtIsGiven) {
        Show("Choice");
        Recorder recorder;
        Json params = Click("Right");
        params["at"] = 0.5;

        const Json response = Call("ui_click", params);

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        EXPECT_EQ(StringMember(*Member(response, "result"), "action"), "UIControlApiSide");
        EXPECT_EQ(StringMember(*Member(response, "result"), "value"), "right");
        EXPECT_EQ(recorder.LastValue(), "right");
        // x of the middle of Right (252..402 -> 327), unchanged from the click without `at`
        EXPECT_NEAR(NumberMember(*Member(response, "result"), "x"), 327.0, kTolerance);
    }

    TEST_F(UIControlApiTest, ASliderInASceneViewCanvasIsPressedAtTheScaledPoint) {
        Show("Fit", 1000.0f, 600.0f);  // scale 2, offset (100, 0)
        Recorder recorder;
        Json params = Click("Slide");
        params["at"] = 0.5;

        const Json response = Call("ui_click", params);

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        EXPECT_NEAR(NumberMember(*Member(response, "result"), "x"), 320.0, kTolerance);  // canvas x 110
        EXPECT_NEAR(NumberMember(*Member(response, "result"), "y"), 240.0, kTolerance);  // canvas y 120
        EXPECT_EQ(StringMember(*Member(response, "result"), "value"), "50");
        EXPECT_EQ(recorder.LastValue(), "50");
    }

    // ---- ui_click ---------------------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, AClickPressesAButtonLikeAUserWould) {
        Show("Main");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("Ok"));  // the Hud one: the first of the two named Ok in the scene order

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        const Json& result = *Member(clicked, "result");
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

        const Json response = Call("ui_click", Click("Go"));

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json& result = *Member(response, "result");
        EXPECT_NEAR(NumberMember(result, "x"), 170.0, kTolerance);  // (10 + 25) * 2 + 100
        EXPECT_NEAR(NumberMember(result, "y"), 60.0, kTolerance);   // (10 + 20) * 2
        EXPECT_EQ(recorder.Count("UIControlApiGo"), 1);
    }

    TEST_F(UIControlApiTest, TheClickAndTheOutputRectFollowTheViewTheSceneSwitchedTo) {
        Show("Views", 800.0f, 600.0f);
        const Json landscape = Call("ui_tree");
        Show("Views", 600.0f, 800.0f);
        const Json portrait = Call("ui_tree");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("Go"));

        ASSERT_TRUE(IsOk(landscape) && IsOk(portrait) && IsOk(clicked));
        const Json* before = FindElement(*FindCanvas(*Member(landscape, "result"), "View"), "Go");
        const Json* after = FindElement(*FindCanvas(*Member(portrait, "result"), "View"), "Go");
        ASSERT_TRUE(before != nullptr && after != nullptr);
        ExpectRect(Member(*before, "outputRect"), 20.0f, 20.0f, 100.0f, 80.0f);
        ExpectRect(Member(*after, "rect"), 125.0f, 350.0f, 50.0f, 40.0f);
        ExpectRect(Member(*after, "outputRect"), 250.0f, 700.0f, 100.0f, 80.0f);
        const Json& result = *Member(clicked, "result");
        EXPECT_NEAR(NumberMember(result, "x"), 300.0, kTolerance);
        EXPECT_NEAR(NumberMember(result, "y"), 740.0, kTolerance);
        EXPECT_EQ(recorder.Count("UIControlApiViewGo"), 1);
    }

    TEST_F(UIControlApiTest, ASceneViewCanvasReportsTheActiveViewAndOtherCanvasesReportNone) {
        Show("Views", 800.0f, 600.0f);
        const Json landscape = Call("ui_tree");
        Show("Views", 600.0f, 800.0f);
        const Json portrait = Call("ui_tree");

        ASSERT_TRUE(IsOk(landscape) && IsOk(portrait));
        const Json* before = Member(*FindCanvas(*Member(landscape, "result"), "View"), "view");
        const Json* after = Member(*FindCanvas(*Member(portrait, "result"), "View"), "view");
        ASSERT_TRUE(before != nullptr && after != nullptr);
        EXPECT_EQ(StringMember(*before, "name"), "");
        EXPECT_NEAR(NumberMember(*before, "width"), 400.0, kTolerance);
        EXPECT_NEAR(NumberMember(*before, "height"), 300.0, kTolerance);
        EXPECT_EQ(StringMember(*after, "name"), "Portrait");
        EXPECT_NEAR(NumberMember(*after, "width"), 300.0, kTolerance);
        EXPECT_NEAR(NumberMember(*after, "height"), 400.0, kTolerance);

        Show("Main");
        const Json main = Call("ui_tree");
        ASSERT_TRUE(IsOk(main));
        EXPECT_EQ(Member(*FindCanvas(*Member(main, "result"), "Hud"), "view"), nullptr);
    }

    TEST_F(UIControlApiTest, ASceneWithoutViewVariantsReportsItsOneView) {
        Show("Fit", 1000.0f, 600.0f);

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response));
        const Json* view = Member(*FindCanvas(*Member(response, "result"), "View"), "view");
        ASSERT_NE(view, nullptr);
        EXPECT_EQ(StringMember(*view, "name"), "");
        EXPECT_NEAR(NumberMember(*view, "width"), 400.0, kTolerance);
        EXPECT_NEAR(NumberMember(*view, "height"), 300.0, kTolerance);
    }

    TEST_F(UIControlApiTest, TheDrawnButtonIsWhereTheTreeSaysInTheActiveView) {
        Show("Views", 600.0f, 800.0f);

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response));
        const Json* go = FindElement(*FindCanvas(*Member(response, "result"), "View"), "Go");
        ASSERT_NE(go, nullptr);
        const Json& output = *Member(*go, "outputRect");
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
        Json params = Click("Ok");
        params["canvas"] = std::string("Debug");

        const Json response = Call("ui_click", params);

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        EXPECT_EQ(StringMember(*Member(response, "result"), "canvas"), "Debug");
        EXPECT_EQ(recorder.Count("UIControlApiDebugOk"), 1);
        EXPECT_EQ(recorder.Count("UIControlApiOk"), 0);
    }

    TEST_F(UIControlApiTest, ADisabledButtonTakesTheClickAndDoesNothingWithIt) {
        Show("Main");
        Recorder recorder;

        const Json response = Call("ui_click", Click("Off"));

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        EXPECT_TRUE(BoolMember(*Member(response, "result"), "consumed"));
        EXPECT_EQ(recorder.Total(), 0u);
        EXPECT_EQ(recorder.downs, 0);
    }

    TEST_F(UIControlApiTest, AModalCanvasAboveTakesTheClick) {
        auto menu = Canvas("Main", "Menu");
        menu->_visible = true;
        Show("Main");
        Recorder recorder;

        const Json response = Call("ui_click", Click("Ok"));

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        EXPECT_TRUE(BoolMember(*Member(response, "result"), "consumed"));
        EXPECT_EQ(recorder.Total(), 0u) << "the modal canvas took the click, as it would take a user's";
        EXPECT_EQ(recorder.downs, 0);
    }

    TEST_F(UIControlApiTest, AnElementThatDoesNotExistIsNotFoundAndItsNameIsNotInterned) {
        Show("Main");
        constexpr const char* kUnknown = "ui_click_no_such_element_77c1d3";
        ASSERT_FALSE(PoolString::Find(kUnknown).has_value());

        const Json response = Call("ui_click", Click(kUnknown));

        EXPECT_FALSE(IsOk(response));
        EXPECT_EQ(ErrorCode(response), "not_found");
        EXPECT_FALSE(PoolString::Find(kUnknown).has_value()) << "text from a client must not grow the pool";
    }

    TEST_F(UIControlApiTest, ACanvasNameThatNoNodeHasIsNotFound) {
        Show("Main");
        Json params = Click("Ok");
        params["canvas"] = std::string("ui_click_no_such_node_5a90e2");

        const Json response = Call("ui_click", params);

        EXPECT_EQ(ErrorCode(response), "not_found");
        EXPECT_FALSE(PoolString::Find("ui_click_no_such_node_5a90e2").has_value());
    }

    TEST_F(UIControlApiTest, AnElementThatIsNotInteractiveCannotBeClicked) {
        Show("Main");
        Recorder recorder;

        const Json response = Call("ui_click", Click("Note"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("not interactive"), std::string::npos) << ErrorMessage(response);
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AHiddenButtonCannotBeClicked) {
        auto hud = Canvas("Main", "Hud");
        ASSERT_TRUE(hud->SetElementVisible("Off"_intern, false));
        Show("Main");

        const Json response = Call("ui_click", Click("Off"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("hidden"), std::string::npos) << ErrorMessage(response);
    }

    TEST_F(UIControlApiTest, AButtonCoveredByAnotherElementCannotBeClicked) {
        Show("Main");
        Recorder recorder;

        const Json response = Call("ui_click", Click("Under"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("covered by 'Cover'"), std::string::npos) << ErrorMessage(response);
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AButtonOfACanvasThatTakesNoInputCannotBeClicked) {
        auto hud = Canvas("Main", "Hud");
        hud->_inputEnabled = false;
        Show("Main");

        const Json response = Call("ui_click", Click("Off"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("canvas 'Hud'"), std::string::npos) << ErrorMessage(response);
    }

    TEST_F(UIControlApiTest, AButtonBeforeTheFirstDrawAsksForAFrame) {
        _manager->SetActiveScene(PoolString::Intern("Main"));

        const Json response = Call("ui_click", Click("Ok"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("step a frame"), std::string::npos) << ErrorMessage(response);
    }

    TEST_F(UIControlApiTest, TheClickHasToNameAnElement) {
        Show("Main");

        Json noName = Json::object();
        EXPECT_EQ(ErrorCode(Call("ui_click", noName)), "invalid_params");

        Json emptyName = Json::object();
        emptyName["name"] = std::string();
        EXPECT_EQ(ErrorCode(Call("ui_click", emptyName)), "invalid_params");

        Json numberName = Json::object();
        numberName["name"] = 7;
        EXPECT_EQ(ErrorCode(Call("ui_click", numberName)), "invalid_params");

        Json badCanvas = Click("Ok");
        badCanvas["canvas"] = 7;
        EXPECT_EQ(ErrorCode(Call("ui_click", badCanvas)), "invalid_params");
    }

    // ---- a button made of components -----------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, AComposedButtonTellsItsComponentsAndTheFieldsOfAButton) {
        CoreManager::GetUIVars().Set("user"_intern, "Ann");
        Show("Composed");
        Canvas("Composed", "Bar")->GetRoot().FindByName(PoolString::Intern("Play"))->selected = true;

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* bar = FindCanvas(*Member(response, "result"), "Bar");
        ASSERT_NE(bar, nullptr);
        const Json* play = FindElement(*bar, "Play");
        ASSERT_NE(play, nullptr);
        EXPECT_EQ(StringMember(*play, "type"), "UIElement");
        EXPECT_TRUE(BoolMember(*play, "interactive")) << "a clicker makes it one";
        const Json* components = Member(*play, "components");
        ASSERT_NE(components, nullptr);
        ASSERT_EQ(components->size(), 4u);
        EXPECT_EQ((*components)[0], "UIInputClicker") << "in the order of the list";
        EXPECT_EQ((*components)[1], "UIImageComponent");
        EXPECT_EQ((*components)[2], "UITextComponent");
        EXPECT_EQ((*components)[3], "UITooltipComponent");
        EXPECT_EQ(StringMember(*play, "text"), "Play Ann") << "as shown, variables replaced";
        EXPECT_EQ(StringMember(*play, "action"), "UIControlApiPlay");
        EXPECT_EQ(StringMember(*play, "value"), "easy");
        EXPECT_FALSE(BoolMember(*play, "disabled"));
        EXPECT_TRUE(BoolMember(*play, "selected"));
        EXPECT_EQ(StringMember(*play, "icon"), "PlayIcon");
        EXPECT_EQ(StringMember(*play, "tooltip"), "Start {#tip.play}") << "the source text";
    }

    TEST_F(UIControlApiTest, AButtonMadeOfChildrenAnswersTheTextAndTheIconOfItsChildren) {
        CoreManager::GetUIVars().Set("user"_intern, "Ann");
        Show("Composed");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* bar = FindCanvas(*Member(response, "result"), "Bar");
        ASSERT_NE(bar, nullptr);

        const Json* pad = FindElement(*bar, "Pad");
        ASSERT_NE(pad, nullptr);
        EXPECT_EQ(StringMember(*pad, "icon"), "PadSprite") << "the icon-only form: the icon child";
        EXPECT_FALSE(pad->contains("text"));
        EXPECT_EQ(StringMember(*pad, "action"), "UIControlApiPad");
        const Json* padIcon = FindElement(*pad, "PadIcon");
        ASSERT_NE(padIcon, nullptr) << "the children are still listed";
        EXPECT_EQ(StringMember(*padIcon, "icon"), "PadSprite");
        EXPECT_FALSE(padIcon->contains("action"));
        EXPECT_FALSE(BoolMember(*padIcon, "interactive"));

        const Json* mix = FindElement(*bar, "Mix");
        ASSERT_NE(mix, nullptr);
        EXPECT_EQ(StringMember(*mix, "text"), "Mix Ann") << "found below a plain child";
        EXPECT_EQ(StringMember(*mix, "icon"), "MixSprite");
        EXPECT_EQ(StringMember(*mix, "value"), "m");
        EXPECT_EQ(StringMember(*mix, "tooltip"), "Mix tip");
        EXPECT_FALSE(BoolMember(*mix, "disabled"));
        EXPECT_EQ(StringMember(*FindElement(*mix, "MixCaption"), "text"), "Mix Ann");
        EXPECT_EQ(StringMember(*FindElement(*mix, "MixIcon"), "icon"), "MixSprite");
        EXPECT_FALSE(FindElement(*mix, "MixCaption")->contains("icon"));
        EXPECT_FALSE(FindElement(*mix, "MixIcon")->contains("text"));
    }

    TEST_F(UIControlApiTest, AClickerWithNothingBelowItAnswersNeitherTextNorIcon) {
        Show("Composed");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* bar = FindCanvas(*Member(response, "result"), "Bar");
        ASSERT_NE(bar, nullptr);
        const Json* hollow = FindElement(*bar, "Hollow");
        ASSERT_NE(hollow, nullptr);
        EXPECT_EQ(StringMember(*hollow, "action"), "UIControlApiHollow");
        EXPECT_FALSE(hollow->contains("text"));
        EXPECT_FALSE(hollow->contains("icon"));
    }

    TEST_F(UIControlApiTest, ANestedClickersTextAndIconAreNotTakenByItsParent) {
        Show("Composed");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* bar = FindCanvas(*Member(response, "result"), "Bar");
        ASSERT_NE(bar, nullptr);
        const Json* outer = FindElement(*bar, "Outer");
        ASSERT_NE(outer, nullptr);
        EXPECT_FALSE(outer->contains("text"));
        EXPECT_FALSE(outer->contains("icon"));
        const Json* inner = FindElement(*outer, "Inner");
        ASSERT_NE(inner, nullptr);
        EXPECT_EQ(StringMember(*inner, "text"), "Inner");
        EXPECT_EQ(StringMember(*inner, "icon"), "InnerSprite");
        EXPECT_EQ(StringMember(*inner, "action"), "UIControlApiInner");
    }

    TEST_F(UIControlApiTest, AClickOnAButtonMadeOfChildrenFindsTheClickerNotTheIcon) {
        Show("Composed");
        Recorder recorder;

        const Json response = Call("ui_click", Click("Mix"));

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json& result = *Member(response, "result");
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiMix");
        EXPECT_EQ(StringMember(result, "value"), "m");
        EXPECT_EQ(recorder.Count("UIControlApiMix"), 1);

        EXPECT_EQ(ErrorCode(Call("ui_click", Click("MixIcon"))), "not_clickable") << "the icon child is no button";
    }

    TEST_F(UIControlApiTest, AComposedButtonIsDisabledByItsClickerAndByItsGroup) {
        Show("Composed");
        UICanvasComponent& bar = *Canvas("Composed", "Bar");
        bar.GetRoot().FindByName(PoolString::Intern("Wide"))->groupDisabled = true;

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* node = FindCanvas(*Member(response, "result"), "Bar");
        ASSERT_NE(node, nullptr);
        EXPECT_TRUE(BoolMember(*FindElement(*node, "Dead"), "disabled"));
        EXPECT_TRUE(BoolMember(*FindElement(*node, "Wide"), "disabled"));
        EXPECT_FALSE(BoolMember(*FindElement(*node, "Play"), "disabled"));
    }

    TEST_F(UIControlApiTest, AnElementWithNoClickerTellsOnlyWhatItHas) {
        Show("Composed");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* bar = FindCanvas(*Member(response, "result"), "Bar");
        ASSERT_NE(bar, nullptr);
        const Json* badge = FindElement(*bar, "Badge");
        ASSERT_NE(badge, nullptr);
        EXPECT_FALSE(BoolMember(*badge, "interactive"));
        EXPECT_EQ(StringMember(*badge, "icon"), "BadgeIcon");
        EXPECT_EQ(StringMember(*badge, "tooltip"), "Badge");
        EXPECT_FALSE(badge->contains("action"));
        EXPECT_FALSE(badge->contains("disabled"));
        EXPECT_FALSE(badge->contains("selected"));
        EXPECT_FALSE(badge->contains("text"));

        const Json* bare = FindElement(*bar, "Bare");
        ASSERT_NE(bare, nullptr);
        EXPECT_FALSE(bare->contains("components")) << "the entry of an element with none is as before";
        EXPECT_FALSE(bare->contains("icon"));
        EXPECT_FALSE(bare->contains("tooltip"));
    }

    TEST_F(UIControlApiTest, AClickOnAComposedButtonEmitsAndAnswersItsActionAndValue) {
        Show("Composed");
        Recorder recorder;

        const Json response = Call("ui_click", Click("Play"));

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json& result = *Member(response, "result");
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

        const Json response = Call("ui_click", Click("Dead"));

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json& result = *Member(response, "result");
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "");
        EXPECT_EQ(StringMember(result, "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AComposedButtonOfADisabledGroupEmitsNothingEither) {
        Show("Composed");
        Canvas("Composed", "Bar")->GetRoot().FindByName(PoolString::Intern("Wide"))->groupDisabled = true;
        Recorder recorder;

        const Json response = Call("ui_click", Click("Wide"));

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        EXPECT_EQ(StringMember(*Member(response, "result"), "action"), "");
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AComposedButtonCoveredByAnotherElementIsNotClickable) {
        Show("Composed");
        Recorder recorder;

        const Json response = Call("ui_click", Click("Shade"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("Cover"), std::string::npos) << ErrorMessage(response);
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AnElementWithNoClickerIsNotClickable) {
        Show("Composed");

        const Json response = Call("ui_click", Click("Badge"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("not interactive"), std::string::npos) << ErrorMessage(response);
    }

    TEST_F(UIControlApiTest, AtPicksTheHorizontalPointOfAComposedButton) {
        Show("Composed");
        Recorder recorder;
        Json params = Click("Wide");
        params["at"] = 0.0;

        const Json response = Call("ui_click", params);

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json& result = *Member(response, "result");
        EXPECT_NEAR(NumberMember(result, "x"), 10.0f, 1.0f) << "the left edge, a hair inside";
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiWide");
        EXPECT_EQ(recorder.Count("UIControlApiWide"), 1);
    }

    // ---- choice groups made of components: a segment or an option made of components ----------------------

    TEST_F(UIControlApiTest, ATreeShowsTheSegmentsOfAComposedGroupWithTheFieldsOfAButton) {
        CoreManager::GetUIVars().Set("uiApiCSide"_intern, "right");
        Show("Choices");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* side = FindElement(*FindCanvas(*Member(response, "result"), "Panel"), "CSide");
        ASSERT_NE(side, nullptr);
        EXPECT_EQ(StringMember(*side, "type"), "UISegmented");
        EXPECT_EQ(StringMember(*side, "bind"), "uiApiCSide");
        EXPECT_EQ(StringMember(*side, "selectedValue"), "right");
        EXPECT_FALSE(BoolMember(*side, "disabled"));

        const Json* left = FindElement(*side, "CLeft");
        const Json* right = FindElement(*side, "CRight");
        const Json* gone = FindElement(*side, "CGone");
        ASSERT_TRUE(left != nullptr && right != nullptr && gone != nullptr);
        EXPECT_EQ(StringMember(*left, "text"), "Left");
        EXPECT_EQ(StringMember(*left, "action"), "UIControlApiCSide");
        EXPECT_EQ(StringMember(*left, "value"), "left");
        EXPECT_FALSE(BoolMember(*left, "selected"));
        EXPECT_FALSE(BoolMember(*left, "disabled"));
        EXPECT_TRUE(BoolMember(*left, "interactive"));
        ExpectRect(Member(*left, "rect"), 100.0f, 100.0f, 132.0f, 56.0f);

        EXPECT_TRUE(BoolMember(*right, "selected"));
        EXPECT_EQ(StringMember(*right, "text"), "Right") << "the caption child";
        EXPECT_EQ(StringMember(*right, "icon"), "SideIcon") << "the icon child";
        EXPECT_EQ(StringMember(*right, "tooltip"), "The right one");
        EXPECT_EQ(StringMember(*right, "value"), "right");

        EXPECT_TRUE(BoolMember(*gone, "disabled"));
        EXPECT_FALSE(BoolMember(*gone, "selected"));
    }

    TEST_F(UIControlApiTest, TheSelectionOfAComposedGroupFollowsTheVariableBetweenTwoTreeCalls) {
        CoreManager::GetUIVars().Set("uiApiCSide"_intern, "left");
        Show("Choices");
        const Json first = Call("ui_tree");
        EXPECT_TRUE(BoolMember(*FindElement(*FindCanvas(*Member(first, "result"), "Panel"), "CLeft"), "selected"));

        CoreManager::GetUIVars().Set("uiApiCSide"_intern, "right");  // no draw in between
        const Json second = Call("ui_tree");
        const Json* panel = FindCanvas(*Member(second, "result"), "Panel");
        EXPECT_FALSE(BoolMember(*FindElement(*panel, "CLeft"), "selected"));
        EXPECT_TRUE(BoolMember(*FindElement(*panel, "CRight"), "selected"));
        EXPECT_EQ(StringMember(*FindElement(*panel, "CSide"), "selectedValue"), "right");
    }

    TEST_F(UIControlApiTest, AClickOnAComposedSegmentAnswersWithItsActionAndValueAndTheListenerGetsThem) {
        Show("Choices");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("CRight"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        const Json& result = *Member(clicked, "result");
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiCSide");
        EXPECT_EQ(StringMember(result, "value"), "right");
        EXPECT_EQ(recorder.Count("UIControlApiCSide"), 1);
        EXPECT_EQ(recorder.LastValue(), "right");
    }

    TEST_F(UIControlApiTest, AClickOnADisabledComposedSegmentOrOneOfADisabledGroupEmitsNothing) {
        Show("Choices");
        Recorder recorder;

        const Json gone = Call("ui_click", Click("CGone"));
        ASSERT_TRUE(IsOk(gone)) << ErrorMessage(gone);
        EXPECT_TRUE(BoolMember(*Member(gone, "result"), "consumed"));
        EXPECT_EQ(StringMember(*Member(gone, "result"), "action"), "");
        EXPECT_EQ(StringMember(*Member(gone, "result"), "value"), "");

        auto canvas = Canvas("Choices", "Panel");
        ASSERT_TRUE(canvas);
        canvas->GetRoot().FindByName("CSide"_intern)->Cast<UISegmented>()->disabled = true;
        Show("Choices");
        const Json left = Call("ui_click", Click("CLeft"));
        ASSERT_TRUE(IsOk(left)) << ErrorMessage(left);
        EXPECT_EQ(StringMember(*Member(left, "result"), "action"), "");
        EXPECT_EQ(StringMember(*Member(left, "result"), "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AGroupOfAButtonAndAComposedSegmentIsDescribedAndClickedAlike) {
        CoreManager::GetUIVars().Set("uiApiMix"_intern, "composed");
        Show("Choices");
        Recorder recorder;

        const Json tree = Call("ui_tree");
        const Json* mix = FindElement(*FindCanvas(*Member(tree, "result"), "Panel"), "Mix");
        ASSERT_NE(mix, nullptr);
        EXPECT_EQ(StringMember(*mix, "selectedValue"), "composed");
        const Json* button = FindElement(*mix, "MixButton");
        const Json* composed = FindElement(*mix, "MixComposed");
        ASSERT_TRUE(button != nullptr && composed != nullptr);
        EXPECT_EQ(StringMember(*button, "type"), "UIElement");
        EXPECT_EQ(StringMember(*composed, "type"), "UIElement");
        for (const Json* segment : {button, composed}) {
            EXPECT_EQ(StringMember(*segment, "action"), "UIControlApiMix");
            EXPECT_FALSE(StringMember(*segment, "text").empty());
            EXPECT_FALSE(StringMember(*segment, "value").empty());
            EXPECT_FALSE(BoolMember(*segment, "disabled"));
        }
        EXPECT_FALSE(BoolMember(*button, "selected"));
        EXPECT_TRUE(BoolMember(*composed, "selected"));

        EXPECT_EQ(StringMember(*Member(Call("ui_click", Click("MixButton")), "result"), "value"), "button");
        EXPECT_EQ(StringMember(*Member(Call("ui_click", Click("MixComposed")), "result"), "value"), "composed");
        EXPECT_EQ(recorder.Count("UIControlApiMix"), 2);
    }

    TEST_F(UIControlApiTest, ATreeShowsAClosedDropdownOfComposedOptionsAndTheOptionsAsHidden) {
        CoreManager::GetUIVars().Set("uiApiCLevel"_intern, "hard");
        Show("Choices");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* level = FindElement(*FindCanvas(*Member(response, "result"), "Panel"), "CLevel");
        ASSERT_NE(level, nullptr);
        EXPECT_EQ(StringMember(*level, "type"), "UIDropdown");
        EXPECT_EQ(StringMember(*level, "selectedValue"), "hard");
        EXPECT_EQ(StringMember(*level, "text"), "Hard") << "the header shows the caption child of the chosen option";
        EXPECT_EQ(StringMember(*level, "action"), "UIControlApiCLevel");
        EXPECT_FALSE(BoolMember(*level, "open"));
        for (const char* option : {"CEasy", "CHard", "CLocked"}) {
            const Json* item = FindElement(*level, option);
            ASSERT_NE(item, nullptr) << option;
            EXPECT_FALSE(BoolMember(*item, "visible")) << option << " is hidden while the list is closed";
        }
        EXPECT_TRUE(BoolMember(*FindElement(*level, "CHard"), "selected"));
        EXPECT_FALSE(BoolMember(*FindElement(*level, "CEasy"), "selected"));
        EXPECT_TRUE(BoolMember(*FindElement(*level, "CLocked"), "disabled"));
        EXPECT_EQ(StringMember(*FindElement(*level, "CHard"), "value"), "hard");
        EXPECT_EQ(StringMember(*FindElement(*level, "CHard"), "icon"), "HardIcon");
        EXPECT_EQ(StringMember(*FindElement(*level, "CEasy"), "text"), "Easy");
        EXPECT_EQ(StringMember(*FindElement(*level, "CHard"), "action"), "") << "the options keep no action of their own";
    }

    TEST_F(UIControlApiTest, AClickOnAComposedOptionAnswersWithTheActionOfTheDropdownAndTheListCloses) {
        CoreManager::GetUIVars().Set("uiApiCLevel"_intern, "easy");
        Show("Choices");
        ASSERT_TRUE(IsOk(Call("ui_click", Click("CLevel"))));
        Show("Choices");
        const Json open = Call("ui_tree");
        const Json* openLevel = FindElement(*FindCanvas(*Member(open, "result"), "Panel"), "CLevel");
        EXPECT_TRUE(BoolMember(*openLevel, "open"));
        EXPECT_TRUE(BoolMember(*FindElement(*openLevel, "CHard"), "visible"));
        ExpectRect(Member(*FindElement(*openLevel, "CHard"), "rect"), 100.0f, 412.0f, 240.0f, 56.0f);
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("CHard"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        const Json& result = *Member(clicked, "result");
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiCLevel");
        EXPECT_EQ(StringMember(result, "value"), "hard");
        EXPECT_EQ(recorder.Count("UIControlApiCLevel"), 1);
        EXPECT_EQ(recorder.LastValue(), "hard");

        Show("Choices");
        const Json tree = Call("ui_tree");
        const Json* level = FindElement(*FindCanvas(*Member(tree, "result"), "Panel"), "CLevel");
        EXPECT_FALSE(BoolMember(*level, "open"));
        EXPECT_FALSE(BoolMember(*FindElement(*level, "CHard"), "visible"));
        EXPECT_EQ(StringMember(*level, "selectedValue"), "easy") << "the choice is the game's: a click sets no variable";
        EXPECT_EQ(Member(*FindElement(*level, "CHard"), "open"), nullptr);
        EXPECT_EQ(ErrorCode(Call("ui_click", Click("CHard"))), "not_clickable") << "the list is closed again";
    }

    TEST_F(UIControlApiTest, AComposedOptionIsNotClickableWhileTheListIsClosed) {
        Show("Choices");

        const Json response = Call("ui_click", Click("CEasy"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("hidden"), std::string::npos) << ErrorMessage(response);
    }

    TEST_F(UIControlApiTest, ADisabledComposedOptionTakesTheClickAndTheListStaysOpen) {
        Show("Choices");
        ASSERT_TRUE(IsOk(Call("ui_click", Click("CLevel"))));
        Show("Choices");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("CLocked"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        EXPECT_TRUE(BoolMember(*Member(clicked, "result"), "consumed"));
        EXPECT_EQ(StringMember(*Member(clicked, "result"), "action"), "");
        EXPECT_EQ(StringMember(*Member(clicked, "result"), "value"), "");
        EXPECT_EQ(recorder.Total(), 0u);
        Show("Choices");
        const Json tree = Call("ui_tree");
        EXPECT_TRUE(BoolMember(*FindElement(*FindCanvas(*Member(tree, "result"), "Panel"), "CLevel"), "open"));
    }

    TEST_F(UIControlApiTest, AClickElsewhereWhileAComposedListIsOpenClosesItAndIsConsumed) {
        Show("Choices");
        ASSERT_TRUE(IsOk(Call("ui_click", Click("CLevel"))));
        Show("Choices");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("CBelow"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        EXPECT_TRUE(BoolMember(*Member(clicked, "result"), "consumed"));
        EXPECT_EQ(StringMember(*Member(clicked, "result"), "action"), "");
        EXPECT_EQ(recorder.Total(), 0u) << "the press only closed the list";
        Show("Choices");
        const Json tree = Call("ui_tree");
        EXPECT_FALSE(BoolMember(*FindElement(*FindCanvas(*Member(tree, "result"), "Panel"), "CLevel"), "open"));
    }

    TEST_F(UIControlApiTest, ADropdownOfComposedOptionsCanBeDrivenEndToEnd) {
        Show("Choices");
        Recorder recorder;

        ASSERT_TRUE(IsOk(Call("ui_click", Click("CLevel"))));
        Show("Choices");  // the "step"
        const Json picked = Call("ui_click", Click("CEasy"));
        ASSERT_TRUE(IsOk(picked)) << ErrorMessage(picked);
        EXPECT_EQ(StringMember(*Member(picked, "result"), "value"), "easy");
        Show("Choices");

        EXPECT_EQ(recorder.Count("UIControlApiCLevel"), 1);
        EXPECT_EQ(recorder.LastValue(), "easy");
    }

}  // namespace BECore::Tests
