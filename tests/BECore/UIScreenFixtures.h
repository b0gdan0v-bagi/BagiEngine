#pragma once

#include <BECore/Types/Types.h>
#include <Common/UITestSupport.h>

namespace BECore::Tests {

    /*
     * The two panels of the screen tests, one source for every test that builds them: the scene tests UISelectorScreenTest and UISettingsScreenTest load them as a scene
     * (PanelSceneXml) and UIWebTest compares their HTML + CSS form with the XML (UIWebPanelXml). Each panel is the HUD button that opens it, the plate, the variants, the HTML and
     * CSS of the plate, and the names of the plate's elements.
     */

    // ---- the choice panel (the Chess opponent panel) ------------------------------------------------------------------------

    inline constexpr const char* kSelectorHudButton = R"(<element type="UIElement" name="NewGame" anchor="TopRight" x="-12" y="12" width="190" height="56" fontSize="24" color="45,75,120,255">
  <components>
    <component type="UITextComponent" text="Opponent" align="Center" verticalAlign="Center" wrap="false"/>
    <component type="UIInputClicker" action="SelectorOpen"/>
  </components>
</element>)";

    // Landscape rows are 56 high with the caption on the left; the portrait variant (a view taller than wide) turns every row into 32 + 8 + 56 = 96 units, caption above group,
    // and the plate grows with it: 32 padding + 44 title + 4 x 96 + 56 OK + 5 x 12 spacing = 576 (416 in landscape). The same variant sets the font of the Level segments to 22: a
    // quarter of a narrow plate on a window with side insets is too little for "Medium" at 24 with its air.
    inline constexpr const char* kSelectorPlate =
        R"(<element type="UIStack" name="Plate" anchor="Center" width="94%" maxWidth="640" height="416" direction="Vertical" spacing="12" padding="16" crossAlign="Stretch" color="36,40,52,255" blocksInput="true">
  <children>
    <element type="UIElement" name="Title" height="44" fontSize="30">
      <components><component type="UITextComponent" text="Opponent" align="Center"/></components>
    </element>
    <element type="UIStack" name="ModeRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
      <children>
        <element type="UIElement" name="ModeCaption" width="150" height="56" fontSize="24">
          <components><component type="UITextComponent" text="Opponent" verticalAlign="Center"/></components>
        </element>
        <element type="UISegmented" name="Mode" bind="opponentMode" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255">
          <children>
            <element type="UIElement" name="ModeTwo" flex="1" height="56" fontSize="24" color="70,70,100,255" selectedColor="110,130,200,255">
              <components>
                <component type="UITextComponent" text="Two players" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SelectorMode" value="two"/>
              </components>
            </element>
            <element type="UIElement" name="ModeComputer" flex="1" height="56" fontSize="24" color="70,70,100,255" selectedColor="110,130,200,255">
              <components>
                <component type="UITextComponent" text="Computer" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SelectorMode" value="computer"/>
              </components>
            </element>
          </children>
        </element>
      </children>
    </element>
    <element type="UIStack" name="ColorRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
      <children>
        <element type="UIElement" name="ColorCaption" width="150" height="56" fontSize="24">
          <components><component type="UITextComponent" text="Play as" verticalAlign="Center"/></components>
        </element>
        <element type="UISegmented" name="Color" bind="opponentColor" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255">
          <children>
            <element type="UIElement" name="ColorWhite" flex="1" height="56" fontSize="24" color="70,70,100,255" selectedColor="110,130,200,255">
              <components>
                <component type="UITextComponent" text="White" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SelectorColor" value="white"/>
              </components>
            </element>
            <element type="UIElement" name="ColorBlack" flex="1" height="56" fontSize="24" color="70,70,100,255" selectedColor="110,130,200,255">
              <components>
                <component type="UITextComponent" text="Black" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SelectorColor" value="black"/>
              </components>
            </element>
          </children>
        </element>
      </children>
    </element>
    <element type="UIStack" name="EngineRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
      <children>
        <element type="UIElement" name="EngineCaption" width="150" height="56" fontSize="24">
          <components><component type="UITextComponent" text="Engine" verticalAlign="Center"/></components>
        </element>
        <element type="UISegmented" name="Engine" bind="opponentEngine" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255">
          <children>
            <element type="UIElement" name="EngineStockfish" flex="1" height="56" fontSize="24" color="70,70,100,255" selectedColor="110,130,200,255">
              <components>
                <component type="UITextComponent" text="Stockfish" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SelectorEngine" value="stockfish"/>
              </components>
            </element>
            <element type="UIElement" name="EngineOwn" flex="1" height="56" fontSize="24" color="70,70,100,255" selectedColor="110,130,200,255">
              <components>
                <component type="UITextComponent" text="Own engine" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SelectorEngine" value="own"/>
              </components>
            </element>
          </children>
        </element>
      </children>
    </element>
    <element type="UIStack" name="LevelRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
      <children>
        <element type="UIElement" name="LevelCaption" width="150" height="56" fontSize="24">
          <components><component type="UITextComponent" text="Level" verticalAlign="Center"/></components>
        </element>
        <element type="UISegmented" name="Level" bind="opponentLevel" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255">
          <children>
            <element type="UIElement" name="LevelEasy" flex="1" height="56" fontSize="24" color="70,70,100,255" selectedColor="110,130,200,255">
              <components>
                <component type="UITextComponent" text="Easy" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SelectorLevel" value="easy"/>
              </components>
            </element>
            <element type="UIElement" name="LevelMedium" flex="1" height="56" fontSize="24" color="70,70,100,255" selectedColor="110,130,200,255">
              <components>
                <component type="UITextComponent" text="Medium" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SelectorLevel" value="medium"/>
              </components>
            </element>
            <element type="UIElement" name="LevelHard" flex="1" height="56" fontSize="24" color="70,70,100,255" selectedColor="110,130,200,255">
              <components>
                <component type="UITextComponent" text="Hard" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SelectorLevel" value="hard"/>
              </components>
            </element>
            <element type="UIElement" name="LevelMax" flex="1" height="56" fontSize="24" color="70,70,100,255" selectedColor="110,130,200,255">
              <components>
                <component type="UITextComponent" text="Max" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SelectorLevel" value="max"/>
              </components>
            </element>
          </children>
        </element>
      </children>
    </element>
    <element type="UIElement" name="Ok" height="56" fontSize="26" color="40,90,60,255">
      <components>
        <component type="UITextComponent" text="OK" align="Center" verticalAlign="Center" wrap="false"/>
        <component type="UIInputClicker" action="SelectorClose"/>
      </components>
    </element>
  </children>
