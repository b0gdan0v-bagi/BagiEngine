#pragma once

#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/UI/UICanvasScaler.h>
#include <BECore/UI/UIScreenContent.h>
#include <gtest/gtest.h>

namespace BECore::Tests::RmlUiScreens {

    // The three screens that the repository ships, in the engine's XML (games/*/config/SceneConfig.xml) and in RmlUi's HTML + CSS dialect.
    // The XML is the reference: every element of the RML screen must land on the same rect, in every shape of the size matrix.
    // The XML is written composed, as the shipped files and the HTML compiler write it (a text is a `UIElement` with a `UITextComponent`, a button one with a text and a clicker).

    inline constexpr const char* kMenuXml = R"(<ui><elements>
  <element type="UIStack" name="Column" anchor="Center" width="90%" maxWidth="320" height="232" direction="Vertical" spacing="16" crossAlign="Stretch">
    <children>
      <element type="UIElement" name="Title" height="88" fontSize="56">
        <components>
          <component type="UITextComponent" text="Match 3" align="Center" verticalAlign="Center"/>
        </components>
      </element>
      <element type="UIElement" name="Play" height="56" fontSize="28" color="40,90,60,255">
        <components>
          <component type="UITextComponent" text="Play" align="Center" verticalAlign="Center" wrap="false"/>
          <component type="UIInputClicker" action="Match3Play"/>
        </components>
      </element>
      <element type="UIElement" name="Quit" height="56" fontSize="28" color="90,50,50,255">
        <components>
          <component type="UITextComponent" text="Quit" align="Center" verticalAlign="Center" wrap="false"/>
          <component type="UIInputClicker" action="Match3Quit"/>
        </components>
      </element>
    </children>
  </element>
</elements></ui>)";

    inline constexpr const char* kMenuRml = R"(<rml>
<head><style>
body { width: 100%; height: 100%; display: flex; align-items: center; justify-content: center; font-family: "PT Sans"; color: white; pointer-events: none; }
#Column { display: flex; flex-direction: column; width: 90%; max-width: 320dp; height: 232dp; gap: 16dp; }
#Title { height: 88dp; font-size: 56dp; line-height: 88dp; text-align: center; }
button { height: 56dp; font-size: 28dp; line-height: 56dp; text-align: center; pointer-events: auto; }
#Play { background-color: rgb(40, 90, 60); }
#Quit { background-color: rgb(90, 50, 50); }
</style></head>
<body>
  <div id="Column">
    <div id="Title">Match 3</div>
    <button id="Play" data-action="Match3Play">Play</button>
    <button id="Quit" data-action="Match3Quit">Quit</button>
  </div>
</body></rml>)";

    inline constexpr const char* kMatch3HudXml = R"(<ui><elements>
  <element type="UIElement" name="Score" x="96" y="12" width="250" height="40" fontSize="28">
    <components>
      <component type="UITextComponent" text="Score: {score}" verticalAlign="Center"/>
    </components>
  </element>
  <element type="UIStack" name="Buttons" anchor="TopRight" x="-96" y="12" width="260" height="48" direction="Horizontal" spacing="12" crossAlign="Stretch">
    <children>
      <element type="UIElement" name="Reset" width="124" height="48" fontSize="24" color="40,90,60,255">
        <components>
          <component type="UITextComponent" text="Reset" align="Center" verticalAlign="Center" wrap="false"/>
          <component type="UIInputClicker" action="Match3Reset"/>
        </components>
      </element>
      <element type="UIElement" name="Menu" width="124" height="48" fontSize="24" color="70,70,100,255">
        <components>
          <component type="UITextComponent" text="Menu" align="Center" verticalAlign="Center" wrap="false"/>
          <component type="UIInputClicker" action="Match3Menu"/>
        </components>
      </element>
    </children>
  </element>
  <element type="UIElement" name="Seed" anchor="BottomLeft" x="96" y="-26" width="300" height="24" fontSize="16">
    <components>
      <component type="UITextComponent" text="seed {seed}" textColor="190,190,190,255"/>
    </components>
  </element>
</elements></ui>)";

    inline constexpr const char* kMatch3HudRml = R"(<rml>
<head><style>
body { width: 100%; height: 100%; font-family: "PT Sans"; color: white; pointer-events: none; }
#Score { position: absolute; left: 96dp; top: 12dp; width: 250dp; height: 40dp; font-size: 28dp; line-height: 40dp; }
#Buttons { position: absolute; right: 96dp; top: 12dp; width: 260dp; height: 48dp; display: flex; flex-direction: row; gap: 12dp; }
button { width: 124dp; height: 48dp; font-size: 24dp; line-height: 48dp; text-align: center; pointer-events: auto; }
#Reset { background-color: rgb(40, 90, 60); }
#Menu { background-color: rgb(70, 70, 100); }
#Seed { position: absolute; left: 96dp; bottom: 26dp; width: 300dp; height: 24dp; font-size: 16dp; color: rgb(190, 190, 190); }
</style></head>
<body data-model="vars">
  <div id="Score">Score: {{vars.score}}</div>
  <div id="Buttons">
    <button id="Reset" data-action="Match3Reset">Reset</button>
    <button id="Menu" data-action="Match3Menu">Menu</button>
  </div>
  <div id="Seed">seed {{vars.seed}}</div>
</body></rml>)";

    struct Screen {
        const char* name;
        const char* xml;
        const char* rml;
        CanvasSpace space;
        float viewWidth;  ///< the scene's view, for a SceneView screen
        float viewHeight;
        float minScale;
        eastl::vector<const char*> ids;
        eastl::vector<const char*> buttons;  ///< the ids that take clicks
    };

    inline const Screen kScreens[] = {
        {"Match3 menu", kMenuXml, kMenuRml, CanvasSpace::Screen, 0.0f, 0.0f, 0.7f, {"Column", "Title", "Play", "Quit"}, {"Play", "Quit"}},
        {"Match3 HUD", kMatch3HudXml, kMatch3HudRml, CanvasSpace::SceneView, 712.0f, 672.0f, 0.25f, {"Score", "Buttons", "Reset", "Menu", "Seed"}, {"Reset", "Menu"}},
    };

    inline constexpr float kRectTolerance = 1.5f;  // the context is a whole number of pixels
    inline constexpr float kMinTouchSize = 32.0f;

    inline UIScreenContent ReadXml(const char* xml) {
        UIScreenContent content;
        XmlDeserializer deserializer;
        EXPECT_TRUE(deserializer.LoadFromString(StringView(xml)));
        content.Deserialize(deserializer);
        EXPECT_FALSE(deserializer.HasErrors());
        return content;
    }

    inline eastl::string SceneXml(const Screen& screen) {
        const bool view = screen.space == CanvasSpace::SceneView;
        return Format(R"(<SceneManager><scenes><scene type="Scene" name="S" viewWidth="{}" viewHeight="{}"><nodes><node name="Doc"><components>
  <component type="RmlUiDocumentComponent" space="{}" scaleMode="ScaleWithScreen" referenceWidth="1280" referenceHeight="720" match="0.5" minScale="{}" useSafeArea="true"/>
</components></node></nodes></scene></scenes></SceneManager>)",
                      view ? screen.viewWidth : 0.0f, view ? screen.viewHeight : 0.0f, EnumUtils<CanvasSpace>::ToString(screen.space), screen.minScale);
    }

}  // namespace BECore::Tests::RmlUiScreens