</element>)";

    inline constexpr const char* kSelectorVariants = R"(<variants>
  <variant when="orientation:portrait">
    <overrides>
      <set element="Plate" height="576"/>
      <set element="ModeRow" direction="Vertical" height="96"/>
      <set element="ModeCaption" height="32"/>
      <set element="ColorRow" direction="Vertical" height="96"/>
      <set element="ColorCaption" height="32"/>
      <set element="EngineRow" direction="Vertical" height="96"/>
      <set element="EngineCaption" height="32"/>
      <set element="LevelRow" direction="Vertical" height="96"/>
      <set element="LevelCaption" height="32"/>
      <set element="LevelEasy" fontSize="22"/>
      <set element="LevelMedium" fontSize="22"/>
      <set element="LevelHard" fontSize="22"/>
      <set element="LevelMax" fontSize="22"/>
    </overrides>
  </variant>
</variants>)";

    inline constexpr const char* kSelectorHtml = R"(
<div id="Plate">
  <span id="Title">Opponent</span>
  <div id="ModeRow" class="row">
    <span id="ModeCaption" class="caption">Opponent</span>
    <div id="Mode" class="group" data-widget="segmented" data-bind="opponentMode">
      <button id="ModeTwo" data-action="SelectorMode" data-value="two">Two players</button>
      <button id="ModeComputer" data-action="SelectorMode" data-value="computer">Computer</button>
    </div>
  </div>
  <div id="ColorRow" class="row">
    <span id="ColorCaption" class="caption">Play as</span>
    <div id="Color" class="group" data-widget="segmented" data-bind="opponentColor">
      <button id="ColorWhite" data-action="SelectorColor" data-value="white">White</button>
      <button id="ColorBlack" data-action="SelectorColor" data-value="black">Black</button>
    </div>
  </div>
  <div id="EngineRow" class="row">
    <span id="EngineCaption" class="caption">Engine</span>
    <div id="Engine" class="group" data-widget="segmented" data-bind="opponentEngine">
      <button id="EngineStockfish" data-action="SelectorEngine" data-value="stockfish">Stockfish</button>
      <button id="EngineOwn" data-action="SelectorEngine" data-value="own">Own engine</button>
    </div>
  </div>
  <div id="LevelRow" class="row">
    <span id="LevelCaption" class="caption">Level</span>
    <div id="Level" class="group" data-widget="segmented" data-bind="opponentLevel">
      <button id="LevelEasy" data-action="SelectorLevel" data-value="easy">Easy</button>
      <button id="LevelMedium" data-action="SelectorLevel" data-value="medium">Medium</button>
      <button id="LevelHard" data-action="SelectorLevel" data-value="hard">Hard</button>
      <button id="LevelMax" data-action="SelectorLevel" data-value="max">Max</button>
    </div>
  </div>
  <button id="Ok" data-action="SelectorClose">OK</button>
</div>)";

    inline constexpr const char* kSelectorCss = R"(
#Plate    { anchor: center; display: flex; flex-direction: column; align-items: stretch; gap: 12px; padding: 16px; width: 94%; max-width: 640px; height: 416px; background: rgb(36, 40, 52); }
#Title    { height: 44px; font-size: 30px; text-align: center; }
.row      { display: flex; flex-direction: row; align-items: stretch; gap: 8px; height: 56px; }
.caption  { width: 150px; height: 56px; font-size: 24px; vertical-align: middle; }
.group    { display: flex; flex-direction: row; gap: 4px; flex: 1; height: 56px; background: rgb(30, 30, 30); }
.group button { flex: 1; height: 56px; font-size: 24px; background: rgb(70, 70, 100); }
.group button:checked { background: rgb(110, 130, 200); }
#Ok       { height: 56px; font-size: 26px; background: rgb(40, 90, 60); }
@media (orientation: portrait) {
  #Plate    { height: 576px; }
  .row      { flex-direction: column; height: 96px; }
  .caption  { height: 32px; }
  #Level button { font-size: 22px; }
}
)";

    inline constexpr const char* kSelectorPlateNames[] = {"Plate",      "Title",       "ModeRow",   "ModeCaption",   "Mode",   "ModeTwo",         "ModeComputer", "ColorRow", "ColorCaption", "Color",
                                                          "ColorWhite", "ColorBlack",  "EngineRow", "EngineCaption", "Engine", "EngineStockfish", "EngineOwn",    "LevelRow", "LevelCaption", "Level",
                                                          "LevelEasy",  "LevelMedium", "LevelHard", "LevelMax",      "Ok"};

    /// A choice group of the selector: its `UISegmented`, the variable it is bound to, the action its segments emit and its segments
    struct UISelectorGroup {
        const char* name;
        const char* variable;
        const char* action;
        Vector<Pair<const char*, const char*>> segments;  // the name of the button, its value
    };

    /// The four groups of the panel (the words are in kSelectorPlate; the values are what the game publishes)
    inline const Vector<UISelectorGroup>& SelectorGroups() {
        static const Vector<UISelectorGroup> groups = {
            {"Mode", "opponentMode", "SelectorMode", {{"ModeTwo", "two"}, {"ModeComputer", "computer"}}},
            {"Color", "opponentColor", "SelectorColor", {{"ColorWhite", "white"}, {"ColorBlack", "black"}}},
            {"Engine", "opponentEngine", "SelectorEngine", {{"EngineStockfish", "stockfish"}, {"EngineOwn", "own"}}},
            {"Level", "opponentLevel", "SelectorLevel", {{"LevelEasy", "easy"}, {"LevelMedium", "medium"}, {"LevelHard", "hard"}, {"LevelMax", "max"}}},
        };
        return groups;
    }

    // ---- the settings panel: a toggle, a slider, a progress bar and a language group, every word a key of kSettingsStringsXml -------

    inline constexpr const char* kSettingsHudButton = R"(<element type="UIElement" name="Open" anchor="TopRight" x="-12" y="12" width="190" height="56" fontSize="24" color="45,75,120,255">
  <components>
    <component type="UITextComponent" text="{#settings.open}" align="Center" verticalAlign="Center" wrap="false"/>
    <component type="UIInputClicker" action="SettingsOpen"/>
  </components>
</element>)";

    // Landscape rows are 56 high with the caption on the left. The portrait variant turns the Language row into 32 + 8 + 56 = 96 units (caption above the three segments) and the
    // plate grows with it: 32 padding + 44 title + 4 x 56 + 96 - 56 + 56 OK + 5 x 12 spacing = 456 (416 in landscape).
    inline constexpr const char* kSettingsPlate =
        R"(<element type="UIStack" name="Plate" anchor="Center" width="94%" maxWidth="640" height="416" direction="Vertical" spacing="12" padding="16" crossAlign="Stretch" color="36,40,52,255" blocksInput="true">
  <children>
    <element type="UIElement" name="Title" height="44" fontSize="30">
      <components><component type="UITextComponent" text="{#settings.title}" align="Center"/></components>
    </element>
    <element type="UIElement" name="Sound" height="56" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
      <components>
        <component type="UIInputClicker"/>
        <component type="UIToggleComponent" bind="soundOn" action="SettingsSound"/>
      </components>
      <children>
        <element type="UIElement" name="SoundCaption" flex="1" height="100%" fontSize="24">
          <components><component type="UITextComponent" text="{#settings.sound}" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
        </element>
        <element type="UIElement" name="SoundSwitch" height="100%" aspectRatio="1.25"><components><component type="UISwitchComponent"/></components></element>
      </children>
    </element>
    <element type="UIStack" name="MusicRow" height="56" direction="Horizontal" spacing="8" crossAlign="Center">
      <children>
        <element type="UIElement" name="MusicCaption" width="220" height="56" fontSize="24">
          <components><component type="UITextComponent" text="{#settings.music}" verticalAlign="Center"/></components>
        </element>
        <element type="UIElement" name="Volume" flex="1" height="56">
          <components>
            <component type="UIInputClicker" stateLook="false"/>
            <component type="UISliderComponent" bind="volume" action="SettingsVolume" minValue="0" maxValue="1" step="0.1"/>
          </components>
        </element>
      </children>
    </element>
    <element type="UIStack" name="DownloadRow" height="56" direction="Horizontal" spacing="8" crossAlign="Center">
      <children>
        <element type="UIElement" name="DownloadCaption" width="220" height="56" fontSize="24">
          <components><component type="UITextComponent" text="{#settings.download}" verticalAlign="Center"/></components>
        </element>
        <element type="UIProgressBar" name="Progress" bind="progress" minValue="0" maxValue="100" flex="1" height="24"/>
        <element type="UIElement" name="Percent" width="64" height="56" fontSize="24">
          <components><component type="UITextComponent" text="{progress}%" align="Right" verticalAlign="Center"/></components>
        </element>
      </children>
    </element>
    <element type="UIStack" name="LanguageRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
      <children>
        <element type="UIElement" name="LanguageCaption" width="220" height="56" fontSize="24">
          <components><component type="UITextComponent" text="{#settings.language}" verticalAlign="Center"/></components>
        </element>
        <element type="UISegmented" name="Language" bind="language" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255">
          <children>
            <element type="UIElement" name="LanguageEn" flex="1" height="56" fontSize="24" color="70,70,100,255">
              <components>
                <component type="UITextComponent" text="{#lang.en}" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SettingsLanguage" value="en"/>
              </components>
            </element>
            <element type="UIElement" name="LanguageRu" flex="1" height="56" fontSize="24" color="70,70,100,255">
              <components>
                <component type="UITextComponent" text="{#lang.ru}" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SettingsLanguage" value="ru"/>
              </components>
            </element>
            <element type="UIElement" name="LanguageDe" flex="1" height="56" fontSize="24" color="70,70,100,255">
              <components>
                <component type="UITextComponent" text="{#lang.de}" align="Center" verticalAlign="Center" wrap="false"/>
                <component type="UIInputClicker" action="SettingsLanguage" value="de"/>
              </components>
            </element>
          </children>
        </element>
      </children>
    </element>
    <element type="UIElement" name="Ok" height="56" fontSize="26" color="40,90,60,255">
      <components>
        <component type="UITextComponent" text="{#settings.ok}" align="Center" verticalAlign="Center" wrap="false"/>
        <component type="UIInputClicker" action="SettingsClose"/>
      </components>
    </element>
  </children>
</element>)";

    inline constexpr const char* kSettingsVariants = R"(<variants>
  <variant when="orientation:portrait">
    <overrides>
      <set element="Plate" height="456"/>
      <set element="LanguageRow" direction="Vertical" height="96"/>
      <set element="LanguageCaption" height="32"/>
    </overrides>
  </variant>
</variants>)";

    inline constexpr const char* kSettingsHtml = R"(
<div id="Plate">
  <span id="Title">{#settings.title}</span>
  <div id="Sound" data-widget="toggle" data-bind="soundOn" data-action="SettingsSound">{#settings.sound}</div>
  <div id="MusicRow" class="row centered">
    <span id="MusicCaption" class="caption">{#settings.music}</span>
    <div id="Volume" data-widget="slider" data-bind="volume" data-action="SettingsVolume" data-min="0" data-max="1" data-step="0.1"></div>
  </div>
  <div id="DownloadRow" class="row centered">
    <span id="DownloadCaption" class="caption">{#settings.download}</span>
    <div id="Progress" data-widget="progress" data-bind="progress" data-min="0" data-max="100"></div>
    <span id="Percent">{progress}%</span>
  </div>
  <div id="LanguageRow" class="row">
    <span id="LanguageCaption" class="caption">{#settings.language}</span>
    <div id="Language" class="group" data-widget="segmented" data-bind="language">
      <button id="LanguageEn" data-action="SettingsLanguage" data-value="en">{#lang.en}</button>
      <button id="LanguageRu" data-action="SettingsLanguage" data-value="ru">{#lang.ru}</button>
      <button id="LanguageDe" data-action="SettingsLanguage" data-value="de">{#lang.de}</button>
    </div>
  </div>
  <button id="Ok" data-action="SettingsClose">{#settings.ok}</button>
</div>)";

    inline constexpr const char* kSettingsCss = R"(
#Plate    { anchor: center; display: flex; flex-direction: column; align-items: stretch; gap: 12px; padding: 16px; width: 94%; max-width: 640px; height: 416px; background: rgb(36, 40, 52); }
#Title    { height: 44px; font-size: 30px; text-align: center; }
#Sound    { height: 56px; font-size: 24px; vertical-align: middle; }
.row      { display: flex; flex-direction: row; align-items: stretch; gap: 8px; height: 56px; }
.centered { align-items: center; }
.caption  { width: 220px; height: 56px; font-size: 24px; vertical-align: middle; }
#Volume   { flex: 1; height: 56px; }
#Progress { flex: 1; height: 24px; }
#Percent  { width: 64px; height: 56px; font-size: 24px; text-align: right; vertical-align: middle; }
.group    { display: flex; flex-direction: row; gap: 4px; flex: 1; height: 56px; background: rgb(30, 30, 30); }
.group button { flex: 1; height: 56px; font-size: 24px; background: rgb(70, 70, 100); }
#Ok       { height: 56px; font-size: 26px; background: rgb(40, 90, 60); }
@media (orientation: portrait) {
  #Plate          { height: 456px; }
  #LanguageRow    { flex-direction: column; height: 96px; }
  #LanguageCaption { height: 32px; }
}
)";

    // English, Russian and a deliberately long test language. The names of the languages are the same in all three (each language is written in itself)
    inline constexpr const char* kSettingsStringsXml = R"(
<UIStrings default="en">
  <language id="en">
    <string key="settings.open" text="Settings"/>
    <string key="settings.title" text="Settings"/>
    <string key="settings.sound" text="Sound"/>
    <string key="settings.music" text="Music volume"/>
    <string key="settings.download" text="Download"/>
    <string key="settings.language" text="Language"/>
    <string key="settings.ok" text="OK"/>
    <string key="lang.en" text="English"/>
    <string key="lang.ru" text="Русский"/>
    <string key="lang.de" text="Deutsch"/>
  </language>
  <language id="ru">
    <string key="settings.open" text="Настройки"/>
    <string key="settings.title" text="Настройки"/>
    <string key="settings.sound" text="Звук"/>
    <string key="settings.music" text="Громкость музыки"/>
    <string key="settings.download" text="Загрузка"/>
    <string key="settings.language" text="Язык"/>
    <string key="settings.ok" text="Готово"/>
    <string key="lang.en" text="English"/>
    <string key="lang.ru" text="Русский"/>
    <string key="lang.de" text="Deutsch"/>
  </language>
  <language id="de">
    <string key="settings.open" text="Einstellungen"/>
    <string key="settings.title" text="Einstellungen ändern"/>
    <string key="settings.sound" text="Geräuscheffekte"/>
    <string key="settings.music" text="Lautstärke der Musik"/>
    <string key="settings.download" text="Herunterladen"/>
    <string key="settings.language" text="Sprachauswahl"/>
    <string key="settings.ok" text="Übernehmen"/>
    <string key="lang.en" text="English"/>
    <string key="lang.ru" text="Русский"/>
    <string key="lang.de" text="Deutsch"/>
  </language>
</UIStrings>)";

    inline constexpr const char* kSettingsPlateNames[] = {"Plate",           "Title",      "Sound",   "MusicRow",    "MusicCaption",    "Volume",   "DownloadRow",
                                                          "DownloadCaption", "Progress",   "Percent", "LanguageRow", "LanguageCaption", "Language", "LanguageEn",
                                                          "LanguageRu",      "LanguageDe", "Ok"};

    // ---- the forms the tests read -------------------------------------------------------------------------------------------------

    /// The scene of a panel: a canvas `Hud` with @p hudButton and, on the node @p panelNode, a modal canvas started hidden with a dim behind @p plate and @p variants. The view
    /// size is left as `@VIEW_W@` x `@VIEW_H@` for SceneXml()
    [[nodiscard]] inline String PanelSceneXml(StringView hudButton, StringView panelNode, StringView plate, StringView variants) {
        String xml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main" viewWidth="@VIEW_W@" viewHeight="@VIEW_H@"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="SceneView" useSafeArea="true">
        <elements>
)";
        xml.append(hudButton.data(), hudButton.size());
        xml += R"(
        </elements>
      </component>
    </components></node>
    <node name=")";
        xml.append(panelNode.data(), panelNode.size());
        xml += R"(" layer="20"><components>
      <component type="UICanvasComponent" space="SceneView" useSafeArea="true" modal="true" visible="false">
        <elements>
          <element type="UIElement" name="Dim" anchor="Stretch" ignoreSafeArea="true" color="0,0,0,150" blocksInput="true"/>
)";
        xml.append(plate.data(), plate.size());
        xml += "\n        </elements>\n";
        xml.append(variants.data(), variants.size());
        xml += R"(
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";
        return xml;
    }

    /// The plate of a panel as the `UIDocument` its HTML + CSS compiles to, for a comparison with the compiler's output. **One documented edit:** every `UISegmented`
    /// takes `blocksInput="true"`, because in the HTML front-end a group with a visible background takes clicks (`ui-web.md`, "Elements"), while the scene panels leave a
    /// press between two segments to the plate
    [[nodiscard]] inline String UIWebPanelXml(StringView plate, StringView variants) {
        String xml = "<ui><elements>\n";
        xml.append(plate.data(), plate.size());
        xml += "\n</elements>\n";
        xml.append(variants.data(), variants.size());
        xml += "</ui>";
        ReplaceAll(xml, R"(type="UISegmented")", R"(type="UISegmented" blocksInput="true")");
        return xml;
    }

}  // namespace BECore::Tests
