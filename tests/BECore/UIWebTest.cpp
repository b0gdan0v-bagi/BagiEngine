#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UICssParser.h>
#include <BECore/UI/UIDocument.h>
#include <BECore/UI/UIHtmlParser.h>
#include <BECore/UI/UIImage.h>
#include <BECore/UI/UILabel.h>
#include <BECore/UI/UIPanel.h>
#include <BECore/UI/UIProgressBar.h>
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UISlider.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UIToggle.h>
#include <BECore/UI/UIWebCompiler.h>
#include <BECore/UI/UIWebText.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/UILayoutMatrix.h>
#include <Common/UITreeCompare.h>
#include <fstream>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 0.01f;

        /// What a compile gives: the XML, the report, the XML read back into the reflected content, and a handle on the XML nodes by element name
        struct Compiled {
            eastl::string xml;
            UIWebReport report;
            UIScreenContent content;
            pugi::xml_document document;
            bool deserializeErrors = false;

            [[nodiscard]] pugi::xml_node Find(const char* name) const {
                return FindIn(document.document_element(), name);
            }

            /// An attribute of the element of that name; "" when there is none
            [[nodiscard]] std::string Attr(const char* name, const char* attribute) const {
                return Find(name).attribute(attribute).as_string();
            }

            [[nodiscard]] bool Has(const char* name, const char* attribute) const {
                return !Find(name).attribute(attribute).empty();
            }

            [[nodiscard]] size_t IssuesMentioning(const char* text) const {
                size_t count = 0;
                for (const UIWebIssue& issue : report.issues) {
                    if (std::string(issue.message.c_str()).find(text) != std::string::npos) {
                        ++count;
                    }
                }
                return count;
            }

            [[nodiscard]] UIElement* Element(const char* name) const {
                for (const auto& element : content.elements) {
                    if (auto* found = element->FindByName(PoolString::Intern(name))) {
                        return found;
                    }
                }
                return nullptr;
            }

        private:
            static pugi::xml_node FindIn(pugi::xml_node node, const char* name) {
                for (pugi::xml_node child : node.children()) {
                    if (std::string(child.name()) == "element" && std::string(child.attribute("name").as_string()) == name) {
                        return child;
                    }
                    if (pugi::xml_node found = FindIn(child, name)) {
                        return found;
                    }
                }
                return {};
            }
        };

        std::unique_ptr<Compiled> Compile(const std::string& html, const std::string& css = {}) {
            auto compiled = std::make_unique<Compiled>();
            const std::string source = css.empty() ? html : "<style>" + css + "</style>" + html;
            compiled->xml = UIWebCompiler::Compile(eastl::string_view(source.data(), source.size()), {}, compiled->report);
            EXPECT_TRUE(compiled->document.load_string(compiled->xml.c_str())) << compiled->xml.c_str();
            XmlDeserializer deserializer;
            if (deserializer.LoadFromString(compiled->xml)) {
                compiled->content.Deserialize(deserializer);
                compiled->deserializeErrors = deserializer.HasErrors();
            }
            return compiled;
        }

        /// The same screen the way the engine's own front-end writes it, for comparison
        UIScreenContent ReadXml(const std::string& xml) {
            UIScreenContent content;
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
            content.Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            return content;
        }

        /// A canvas layout of a screen, updated for an output
        struct LaidOut {
            UICanvasLayout layout;
            explicit LaidOut(const UIScreenContent& content, float referenceWidth = 1280.0f, float referenceHeight = 720.0f) {
                layout.root->children = content.elements;
                layout.variants = content.variants;
                layout.params.space = CanvasSpace::Screen;
                layout.params.scaleMode = CanvasScaleMode::ScaleWithScreen;
                layout.params.referenceWidth = referenceWidth;
                layout.params.referenceHeight = referenceHeight;
                layout.params.match = 0.5f;
                layout.params.minScale = 0.7f;
                layout.params.useSafeArea = true;
                layout.Invalidate();
            }

            /// A canvas in the margins of a scene's view (the HUD of a game): the scene's view size is part of the context
            void InSceneView(float width, float height) {
                layout.params.space = CanvasSpace::SceneView;
                viewWidth = width;
                viewHeight = height;
                layout.Invalidate();
            }

            void At(float width, float height) {
                UILayoutContext context;
                context.outputWidth = width;
                context.outputHeight = height;
                context.sceneViewWidth = viewWidth;
                context.sceneViewHeight = viewHeight;
                layout.Update(context);
            }

            float viewWidth = 0.0f;
            float viewHeight = 0.0f;

            [[nodiscard]] const UIElement* Find(const char* name) const {
                return layout.root->FindByName(PoolString::Intern(name));
            }
        };

        void ExpectSameRect(const UIElement* expected, const UIElement* actual, const char* name) {
            ASSERT_NE(expected, nullptr) << name;
            ASSERT_NE(actual, nullptr) << name;
            EXPECT_NEAR(expected->rect.x, actual->rect.x, kTolerance) << name;
            EXPECT_NEAR(expected->rect.y, actual->rect.y, kTolerance) << name;
            EXPECT_NEAR(expected->rect.w, actual->rect.w, kTolerance) << name;
            EXPECT_NEAR(expected->rect.h, actual->rect.h, kTolerance) << name;
        }

        // The HUD of the plan's sketch (a plate with a score and a Reset button, and a portrait variant), once in the engine's XML and once in HTML + CSS
        constexpr const char* kHudHtml = R"(
<div id="Hud" class="hud">
  <span id="Score">Score: {score}</span>
  <button id="Reset" data-action="Match3Reset">Reset</button>
</div>)";

        constexpr const char* kHudCss = R"(
.hud    { anchor: top-left; left: 16px; top: 16px; width: 220px; height: 96px; background: #000000A0; }
#Score  { left: 12px; top: 10px; width: 150px; height: 32px; font-size: 28px; }
#Reset  { anchor: bottom-right; right: 12px; bottom: 10px; width: 96px; height: 48px; font-size: 24px; }
button  { background: rgb(40, 90, 60); }
button:hover { background: #3a6; }
@media (orientation: portrait) { .hud { anchor: top; left: 0; width: 90%; } }
)";

        constexpr const char* kHudXml = R"(<ui>
  <elements>
    <element type="UIElement" name="Hud" x="16" y="16" width="220" height="96" color="0,0,0,160" blocksInput="true">
      <children>
        <element type="UILabel" name="Score" x="12" y="10" width="150" height="32" text="Score: {score}" fontSize="28"/>
        <element type="UIButton" name="Reset" anchor="BottomRight" x="-12" y="-10" width="96" height="48" text="Reset" fontSize="24" action="Match3Reset" color="40,90,60,255" hoverColor="51,170,102,255"/>
      </children>
    </element>
  </elements>
  <variants>
    <variant when="orientation:portrait">
      <overrides><set element="Hud" anchor="Top" x="0" width="90%"/></overrides>
    </variant>
  </variants>
</ui>)";

        // The main menu of Match3 as the game ships it (games/Match3/config/SceneConfig.xml), and the same menu in HTML + CSS
        constexpr const char* kMenuXml = R"(<ui><elements>
  <element type="UIStack" name="Column" anchor="Center" width="90%" maxWidth="320" height="232" direction="Vertical" spacing="16" crossAlign="Stretch">
    <children>
      <element type="UILabel" name="Title" height="88" text="Match 3" fontSize="56" align="Center" verticalAlign="Center" />
      <element type="UIButton" name="Play" height="56" text="Play" fontSize="28" action="Match3Play" color="40,90,60,255" />
      <element type="UIButton" name="Quit" height="56" text="Quit" fontSize="28" action="Match3Quit" color="90,50,50,255" />
    </children>
  </element>
</elements></ui>)";

        constexpr const char* kMenuHtml = R"(
<div id="Column">
  <h1 id="Title">Match 3</h1>
  <button id="Play" data-action="Match3Play">Play</button>
  <button id="Quit" data-action="Match3Quit">Quit</button>
</div>)";

        constexpr const char* kMenuCss = R"(
#Column { anchor: center; display: flex; flex-direction: column; align-items: stretch; gap: 16px; width: 90%; max-width: 320px; height: 232px; }
#Title  { height: 88px; font-size: 56px; text-align: center; vertical-align: middle; }
button  { height: 56px; font-size: 28px; }
#Play   { background: rgb(40, 90, 60); }
#Quit   { background: rgb(90, 50, 50); }
)";

        // The HUD of Match3 as the game ships it (games/Match3/config/SceneConfig.xml), and the same HUD in HTML + CSS
        constexpr const char* kMatch3HudXml = R"(<ui><elements>
  <element type="UILabel" name="Score" x="96" y="12" width="250" height="40" text="Score: {score}" fontSize="28" verticalAlign="Center" />
  <element type="UIStack" name="Buttons" anchor="TopRight" x="-96" y="12" width="260" height="48" direction="Horizontal" spacing="12" crossAlign="Stretch">
    <children>
      <element type="UIButton" name="Reset" width="124" height="48" text="Reset" fontSize="24" action="Match3Reset" color="40,90,60,255" />
      <element type="UIButton" name="Menu" width="124" height="48" text="Menu" fontSize="24" action="Match3Menu" color="70,70,100,255" />
    </children>
  </element>
  <element type="UILabel" name="Seed" anchor="BottomLeft" x="96" y="-26" width="300" height="24" text="seed {seed}" fontSize="16" textColor="190,190,190,255" />
</elements></ui>)";

        constexpr const char* kMatch3HudHtml = R"(
<span id="Score">Score: {score}</span>
<div id="Buttons">
  <button id="Reset" data-action="Match3Reset">Reset</button>
  <button id="Menu" data-action="Match3Menu">Menu</button>
</div>
<span id="Seed">seed {seed}</span>)";

        constexpr const char* kMatch3HudCss = R"(
#Score   { left: 96px; top: 12px; width: 250px; height: 40px; font-size: 28px; vertical-align: middle; }
#Buttons { anchor: top-right; right: 96px; top: 12px; width: 260px; height: 48px; display: flex; gap: 12px; align-items: stretch; }
button   { width: 124px; height: 48px; font-size: 24px; }
#Reset   { background: rgb(40, 90, 60); }
#Menu    { background: rgb(70, 70, 100); }
#Seed    { left: 96px; bottom: 26px; width: 300px; height: 24px; font-size: 16px; color: rgb(190, 190, 190); }
)";

        // The plate of the choice panel of UISelectorScreenTest (the panel shape of the Chess opponent panel), without the dim behind it, and the same plate in HTML + CSS.
        // The XML is that test's, with `selectedColor` on every segment and `blocksInput` on the filled groups (a background takes clicks in the HTML front-end)
        constexpr const char* kPanelXml = R"(<ui><elements>
  <element type="UIStack" name="Plate" anchor="Center" width="94%" maxWidth="640" height="416" direction="Vertical" spacing="12" padding="16" crossAlign="Stretch" color="36,40,52,255" blocksInput="true">
    <children>
      <element type="UILabel" name="Title" height="44" text="Opponent" fontSize="30" align="Center" />
      <element type="UIStack" name="ModeRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
        <children>
          <element type="UILabel" name="ModeCaption" width="150" height="56" text="Opponent" fontSize="24" verticalAlign="Center" />
          <element type="UISegmented" name="Mode" bind="opponentMode" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255" blocksInput="true">
            <children>
              <element type="UIButton" name="ModeTwo" flex="1" height="56" text="Two players" fontSize="24" value="two" action="SelectorMode" color="70,70,100,255" selectedColor="110,130,200,255" />
              <element type="UIButton" name="ModeComputer" flex="1" height="56" text="Computer" fontSize="24" value="computer" action="SelectorMode" color="70,70,100,255" selectedColor="110,130,200,255" />
            </children>
          </element>
        </children>
      </element>
      <element type="UIStack" name="ColorRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
        <children>
          <element type="UILabel" name="ColorCaption" width="150" height="56" text="Play as" fontSize="24" verticalAlign="Center" />
          <element type="UISegmented" name="Color" bind="opponentColor" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255" blocksInput="true">
            <children>
              <element type="UIButton" name="ColorWhite" flex="1" height="56" text="White" fontSize="24" value="white" action="SelectorColor" color="70,70,100,255" selectedColor="110,130,200,255" />
              <element type="UIButton" name="ColorBlack" flex="1" height="56" text="Black" fontSize="24" value="black" action="SelectorColor" color="70,70,100,255" selectedColor="110,130,200,255" />
            </children>
          </element>
        </children>
      </element>
      <element type="UIStack" name="EngineRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
        <children>
          <element type="UILabel" name="EngineCaption" width="150" height="56" text="Engine" fontSize="24" verticalAlign="Center" />
          <element type="UISegmented" name="Engine" bind="opponentEngine" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255" blocksInput="true">
            <children>
              <element type="UIButton" name="EngineStockfish" flex="1" height="56" text="Stockfish" fontSize="24" value="stockfish" action="SelectorEngine" color="70,70,100,255" selectedColor="110,130,200,255" />
              <element type="UIButton" name="EngineOwn" flex="1" height="56" text="Own engine" fontSize="24" value="own" action="SelectorEngine" color="70,70,100,255" selectedColor="110,130,200,255" />
            </children>
          </element>
        </children>
      </element>
      <element type="UIStack" name="LevelRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
        <children>
          <element type="UILabel" name="LevelCaption" width="150" height="56" text="Level" fontSize="24" verticalAlign="Center" />
          <element type="UISegmented" name="Level" bind="opponentLevel" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255" blocksInput="true">
            <children>
              <element type="UIButton" name="LevelEasy" flex="1" height="56" text="Easy" fontSize="24" value="easy" action="SelectorLevel" color="70,70,100,255" selectedColor="110,130,200,255" />
              <element type="UIButton" name="LevelMedium" flex="1" height="56" text="Medium" fontSize="24" value="medium" action="SelectorLevel" color="70,70,100,255" selectedColor="110,130,200,255" />
              <element type="UIButton" name="LevelHard" flex="1" height="56" text="Hard" fontSize="24" value="hard" action="SelectorLevel" color="70,70,100,255" selectedColor="110,130,200,255" />
              <element type="UIButton" name="LevelMax" flex="1" height="56" text="Max" fontSize="24" value="max" action="SelectorLevel" color="70,70,100,255" selectedColor="110,130,200,255" />
            </children>
          </element>
        </children>
      </element>
      <element type="UIButton" name="Ok" height="56" text="OK" fontSize="26" action="SelectorClose" color="40,90,60,255" />
    </children>
  </element>
</elements>
<variants>
  <variant when="orientation:portrait">
    <overrides>
      <set element="Plate" height="576" />
      <set element="ModeRow" direction="Vertical" height="96" />
      <set element="ModeCaption" height="32" />
      <set element="ColorRow" direction="Vertical" height="96" />
      <set element="ColorCaption" height="32" />
      <set element="EngineRow" direction="Vertical" height="96" />
      <set element="EngineCaption" height="32" />
      <set element="LevelRow" direction="Vertical" height="96" />
      <set element="LevelCaption" height="32" />
      <set element="LevelEasy" fontSize="22" />
      <set element="LevelMedium" fontSize="22" />
      <set element="LevelHard" fontSize="22" />
      <set element="LevelMax" fontSize="22" />
    </overrides>
  </variant>
</variants></ui>)";

        constexpr const char* kPanelHtml = R"(
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

        constexpr const char* kPanelCss = R"(
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

        /// Every attribute of every `element` of @p expected is on the same element of @p actual with the same text, and the other way round: the two XML texts say the same.
        /// Three spellings are the same thing and are not compared: a `UIStack` is a `UIElement` with `layout="Stack"` (the compile writes the second), and a `Vertical`
        /// direction is the default and is not written
        void ExpectSameAttributes(pugi::xml_node expected, pugi::xml_node actual) {
            ASSERT_EQ(std::string(expected.attribute("name").as_string()), std::string(actual.attribute("name").as_string()));
            const std::string name = expected.attribute("name").as_string();
            const auto skipped = [](const pugi::xml_attribute& attribute, const pugi::xml_node& owner) {
                const std::string key = attribute.name();
                return key == "layout" || (key == "type" && std::string(attribute.as_string()) == "UIStack" && !owner.attribute("bind")) ||
                       (key == "direction" && std::string(attribute.as_string()) == "Vertical");
            };
            for (pugi::xml_attribute attribute : expected.attributes()) {
                if (!skipped(attribute, expected)) {
                    EXPECT_EQ(std::string(attribute.as_string()), std::string(actual.attribute(attribute.name()).as_string())) << name << "." << attribute.name();
                }
            }
            for (pugi::xml_attribute attribute : actual.attributes()) {
                if (!skipped(attribute, actual) && std::string(attribute.name()) != "type") {
                    EXPECT_FALSE(expected.attribute(attribute.name()).empty()) << name << " has the extra attribute " << attribute.name();
                }
            }
            const pugi::xml_node expectedChildren = expected.child("children");
            const pugi::xml_node actualChildren = actual.child("children");
            pugi::xml_node a = actualChildren.first_child();
            for (pugi::xml_node e : expectedChildren.children()) {
                ASSERT_TRUE(a) << name << " lacks a child " << e.attribute("name").as_string();
                ExpectSameAttributes(e, a);
                a = a.next_sibling();
            }
            EXPECT_FALSE(a) << name << " has more children than expected";
        }

        const char* const kPanelNames[] = {"Plate",      "Title",       "ModeRow",   "ModeCaption",   "Mode",   "ModeTwo",         "ModeComputer", "ColorRow", "ColorCaption", "Color",
                                           "ColorWhite", "ColorBlack",  "EngineRow", "EngineCaption", "Engine", "EngineStockfish", "EngineOwn",    "LevelRow", "LevelCaption", "Level",
                                           "LevelEasy",  "LevelMedium", "LevelHard", "LevelMax",      "Ok"};

        // The plate of the settings panel of UISettingsScreenTest (kSettingsXml: the `Plate` element of its `Settings` canvas, with its portrait variant), without the dim behind
        // it and without the `Hud` canvas that opens it. **This mirrors that constant** (it is in an anonymous namespace of that file, so it is copied, as kPanelXml copies the
        // selector panel): when the settings screen changes, change this too. The one difference is `blocksInput="true"` on the Language group, because a background takes clicks
        // in the HTML front-end (the same as kPanelXml).
        constexpr const char* kSettingsPanelXml = R"(<ui><elements>
  <element type="UIStack" name="Plate" anchor="Center" width="94%" maxWidth="640" height="416" direction="Vertical" spacing="12" padding="16" crossAlign="Stretch" color="36,40,52,255" blocksInput="true">
    <children>
      <element type="UILabel" name="Title" height="44" text="{#settings.title}" fontSize="30" align="Center"/>
      <element type="UIToggle" name="Sound" bind="soundOn" action="SettingsSound" height="56" text="{#settings.sound}" fontSize="24" verticalAlign="Center"/>
      <element type="UIStack" name="MusicRow" height="56" direction="Horizontal" spacing="8" crossAlign="Center">
        <children>
          <element type="UILabel" name="MusicCaption" width="220" height="56" text="{#settings.music}" fontSize="24" verticalAlign="Center"/>
          <element type="UISlider" name="Volume" bind="volume" action="SettingsVolume" minValue="0" maxValue="1" step="0.1" flex="1" height="56"/>
        </children>
      </element>
      <element type="UIStack" name="DownloadRow" height="56" direction="Horizontal" spacing="8" crossAlign="Center">
        <children>
          <element type="UILabel" name="DownloadCaption" width="220" height="56" text="{#settings.download}" fontSize="24" verticalAlign="Center"/>
          <element type="UIProgressBar" name="Progress" bind="progress" minValue="0" maxValue="100" flex="1" height="24"/>
          <element type="UILabel" name="Percent" width="64" height="56" text="{progress}%" fontSize="24" align="Right" verticalAlign="Center"/>
        </children>
      </element>
      <element type="UIStack" name="LanguageRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
        <children>
          <element type="UILabel" name="LanguageCaption" width="220" height="56" text="{#settings.language}" fontSize="24" verticalAlign="Center"/>
          <element type="UISegmented" name="Language" bind="language" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255" blocksInput="true">
            <children>
              <element type="UIButton" name="LanguageEn" flex="1" height="56" text="{#lang.en}" fontSize="24" value="en" action="SettingsLanguage" color="70,70,100,255"/>
              <element type="UIButton" name="LanguageRu" flex="1" height="56" text="{#lang.ru}" fontSize="24" value="ru" action="SettingsLanguage" color="70,70,100,255"/>
              <element type="UIButton" name="LanguageDe" flex="1" height="56" text="{#lang.de}" fontSize="24" value="de" action="SettingsLanguage" color="70,70,100,255"/>
            </children>
          </element>
        </children>
      </element>
      <element type="UIButton" name="Ok" height="56" text="{#settings.ok}" fontSize="26" action="SettingsClose" color="40,90,60,255"/>
    </children>
  </element>
</elements>
<variants>
  <variant when="orientation:portrait">
    <overrides>
      <set element="Plate" height="456"/>
      <set element="LanguageRow" direction="Vertical" height="96"/>
      <set element="LanguageCaption" height="32"/>
    </overrides>
  </variant>
</variants></ui>)";

        constexpr const char* kSettingsPanelHtml = R"(
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

        constexpr const char* kSettingsPanelCss = R"(
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

        const char* const kSettingsPanelNames[] = {"Plate",       "Title",           "Sound",    "MusicRow",   "MusicCaption", "Volume",     "DownloadRow", "DownloadCaption", "Progress", "Percent",
                                                   "LanguageRow", "LanguageCaption", "Language", "LanguageEn", "LanguageRu",   "LanguageDe", "Ok"};

    }  // namespace

    class UIWebTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
            CoreManager::GetResourceManager().ClearCache();
        }
    };

    // ---- the text forms ------------------------------------------------------------------------------------------------------

    TEST_F(UIWebTest, CssColorsInTheFormsAScreenUses) {
        using UIWebText::ParseColor;
        EXPECT_EQ(ParseColor("#fff")->r, 255);
        EXPECT_EQ(ParseColor("#3a6")->g, 0xAA);
        EXPECT_EQ(ParseColor("#102030")->b, 0x30);
        EXPECT_EQ(ParseColor("#10203040")->a, 0x40);
        const auto rgb = ParseColor("rgb(40, 90, 60)");
        ASSERT_TRUE(rgb);
        EXPECT_EQ(rgb->r, 40);
        EXPECT_EQ(rgb->a, 255);
        EXPECT_EQ(ParseColor("rgba(0,0,0,.5)")->a, 128);
        EXPECT_EQ(ParseColor("rgb(0 0 0 / 50%)")->a, 128);
        EXPECT_EQ(ParseColor("rgb(100%, 0%, 0%)")->r, 255);
        EXPECT_EQ(ParseColor("transparent")->a, 0);
        EXPECT_EQ(ParseColor("WHITE")->g, 255);
        EXPECT_FALSE(ParseColor("not-a-color"));
        EXPECT_FALSE(ParseColor("#12"));
        EXPECT_FALSE(ParseColor("hsl(0, 0%, 0%)"));
    }

    TEST_F(UIWebTest, UrlsLoseTheirQuotes) {
        EXPECT_EQ(*UIWebText::ParseUrl("url(Panel9)"), "Panel9");
        EXPECT_EQ(*UIWebText::ParseUrl("url('ui/panel.png')"), "ui/panel.png");
        EXPECT_EQ(*UIWebText::ParseUrl("URL( \"a b\" )"), "a b");
        EXPECT_FALSE(UIWebText::ParseUrl("Panel9"));
    }

    TEST_F(UIWebTest, WordsAndPartsKeepParenthesesTogether) {
        const auto words = UIWebText::SplitWords("url(a b) rgba(0, 0, 0, .5)  none");
        ASSERT_EQ(words.size(), 3u);
        EXPECT_EQ(words[1], "rgba(0, 0, 0, .5)");
        const auto parts = UIWebText::SplitTopLevel("a; url(x;y); 'p;q'", ';');
        ASSERT_EQ(parts.size(), 3u);
        EXPECT_EQ(parts[1], "url(x;y)");
        EXPECT_EQ(parts[2], "'p;q'");
        EXPECT_EQ(*UIWebText::ParsePixels("12px"), 12.0f);
        EXPECT_EQ(*UIWebText::ParsePixels(" -3 "), -3.0f);
        EXPECT_FALSE(UIWebText::ParsePixels("2em"));
    }

    // ---- the HTML reader -----------------------------------------------------------------------------------------------------

    TEST_F(UIWebTest, HtmlTagsAttributesAndText) {
        UIWebReport report;
        const auto doc = UIHtmlParser::Parse(R"(<DIV id="a" class='x  y' data-n=5 hidden>Hi <b>there</b></DIV>)", report);
        EXPECT_FALSE(report.HasIssues());
        ASSERT_GE(doc.nodes.size(), 4u);
        const UIHtmlNode& div = doc.nodes[doc.nodes[0].children.at(0)];
        EXPECT_EQ(div.tag, "div");
        EXPECT_EQ(*div.FindAttribute("id"), "a");
        EXPECT_EQ(*div.FindAttribute("data-n"), "5");
        EXPECT_TRUE(div.HasAttribute("hidden"));
        EXPECT_TRUE(div.HasClass("x"));
        EXPECT_TRUE(div.HasClass("y"));
        EXPECT_FALSE(div.HasClass("z"));
        ASSERT_EQ(div.children.size(), 2u);
        EXPECT_EQ(doc.nodes[div.children[0]].text, "Hi ");
        EXPECT_EQ(doc.nodes[div.children[1]].tag, "b");
    }

    TEST_F(UIWebTest, HtmlEntitiesAreDecoded) {
        UIWebReport report;
        const auto doc = UIHtmlParser::Parse(R"(<p title="a &amp; b">1 &lt; 2 &#65;&#x42; &unknown; &copy</p>)", report);
        const UIHtmlNode& p = doc.nodes[doc.nodes[0].children.at(0)];
        EXPECT_EQ(*p.FindAttribute("title"), "a & b");
        EXPECT_EQ(doc.nodes[p.children.at(0)].text, "1 < 2 AB &unknown; &copy");
    }

    TEST_F(UIWebTest, HtmlVoidElementsCommentsAndDoctype) {
        UIWebReport report;
        const auto doc = UIHtmlParser::Parse("<!DOCTYPE html><!-- a <b> comment -->\n<div><img src=\"x\"><br><span>t</span></div>", report);
        EXPECT_FALSE(report.HasIssues());
        const UIHtmlNode* div = nullptr;
        for (const UIHtmlNode& node : doc.nodes) {
            if (node.tag == "div") {
                div = &node;
            }
        }
        ASSERT_NE(div, nullptr);
        ASSERT_EQ(div->children.size(), 3u) << "img and br have no children and do not swallow the span";
        EXPECT_EQ(doc.nodes[div->children[0]].tag, "img");
        EXPECT_EQ(doc.nodes[div->children[1]].tag, "br");
        EXPECT_EQ(doc.nodes[div->children[2]].tag, "span");
    }

    TEST_F(UIWebTest, StyleTextIsRawAndLinesAreCounted) {
        UIWebReport report;
        const auto doc = UIHtmlParser::Parse("<style>\n a > b { c: d; }\n</style>\n<div>\n</div>\n<p>x</p>", report);
        const UIHtmlNode* style = nullptr;
        const UIHtmlNode* p = nullptr;
        for (const UIHtmlNode& node : doc.nodes) {
            if (node.tag == "style") {
                style = &node;
            } else if (node.tag == "p") {
                p = &node;
            }
        }
        ASSERT_NE(style, nullptr);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(doc.nodes[style->children.at(0)].text, "\n a > b { c: d; }\n");
        EXPECT_EQ(style->line, 1u);
        EXPECT_EQ(p->line, 6u);
    }

    TEST_F(UIWebTest, HtmlThatIsNotClosedIsReported) {
        UIWebReport report;
        const auto doc = UIHtmlParser::Parse("<div><span>a</div></p><b>", report);
        EXPECT_FALSE(doc.nodes.empty());
        EXPECT_EQ(report.issues.size(), 2u);
        EXPECT_TRUE(report.issues[0].message.find("</p>") != eastl::string::npos);
        EXPECT_TRUE(report.issues[1].message.find("<b>") != eastl::string::npos);
    }

    // ---- the CSS reader ------------------------------------------------------------------------------------------------------

    TEST_F(UIWebTest, CssRulesSelectorsAndSpecificity) {
        UIWebReport report;
        UICssSheet sheet;
        UICssParser::ParseInto(sheet, "/* c */ a, #b.c:hover, div > span p { x: 1; y: 2 !important ; }\n.d{z:3}", "css", 1, report);
        EXPECT_FALSE(report.HasIssues()) << report.issues[0].message.c_str();
        ASSERT_EQ(sheet.rules.size(), 2u);
        const UICssRule& first = sheet.rules[0];
        ASSERT_EQ(first.selectors.size(), 3u);
        EXPECT_EQ(first.selectors[0].specificity, 1u);
        EXPECT_EQ(first.selectors[1].specificity, (1u << 16) | (2u << 8));
        EXPECT_EQ(first.selectors[1].compounds[0].pseudo, UICssPseudo::Hover);
        ASSERT_EQ(first.selectors[2].compounds.size(), 3u);
        EXPECT_EQ(first.selectors[2].compounds[1].combinator, UICssCombinator::Child);
        EXPECT_EQ(first.selectors[2].compounds[2].combinator, UICssCombinator::Descendant);
        ASSERT_EQ(first.declarations.size(), 2u);
        EXPECT_FALSE(first.declarations[0].important);
        EXPECT_TRUE(first.declarations[1].important);
        EXPECT_EQ(first.declarations[1].value, "2");
        EXPECT_EQ(sheet.rules[1].declarations[0].line, 2u);
    }

    TEST_F(UIWebTest, CssDeclarationValuesMayHoldSemicolonsInParentheses) {
        UIWebReport report;
        UICssSheet sheet;
        UICssParser::ParseInto(sheet, "a { background: url(data:image/png;base64,AAA) no-repeat; color: red }", "css", 1, report);
        ASSERT_EQ(sheet.rules.size(), 1u);
        ASSERT_EQ(sheet.rules[0].declarations.size(), 2u);
        EXPECT_EQ(sheet.rules[0].declarations[0].value, "url(data:image/png;base64,AAA) no-repeat");
    }

    TEST_F(UIWebTest, MediaQueriesBecomeVariantConditions) {
        UIWebReport report;
        UICssSheet sheet;
        UICssParser::ParseInto(sheet,
                               "@media (orientation: portrait) { a { x: 1 } }"
                               "@media screen and (min-width: 600px) and (max-width: 900px), (min-aspect-ratio: 16/9) { b { x: 2 } }",
                               "css", 1, report);
        EXPECT_FALSE(report.HasIssues()) << report.issues[0].message.c_str();
        ASSERT_EQ(sheet.medias.size(), 2u);
        EXPECT_EQ(sheet.medias[0].alternatives.at(0).orientation, UIOrientation::Portrait);
        ASSERT_EQ(sheet.medias[1].alternatives.size(), 2u);
        EXPECT_EQ(sheet.medias[1].alternatives[0].minWidth, 600.0f);
        EXPECT_EQ(sheet.medias[1].alternatives[0].maxWidth, 900.0f);
        EXPECT_NEAR(sheet.medias[1].alternatives[1].minAspect, 16.0f / 9.0f, 1e-4f);
        ASSERT_EQ(sheet.rules.size(), 2u);
        EXPECT_EQ(sheet.rules[0].media, 0);
        EXPECT_EQ(sheet.rules[1].media, 1);
    }

    TEST_F(UIWebTest, WhatIsOutsideTheSubsetIsReportedAndTheRestStays) {
        UIWebReport report;
        UICssSheet sheet;
        UICssParser::ParseInto(sheet,
                               "@import url(x.css);\n"
                               "@keyframes k { from { a: b } to { a: c } }\n"
                               "a[href], b + c, d:first-child, e::before, ok { x: 1 }\n"
                               "@media print { p { x: 1 } }\n"
                               "@media (min-width: 10em) { p { x: 1 } }\n"
                               "@media (color) { p { x: 1 } }\n",
                               "css", 1, report);
        ASSERT_EQ(sheet.rules.size(), 1u) << "only 'ok' survives";
        EXPECT_EQ(sheet.rules[0].selectors.size(), 1u);
        EXPECT_EQ(sheet.rules[0].selectors[0].compounds[0].tag, "ok");
        EXPECT_TRUE(sheet.medias.empty());
        // @import, @keyframes, four selectors, three media queries
        EXPECT_EQ(report.issues.size(), 9u);
    }

    // ---- one screen, two front-ends ------------------------------------------------------------------------------------------

    TEST_F(UIWebTest, TheHudInHtmlAndCssBuildsTheTreeOfTheXml) {
        const auto compiled = Compile(kHudHtml, kHudCss);
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_FALSE(compiled->report.HasIssues()) << compiled->report.issues[0].message.c_str() << " / " << compiled->xml.c_str();

        const UIScreenContent expected = ReadXml(kHudXml);
        ExpectSameElements(expected.elements, compiled->content.elements);
        EXPECT_TRUE(expected.variants == compiled->content.variants) << compiled->xml.c_str();

        // The fields the tree comparison does not look at
        const auto* score = dynamic_cast<UILabel*>(compiled->Element("Score"));
        ASSERT_NE(score, nullptr);
        EXPECT_EQ(score->text, "Score: {score}");
        const auto* reset = dynamic_cast<UIButton*>(compiled->Element("Reset"));
        ASSERT_NE(reset, nullptr);
        EXPECT_EQ(reset->text, "Reset");
        EXPECT_EQ(reset->action.ToStringView(), "Match3Reset");
        EXPECT_EQ(reset->hoverColor.r, 51);
        EXPECT_EQ(reset->hoverColor.g, 170);
        EXPECT_EQ(compiled->report.elements, 3u);
        EXPECT_EQ(compiled->report.variants, 1u);
        EXPECT_EQ(compiled->report.mediaBlocks, 1u);
    }

    TEST_F(UIWebTest, TheHudInHtmlLaysOutLikeTheXml) {
        const auto compiled = Compile(kHudHtml, kHudCss);
        LaidOut web(compiled->content);
        const UIScreenContent expectedContent = ReadXml(kHudXml);
        LaidOut xml(expectedContent);
        for (const auto& [width, height] : {std::pair{1280.0f, 720.0f}, {640.0f, 360.0f}, {720.0f, 1280.0f}, {480.0f, 480.0f}, {2560.0f, 1080.0f}}) {
            web.At(width, height);
            xml.At(width, height);
            for (const char* name : {"Hud", "Score", "Reset"}) {
                SCOPED_TRACE(Format("{} at {}x{}", name, width, height).c_str());
                ExpectSameRect(xml.Find(name), web.Find(name), name);
            }
        }
    }

    TEST_F(UIWebTest, TheMenuInHtmlAndCssLaysOutLikeTheShippedXml) {
        const auto compiled = Compile(kMenuHtml, kMenuCss);
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_FALSE(compiled->report.HasIssues()) << compiled->report.issues[0].message.c_str() << " / " << compiled->xml.c_str();

        const UIScreenContent shipped = ReadXml(kMenuXml);
        LaidOut web(compiled->content);
        LaidOut xml(shipped);
        for (const auto& [width, height] : {std::pair{1280.0f, 720.0f}, {640.0f, 360.0f}, {720.0f, 1280.0f}, {480.0f, 480.0f}, {3840.0f, 1080.0f}}) {
            web.At(width, height);
            xml.At(width, height);
            for (const char* name : {"Column", "Title", "Play", "Quit"}) {
                SCOPED_TRACE(Format("{} at {}x{}", name, width, height).c_str());
                ExpectSameRect(xml.Find(name), web.Find(name), name);
            }
        }
        const auto* title = dynamic_cast<UILabel*>(compiled->Element("Title"));
        ASSERT_NE(title, nullptr);
        EXPECT_EQ(title->align, TextAlign::Center);
        EXPECT_EQ(title->verticalAlign, UIVerticalAlign::Center);
        EXPECT_EQ(compiled->Element("Column")->layout, UILayoutKind::Stack);
    }

    TEST_F(UIWebTest, TheMenuInHtmlPassesTheSizeMatrix) {
        const auto compiled = Compile(kMenuHtml, kMenuCss);
        LaidOut web(compiled->content);
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const auto failures = ValidateOnMatrix(web.layout, options);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(UIWebTest, TheMatch3HudInHtmlLaysOutLikeTheShippedXml) {
        const auto compiled = Compile(kMatch3HudHtml, kMatch3HudCss);
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_FALSE(compiled->report.HasIssues()) << compiled->report.issues[0].message.c_str() << " / " << compiled->xml.c_str();

        const UIScreenContent shipped = ReadXml(kMatch3HudXml);
        LaidOut web(compiled->content);
        LaidOut xml(shipped);
        web.InSceneView(712.0f, 672.0f);
        xml.InSceneView(712.0f, 672.0f);
        for (const auto& [width, height] : {std::pair{1280.0f, 720.0f}, {480.0f, 480.0f}, {720.0f, 1280.0f}, {2560.0f, 1080.0f}}) {
            web.At(width, height);
            xml.At(width, height);
            for (const char* name : {"Score", "Buttons", "Reset", "Menu", "Seed"}) {
                SCOPED_TRACE(Format("{} at {}x{}", name, width, height).c_str());
                ExpectSameRect(xml.Find(name), web.Find(name), name);
            }
        }
        EXPECT_EQ(dynamic_cast<UILabel*>(compiled->Element("Seed"))->textColor.r, 190);
    }

    TEST_F(UIWebTest, TheMatch3HudInHtmlPassesTheSizeMatrixInTheMarginsOfItsView) {
        constexpr float kViewWidth = 712.0f;
        constexpr float kViewHeight = 672.0f;
        const auto compiled = Compile(kMatch3HudHtml, kMatch3HudCss);
        LaidOut web(compiled->content);
        web.InSceneView(kViewWidth, kViewHeight);
        // The longest words the game can show
        CoreManager::GetUIVars().Set("score"_intern, 99999999);
        CoreManager::GetUIVars().Set("seed"_intern, 4294967295u);
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        UILayoutContext base;
        base.sceneViewWidth = kViewWidth;
        base.sceneViewHeight = kViewHeight;
        const auto failures = ValidateOnMatrix(web.layout, options, base);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    // ---- choice groups -------------------------------------------------------------------------------------------------------

    TEST_F(UIWebTest, ThePanelInHtmlAndCssBuildsTheElementsOfTheXml) {
        const auto compiled = Compile(kPanelHtml, kPanelCss);
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_FALSE(compiled->report.HasIssues()) << compiled->report.issues[0].message.c_str() << " / " << compiled->xml.c_str();

        pugi::xml_document reference;
        ASSERT_TRUE(reference.load_string(kPanelXml));
        ExpectSameAttributes(reference.child("ui").child("elements").child("element"), compiled->document.child("ui").child("elements").child("element"));

        const UIScreenContent expected = ReadXml(kPanelXml);
        ExpectSameElements(expected.elements, compiled->content.elements);
        EXPECT_TRUE(expected.variants == compiled->content.variants) << compiled->xml.c_str();
        EXPECT_EQ(compiled->report.variants, 1u);

        // The fields the tree comparison does not look at
        const auto* level = dynamic_cast<UISegmented*>(compiled->Element("Level"));
        ASSERT_NE(level, nullptr) << "a data-widget=\"segmented\" div is a UISegmented";
        EXPECT_EQ(level->bind.ToStringView(), "opponentLevel");
        EXPECT_EQ(level->layout, UILayoutKind::Stack);
        const auto* max = dynamic_cast<UIButton*>(compiled->Element("LevelMax"));
        ASSERT_NE(max, nullptr);
        EXPECT_EQ(max->value.ToStringView(), "max");
        EXPECT_EQ(max->action.ToStringView(), "SelectorLevel");
        EXPECT_EQ(max->selectedColor.r, 110);
        EXPECT_EQ(max->selectedColor.g, 130);
        EXPECT_EQ(max->selectedColor.b, 200);
        EXPECT_EQ(max->hoverColor.a, 0) << "only :checked was written, so the hover color stays derived";
    }

    TEST_F(UIWebTest, ThePanelInHtmlLaysOutLikeTheXmlInLandscapeAndPortrait) {
        const auto compiled = Compile(kPanelHtml, kPanelCss);
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        const UIScreenContent expectedContent = ReadXml(kPanelXml);
        // The view of the Chess board (landscape: the portrait variant is off) and a portrait view (the variant is on)
        for (const auto& [viewWidth, viewHeight] : {std::pair{840.0f, 800.0f}, {560.0f, 840.0f}}) {
            LaidOut web(compiled->content);
            LaidOut xml(expectedContent);
            web.InSceneView(viewWidth, viewHeight);
            xml.InSceneView(viewWidth, viewHeight);
            for (const auto& [width, height] : {std::pair{1280.0f, 720.0f}, {640.0f, 360.0f}, {720.0f, 1280.0f}, {480.0f, 480.0f}, {2560.0f, 1080.0f}}) {
                web.At(width, height);
                xml.At(width, height);
                for (const char* name : kPanelNames) {
                    SCOPED_TRACE(Format("{} in a {}x{} view at {}x{}", name, viewWidth, viewHeight, width, height).c_str());
                    ExpectSameRect(xml.Find(name), web.Find(name), name);
                }
            }
        }
        // The portrait variant really changed the shape: the rows are taller in portrait than in landscape (each view lays the shared elements out in turn)
        const struct {
            float viewWidth;
            float viewHeight;
            float rowHeight;
        } shapes[] = {{840.0f, 800.0f, 56.0f}, {560.0f, 840.0f, 96.0f}};
        for (const auto& shape : shapes) {
            LaidOut web(compiled->content);
            web.InSceneView(shape.viewWidth, shape.viewHeight);
            web.At(1280.0f, 720.0f);
            EXPECT_FLOAT_EQ(web.Find("LevelRow")->rect.h, shape.rowHeight) << shape.viewWidth;
        }
    }

    TEST_F(UIWebTest, ThePanelInHtmlFollowsTheVariablesAndPassesItsClicksOn) {
        const auto compiled = Compile(kPanelHtml, kPanelCss);
        auto* color = dynamic_cast<UISegmented*>(compiled->Element("Color"));
        auto* white = dynamic_cast<UIButton*>(compiled->Element("ColorWhite"));
        auto* black = dynamic_cast<UIButton*>(compiled->Element("ColorBlack"));
        ASSERT_TRUE(color != nullptr && white != nullptr && black != nullptr);
        color->SyncChildren();
        EXPECT_FALSE(white->selected || black->selected) << "an unset variable selects nothing";
        CoreManager::GetUIVars().Set("opponentColor"_intern, "black");
        color->SyncChildren();
        EXPECT_FALSE(white->selected);
        EXPECT_TRUE(black->selected);
        EXPECT_EQ(color->GetSelectedValue(), "black");
    }

    TEST_F(UIWebTest, ASegmentedGroupTakesDisabledFromTheAttribute) {
        const auto compiled = Compile(R"(<div id="g" data-widget="segmented" data-bind="v" disabled><button id="a" data-value="x">A</button></div>)", "div { display: flex; height: 56px }");
        EXPECT_EQ(compiled->Attr("g", "disabled"), "true");
        EXPECT_EQ(compiled->Attr("g", "bind"), "v");
        EXPECT_EQ(compiled->Attr("a", "disabled"), "") << "the group greys its buttons at run time, the button is not disabled itself";
        EXPECT_EQ(compiled->Attr("a", "value"), "x");
        EXPECT_FALSE(compiled->report.HasIssues()) << compiled->report.issues[0].message.c_str();
        auto* group = dynamic_cast<UISegmented*>(compiled->Element("g"));
        ASSERT_NE(group, nullptr);
        EXPECT_TRUE(group->disabled);
        group->SyncChildren();
        EXPECT_TRUE(dynamic_cast<UIButton*>(compiled->Element("a"))->groupDisabled);
    }

    TEST_F(UIWebTest, CheckedAndHoverOnOneButton) {
        const auto compiled = Compile(R"(<button id="b" data-action="Go" data-value="v">Go</button>)",
                                      "button { background: #202020 } button:hover { background: #303030 } button:checked { background: #405060 } button:active { background: #101010 }");
        EXPECT_EQ(compiled->Attr("b", "color"), "32,32,32,255");
        EXPECT_EQ(compiled->Attr("b", "hoverColor"), "48,48,48,255");
        EXPECT_EQ(compiled->Attr("b", "selectedColor"), "64,80,96,255");
        EXPECT_EQ(compiled->Attr("b", "pressedColor"), "16,16,16,255");
        EXPECT_EQ(compiled->Attr("b", "value"), "v");
        EXPECT_TRUE(compiled->report.issues.empty());

        // `:checked` counts as a class: a longer selector wins over a plain one, the order breaks ties
        const auto specific =
            Compile(R"(<button id="b" class="c">Go</button>)", "button:checked { background: #111111 } button.c:checked { background: #222222 } button:checked { background: #333333 }");
        EXPECT_EQ(specific->Attr("b", "selectedColor"), "34,34,34,255");
        const auto onlyChecked = Compile(R"(<button id="b">Go</button>)", "button:checked { background: #111111 }");
        EXPECT_FALSE(onlyChecked->Has("b", "color")) << "a state rule changes nothing of the base look";
        EXPECT_EQ(onlyChecked->Attr("b", "selectedColor"), "17,17,17,255");
    }

    TEST_F(UIWebTest, ACheckedRuleCanChangeOnlyTheBackgroundOfAButton) {
        const auto compiled = Compile(R"(<button id="b">Go</button><div id="d"></div>)", "button:checked { background: #111111; font-size: 30px } div:checked { background: #222222 }");
        EXPECT_EQ(compiled->IssuesMentioning("'fontSize' of a <button>"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("'color' of a <div>"), 1u) << "the :checked rule of a div has no button to change";
        EXPECT_EQ(compiled->Attr("b", "selectedColor"), "17,17,17,255");
    }

    TEST_F(UIWebTest, ChoiceGroupsOutsideTheSubsetAreReported) {
        // An unknown widget is read as a plain element and reported
        const auto unknown = Compile(R"(<div id="g" data-widget="dropdown"></div>)");
        EXPECT_EQ(unknown->IssuesMentioning("data-widget=\"dropdown\" is not supported"), 1u);
        EXPECT_EQ(unknown->IssuesMentioning("\"segmented\", \"toggle\", \"slider\" and \"progress\""), 1u) << "the message names every widget";
        EXPECT_EQ(unknown->Find("g").attribute("type").as_string(), std::string("UIElement"));

        // data-bind with no widget, on a div and on a button
        const auto bind = Compile(R"(<div id="g" data-bind="v"></div><button id="b" data-bind="w">B</button>)");
        EXPECT_EQ(bind->IssuesMentioning("data-bind only applies"), 1u) << "one message for both, like every issue";
        EXPECT_FALSE(bind->Has("g", "bind"));
        EXPECT_FALSE(bind->Has("b", "bind"));

        // A group that is not display: flex still compiles (a UISegmented is a stack), and says so
        const auto noFlex = Compile(R"(<div id="g" data-widget="segmented" data-bind="v"><button id="a" data-value="x">A</button></div>)");
        EXPECT_EQ(noFlex->IssuesMentioning("needs display: flex"), 1u);
        EXPECT_EQ(noFlex->Attr("g", "bind"), "v");
        EXPECT_EQ(noFlex->Find("g").attribute("type").as_string(), std::string("UISegmented"));
        const auto flex = Compile(R"(<div id="g" data-widget="segmented"><button>A</button></div>)", "div { display: flex }");
        EXPECT_EQ(flex->IssuesMentioning("needs display: flex"), 0u);

        // data-value away from a button, and the widget on a button
        const auto value = Compile(R"(<div id="g" data-value="x"></div>)");
        EXPECT_EQ(value->IssuesMentioning("data-value only applies to a <button>"), 1u);
        EXPECT_FALSE(value->Has("g", "value"));
        const auto onButton = Compile(R"(<button id="b" data-widget="segmented">B</button>)");
        EXPECT_EQ(onButton->IssuesMentioning("on a <button> is left out"), 1u);
        EXPECT_EQ(onButton->Find("b").attribute("type").as_string(), std::string("UIButton"));

        // The widget name is not case sensitive, like a tag
        const auto upper = Compile(R"(<div id="g" data-widget="Segmented"></div>)", "div { display: flex }");
        EXPECT_EQ(upper->Find("g").attribute("type").as_string(), std::string("UISegmented"));
        EXPECT_TRUE(upper->report.issues.empty());
    }

    TEST_F(UIWebTest, AChoiceGroupKeepsItsCounters) {
        const auto compiled =
            Compile(R"(<div id="g" data-widget="segmented" data-bind="v"><button id="a" data-value="x">A</button><button id="b" data-value="y">B</button></div>)", "div { display: flex }");
        EXPECT_EQ(compiled->report.elements, 3u);
        EXPECT_EQ(compiled->report.rules, 1u);
        EXPECT_EQ(compiled->report.declarations, 1u);
    }

    // ---- toggle, slider and progress bar ------------------------------------------------------------------------------------------

    TEST_F(UIWebTest, TheSettingsPanelInHtmlAndCssBuildsTheElementsOfTheXml) {
        const auto compiled = Compile(kSettingsPanelHtml, kSettingsPanelCss);
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_FALSE(compiled->report.HasIssues()) << compiled->report.issues[0].message.c_str() << " / " << compiled->xml.c_str();

        pugi::xml_document reference;
        ASSERT_TRUE(reference.load_string(kSettingsPanelXml));
        ExpectSameAttributes(reference.child("ui").child("elements").child("element"), compiled->document.child("ui").child("elements").child("element"));

        const UIScreenContent expected = ReadXml(kSettingsPanelXml);
        ExpectSameElements(expected.elements, compiled->content.elements);
        EXPECT_TRUE(expected.variants == compiled->content.variants) << compiled->xml.c_str();
        EXPECT_EQ(compiled->report.variants, 1u);

        // The types and the fields the tree comparison does not look at
        const auto* sound = compiled->Element("Sound")->Cast<UIToggle>();
        ASSERT_NE(sound, nullptr) << "a data-widget=\"toggle\" div is a UIToggle";
        EXPECT_EQ(sound->bind.ToStringView(), "soundOn");
        EXPECT_EQ(sound->action.ToStringView(), "SettingsSound");
        EXPECT_EQ(sound->onValue.ToStringView(), "true") << "data-on and data-off were not written, so the defaults stand";
        EXPECT_EQ(sound->offValue.ToStringView(), "false");
        EXPECT_EQ(sound->text, "{#settings.sound}") << "the caption of a toggle is its text";
        EXPECT_TRUE(sound->interactive);
        const auto* volume = compiled->Element("Volume")->Cast<UISlider>();
        ASSERT_NE(volume, nullptr) << "a data-widget=\"slider\" div is a UISlider";
        EXPECT_EQ(volume->bind.ToStringView(), "volume");
        EXPECT_EQ(volume->action.ToStringView(), "SettingsVolume");
        EXPECT_FLOAT_EQ(volume->minValue, 0.0f);
        EXPECT_FLOAT_EQ(volume->maxValue, 1.0f);
        EXPECT_FLOAT_EQ(volume->step, 0.1f);
        const auto* progress = compiled->Element("Progress")->Cast<UIProgressBar>();
        ASSERT_NE(progress, nullptr) << "a data-widget=\"progress\" div is a UIProgressBar";
        EXPECT_EQ(progress->bind.ToStringView(), "progress");
        EXPECT_FLOAT_EQ(progress->maxValue, 100.0f);
        EXPECT_FALSE(progress->interactive);
        EXPECT_FALSE(progress->blocksInput) << "a progress bar takes no input";
    }

    TEST_F(UIWebTest, TheSettingsPanelInHtmlLaysOutLikeTheXmlInLandscapeAndPortrait) {
        const auto compiled = Compile(kSettingsPanelHtml, kSettingsPanelCss);
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        const UIScreenContent expectedContent = ReadXml(kSettingsPanelXml);
        // The view of the Chess board (landscape: the portrait variant is off) and a portrait view (the variant is on)
        for (const auto& [viewWidth, viewHeight] : {std::pair{840.0f, 800.0f}, {560.0f, 840.0f}}) {
            LaidOut web(compiled->content);
            LaidOut xml(expectedContent);
            web.InSceneView(viewWidth, viewHeight);
            xml.InSceneView(viewWidth, viewHeight);
            for (const auto& [width, height] : {std::pair{1280.0f, 720.0f}, {640.0f, 360.0f}, {720.0f, 1280.0f}, {480.0f, 480.0f}, {2560.0f, 1080.0f}}) {
                web.At(width, height);
                xml.At(width, height);
                for (const char* name : kSettingsPanelNames) {
                    SCOPED_TRACE(Format("{} in a {}x{} view at {}x{}", name, viewWidth, viewHeight, width, height).c_str());
                    ExpectSameRect(xml.Find(name), web.Find(name), name);
                }
            }
        }
        // The portrait variant really changed the shape, and the three widgets have a rect (a slider of height 56, a bar of 24)
        const struct {
            float viewWidth;
            float viewHeight;
            float plateHeight;
        } shapes[] = {{840.0f, 800.0f, 416.0f}, {560.0f, 840.0f, 456.0f}};
        for (const auto& shape : shapes) {
            LaidOut web(compiled->content);
            web.InSceneView(shape.viewWidth, shape.viewHeight);
            web.At(1280.0f, 720.0f);
            EXPECT_FLOAT_EQ(web.Find("Plate")->rect.h, shape.plateHeight) << shape.viewWidth;
            EXPECT_FLOAT_EQ(web.Find("Volume")->rect.h, 56.0f);
            EXPECT_GT(web.Find("Volume")->rect.w, 100.0f);
            EXPECT_FLOAT_EQ(web.Find("Progress")->rect.h, 24.0f);
            EXPECT_FLOAT_EQ(web.Find("Sound")->rect.h, 56.0f);
        }
    }

    TEST_F(UIWebTest, TheSettingsPanelInHtmlFollowsTheVariablesAndTheStrings) {
        const auto compiled = Compile(kSettingsPanelHtml, kSettingsPanelCss);
        UIStrings& strings = CoreManager::GetUIStrings();
        strings.Set("en"_intern, "settings.sound", "Sound");
        strings.Set("de"_intern, "settings.sound", "Geraeuscheffekte");
        auto* sound = compiled->Element("Sound")->Cast<UIToggle>();
        auto* volume = compiled->Element("Volume")->Cast<UISlider>();
        auto* progress = compiled->Element("Progress")->Cast<UIProgressBar>();
        ASSERT_TRUE(sound != nullptr && volume != nullptr && progress != nullptr);

        EXPECT_FALSE(sound->IsOn());
        EXPECT_FLOAT_EQ(progress->GetFraction(), 0.0f);
        CoreManager::GetUIVars().Set("soundOn"_intern, "true");
        CoreManager::GetUIVars().Set("volume"_intern, "0.5");
        CoreManager::GetUIVars().Set("progress"_intern, 25);
        EXPECT_TRUE(sound->IsOn());
        EXPECT_FLOAT_EQ(volume->GetValue(), 0.5f);
        EXPECT_FLOAT_EQ(volume->GetFraction(), 0.5f);
        EXPECT_FLOAT_EQ(progress->GetFraction(), 0.25f);

        EXPECT_EQ(sound->GetDisplayText(), "Sound");
        strings.SetLanguage("de"_intern);
        EXPECT_EQ(sound->GetDisplayText(), "Geraeuscheffekte") << "a {#key} caption of a toggle follows the language";
    }

    TEST_F(UIWebTest, TheWidgetsTakeTheirAttributesAndColors) {
        const auto compiled = Compile(R"(
<div id="t" data-widget="Toggle" data-bind="muted" action="Mute" data-on="1" data-off="0" disabled>Mute</div>
<div id="s" data-widget="slider" data-bind="v" data-action="Set" data-min="-5" data-max="5.5" data-step="0.5" disabled></div>
<div id="p" data-widget="progress" data-bind="load" data-max="200"></div>
<div id="d" data-widget="slider"></div>)",
                                      R"(
#t, #s, #p { -be-track-color: #20202080; -be-fill-color: rgb(10, 200, 30); }
#t, #s { -be-thumb-color: #ffeeddff; }
#t { background: rgb(1, 2, 3); }
#p { background: rgb(4, 5, 6); height: 24px; }
)");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_TRUE(compiled->report.issues.empty()) << compiled->report.issues[0].message.c_str();

        EXPECT_EQ(compiled->Find("t").attribute("type").as_string(), std::string("UIToggle")) << "the widget name is not case sensitive";
        EXPECT_EQ(compiled->Attr("t", "bind"), "muted");
        EXPECT_EQ(compiled->Attr("t", "action"), "Mute") << "the plain `action` attribute is accepted as on a button";
        EXPECT_EQ(compiled->Attr("t", "onValue"), "1");
        EXPECT_EQ(compiled->Attr("t", "offValue"), "0");
        EXPECT_EQ(compiled->Attr("t", "disabled"), "true");
        EXPECT_EQ(compiled->Attr("t", "text"), "Mute");
        EXPECT_EQ(compiled->Attr("t", "color"), "1,2,3,255") << "the background is the fill behind the caption";
        EXPECT_EQ(compiled->Attr("t", "trackColor"), "32,32,32,128");
        EXPECT_EQ(compiled->Attr("t", "fillColor"), "10,200,30,255");
        EXPECT_EQ(compiled->Attr("t", "thumbColor"), "255,238,221,255");
        EXPECT_FALSE(compiled->Has("t", "blocksInput")) << "a toggle takes input by itself, whatever its background";

        EXPECT_EQ(compiled->Attr("s", "minValue"), "-5");
        EXPECT_EQ(compiled->Attr("s", "maxValue"), "5.5");
        EXPECT_EQ(compiled->Attr("s", "step"), "0.5");
        EXPECT_EQ(compiled->Attr("s", "disabled"), "true");
        EXPECT_EQ(compiled->Attr("s", "thumbColor"), "255,238,221,255");
        EXPECT_EQ(compiled->Attr("s", "fillColor"), "10,200,30,255");

        EXPECT_EQ(compiled->Attr("p", "maxValue"), "200");
        EXPECT_FALSE(compiled->Has("p", "minValue"));
        EXPECT_EQ(compiled->Attr("p", "color"), "4,5,6,255");
        EXPECT_EQ(compiled->Attr("p", "trackColor"), "32,32,32,128");
        EXPECT_FALSE(compiled->Has("p", "thumbColor"));
        EXPECT_FALSE(compiled->Has("p", "blocksInput")) << "a progress bar never takes input, a background or not";

        EXPECT_FALSE(compiled->Has("d", "minValue")) << "nothing written, nothing invented";
        EXPECT_FALSE(compiled->Has("d", "step"));

        // and what the controls read from it
        const auto* toggle = compiled->Element("t")->Cast<UIToggle>();
        const auto* slider = compiled->Element("s")->Cast<UISlider>();
        ASSERT_TRUE(toggle != nullptr && slider != nullptr);
        EXPECT_TRUE(toggle->disabled);
        EXPECT_EQ(toggle->onValue.ToStringView(), "1");
        EXPECT_EQ(toggle->fillColor.g, 200);
        EXPECT_FLOAT_EQ(slider->minValue, -5.0f);
        EXPECT_FLOAT_EQ(slider->step, 0.5f);
        EXPECT_TRUE(slider->disabled);
    }

    TEST_F(UIWebTest, NumbersOfTheWidgetsAreWrittenAsNumbers) {
        const auto compiled = Compile(R"(<div id="s" data-widget="slider" data-min=" 0.0 " data-max="1e2" data-step="1.50"></div>)");
        EXPECT_EQ(compiled->Attr("s", "minValue"), "0");
        EXPECT_EQ(compiled->Attr("s", "maxValue"), "100");
        EXPECT_EQ(compiled->Attr("s", "step"), "1.5");
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    TEST_F(UIWebTest, WidgetMisuseIsReported) {
        // data-step, data-min, data-max, data-on, data-off, data-action and data-bind with no widget that reads them
        const auto stray = Compile(R"(<div id="a" data-step="1" data-min="0" data-max="1" data-on="x" data-off="y" data-action="Go" data-bind="v"></div>)");
        EXPECT_EQ(stray->IssuesMentioning("data-step only applies"), 1u);
        EXPECT_EQ(stray->IssuesMentioning("data-min only applies"), 1u);
        EXPECT_EQ(stray->IssuesMentioning("data-max only applies"), 1u);
        EXPECT_EQ(stray->IssuesMentioning("data-on only applies"), 1u);
        EXPECT_EQ(stray->IssuesMentioning("data-off only applies"), 1u);
        EXPECT_EQ(stray->IssuesMentioning("data-action only applies"), 1u);
        EXPECT_EQ(stray->IssuesMentioning("data-bind only applies"), 1u);
        for (const char* key : {"step", "minValue", "maxValue", "onValue", "offValue", "action", "bind"}) {
            EXPECT_FALSE(stray->Has("a", key)) << key << " has no widget to read it, so nothing is written";
        }

        // data-step on a progress bar, data-on on a slider, data-action on a progress bar and a segmented group
        const auto wrong = Compile(R"(<div id="p" data-widget="progress" data-step="1" data-action="Go"></div><div id="s" data-widget="slider" data-on="1"></div>)");
        EXPECT_EQ(wrong->IssuesMentioning("data-step only applies"), 1u);
        EXPECT_EQ(wrong->IssuesMentioning("data-action only applies"), 1u);
        EXPECT_EQ(wrong->IssuesMentioning("data-on only applies"), 1u);
        EXPECT_FALSE(wrong->Has("p", "step"));
        EXPECT_FALSE(wrong->Has("p", "action"));
        EXPECT_FALSE(wrong->Has("s", "onValue"));

        // a number that is not one is reported and the default stays
        const auto bad = Compile(R"(<div id="s" data-widget="slider" data-min="low" data-max="inf" data-step="-1"></div><div id="p" data-widget="progress" data-max="ten"></div>)");
        EXPECT_EQ(bad->IssuesMentioning("data-min=\"low\" is not a number"), 1u);
        EXPECT_EQ(bad->IssuesMentioning("data-max=\"inf\" is not a number"), 1u);
        EXPECT_EQ(bad->IssuesMentioning("data-max=\"ten\" is not a number"), 1u);
        EXPECT_EQ(bad->IssuesMentioning("data-step=\"-1\" is not a number of 0 or more"), 1u);
        for (const char* key : {"minValue", "maxValue", "step"}) {
            EXPECT_FALSE(bad->Has("s", key)) << key;
        }
        EXPECT_FALSE(bad->Has("p", "maxValue"));

        // a widget on a button or an image is left out, and a progress bar is not disabled
        const auto onButton = Compile(R"(<button id="b" data-widget="toggle">B</button><img id="i" src="x" data-widget="slider"><div id="p" data-widget="progress" disabled></div>)");
        EXPECT_EQ(onButton->IssuesMentioning("data-widget=\"toggle\" on a <button> is left out"), 1u);
        EXPECT_EQ(onButton->IssuesMentioning("data-widget=\"slider\" on a <img> is left out"), 1u);
        EXPECT_EQ(onButton->IssuesMentioning("disabled does not apply to a progress bar"), 1u);
        EXPECT_EQ(onButton->Find("b").attribute("type").as_string(), std::string("UIButton"));
        EXPECT_EQ(onButton->Find("i").attribute("type").as_string(), std::string("UIImage"));
        EXPECT_FALSE(onButton->Has("p", "disabled"));

        // content inside a slider or a progress bar has nowhere to go
        const auto inside = Compile(R"(<div id="s" data-widget="slider"><span id="x">hi</span></div>)");
        EXPECT_EQ(inside->IssuesMentioning("shows no content"), 1u);
        EXPECT_TRUE(inside->Find("x").empty()) << "its children are not elements";
    }

    TEST_F(UIWebTest, WidgetColorsOnAnythingElseAreReported) {
        const auto compiled = Compile(R"(<div id="d"></div><button id="b">B</button><span id="l">L</span><div id="p" data-widget="progress"></div>)",
                                      "#d, #b, #l { -be-fill-color: red; -be-track-color: red; } #p { -be-thumb-color: red; } #d { -be-thumb-color: red; }");
        EXPECT_EQ(compiled->IssuesMentioning("-be-fill-color applies to a toggle, slider or progress bar only"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("-be-track-color applies to a toggle, slider or progress bar only"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("-be-thumb-color applies to a toggle or slider only"), 1u) << "a progress bar has no thumb";
        for (const char* name : {"d", "b", "l", "p"}) {
            EXPECT_FALSE(compiled->Has(name, "fillColor")) << name;
            EXPECT_FALSE(compiled->Has(name, "trackColor")) << name;
            EXPECT_FALSE(compiled->Has(name, "thumbColor")) << name;
        }
        // The value is checked like any property
        const auto bad = Compile(R"(<div id="t" data-widget="toggle">T</div>)", "#t { -be-fill-color: notacolor; }");
        EXPECT_EQ(bad->IssuesMentioning("'-be-fill-color: notacolor' is not understood"), 1u);
        EXPECT_EQ(bad->report.declarationsUnsupported, 1u);
        EXPECT_FALSE(bad->Has("t", "fillColor"));
    }

    TEST_F(UIWebTest, AStateRuleOnAWidgetIsReportedAndNotApplied) {
        const auto compiled =
            Compile(R"(<div id="t" data-widget="toggle">T</div><div id="s" data-widget="slider"></div><div id="p" data-widget="progress"></div>)",
                    "div { background: #101010 } #s:hover { background: #202020 } #t:active { background: #303030 } #p:disabled { -be-fill-color: red } #t:checked { background: #404040 }");
        EXPECT_EQ(compiled->IssuesMentioning("a state rule does not apply to a toggle, slider or progress bar"), 2u) << "one message per property and tag, however many elements";
        EXPECT_EQ(compiled->IssuesMentioning("('color' of a <div> is left out)"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("('fillColor' of a <div> is left out)"), 1u);
        EXPECT_EQ(compiled->Attr("t", "color"), "16,16,16,255") << "the base look is kept";
        EXPECT_EQ(compiled->Attr("s", "color"), "16,16,16,255");
        for (const char* name : {"t", "s", "p"}) {
            EXPECT_FALSE(compiled->Has(name, "hoverColor")) << name;
            EXPECT_FALSE(compiled->Has(name, "pressedColor")) << name;
            EXPECT_FALSE(compiled->Has(name, "selectedColor")) << name;
            EXPECT_FALSE(compiled->Has(name, "disabledColor")) << name;
        }
        EXPECT_EQ(compiled->Attr("p", "fillColor"), "") << "the rule did not change the base look";
    }

    TEST_F(UIWebTest, PointerEventsOnAWidgetIsReported) {
        const auto compiled = Compile(R"(<div id="s" data-widget="slider"></div>)", "#s { pointer-events: none }");
        EXPECT_EQ(compiled->IssuesMentioning("pointer-events does not apply to a toggle, slider or progress bar"), 1u);
        EXPECT_FALSE(compiled->Has("s", "blocksInput"));
    }

    TEST_F(UIWebTest, AWidgetKeepsItsCounters) {
        const auto compiled =
            Compile(R"(<div id="t" data-widget="toggle" data-bind="a">T</div><div id="s" data-widget="slider" data-bind="b"></div><div id="p" data-widget="progress" data-bind="c"></div>)",
                    "div { height: 56px; -be-fill-color: #00ff00 }");
        EXPECT_EQ(compiled->report.elements, 3u);
        EXPECT_EQ(compiled->report.rules, 1u);
        EXPECT_EQ(compiled->report.declarations, 2u);
        EXPECT_EQ(compiled->report.declarationsUnsupported, 0u);
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    TEST_F(UIWebTest, AToggleCaptionKeepsItsStringReferenceAndVariables) {
        const auto compiled = Compile(R"(<div id="t" data-widget="toggle" data-bind="a">{#settings.sound}: <b>{state}</b></div>)");
        EXPECT_EQ(compiled->Attr("t", "text"), "{#settings.sound}: {state}") << "inline elements are part of the caption";
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    // ---- the cascade ---------------------------------------------------------------------------------------------------------

    TEST_F(UIWebTest, SpecificityBeatsOrderAndOrderBreaksTies) {
        const auto compiled = Compile(R"(<div id="a" class="c"></div><div id="b" class="c"></div>)", "#a { width: 10px } .c { width: 20px } div { width: 30px }\n"
                                                                                                     ".c { height: 1px } .c { height: 2px }\n");
        EXPECT_EQ(compiled->Attr("a", "width"), "10");
        EXPECT_EQ(compiled->Attr("b", "width"), "20");
        EXPECT_EQ(compiled->Attr("b", "height"), "2");
    }

    TEST_F(UIWebTest, ImportantBeatsSpecificityAndAStyleAttributeBeatsRules) {
        const auto compiled = Compile(R"(<div id="a" style="width: 5px; height: 6px"></div>)", "#a { width: 10px !important; height: 20px } div { width: 99px }");
        EXPECT_EQ(compiled->Attr("a", "width"), "10") << "!important in a rule beats the style attribute";
        EXPECT_EQ(compiled->Attr("a", "height"), "6") << "the style attribute beats an id";
    }

    TEST_F(UIWebTest, DescendantAndChildCombinators) {
        const auto compiled = Compile(R"(<div id="outer" class="o"><div id="mid"><span id="deep">x</span></div><span id="direct">y</span></div><span id="free">z</span>)",
                                      ".o span { width: 1px } .o > span { height: 2px }");
        EXPECT_EQ(compiled->Attr("deep", "width"), "1");
        EXPECT_EQ(compiled->Attr("direct", "width"), "1");
        EXPECT_FALSE(compiled->Has("free", "width"));
        EXPECT_FALSE(compiled->Has("deep", "height"));
        EXPECT_EQ(compiled->Attr("direct", "height"), "2");
    }

    TEST_F(UIWebTest, TextPropertiesAreInheritedAndAButtonKeepsItsOwnAlignment) {
        const auto compiled = Compile(R"(<div class="box"><span id="s">a</span><button id="b">b</button><div id="g"><span id="deep">c</span></div></div>)",
                                      ".box { color: #ff0000; font-size: 20px; text-align: right } #g { font-size: 30px }");
        EXPECT_EQ(compiled->Attr("s", "textColor"), "255,0,0,255");
        EXPECT_EQ(compiled->Attr("s", "fontSize"), "20");
        EXPECT_EQ(compiled->Attr("s", "align"), "Right");
        EXPECT_EQ(compiled->Attr("deep", "fontSize"), "30") << "a nearer declaration wins";
        EXPECT_EQ(compiled->Attr("b", "fontSize"), "20");
        EXPECT_FALSE(compiled->Has("b", "align")) << "text-align does not reach a button";
        EXPECT_FALSE(compiled->Has("g", "fontSize")) << "a group has no text to size";
    }

    // ---- what a property turns into ------------------------------------------------------------------------------------------

    TEST_F(UIWebTest, FlexBecomesAStack) {
        const auto compiled = Compile(R"(<div id="row"><span id="a">1</span><span id="b">2</span></div><div id="col"></div>)",
                                      "#row { display: flex; gap: 8px; padding: 4px 6px; flex-wrap: wrap; align-items: center }\n"
                                      "#col { display: flex; flex-direction: column }\n"
                                      "#a { flex: 1 1 0; } #b { flex-grow: 2 }");
        EXPECT_EQ(compiled->Attr("row", "layout"), "Stack");
        EXPECT_EQ(compiled->Attr("row", "direction"), "Horizontal") << "CSS lays out a flex container as a row";
        EXPECT_EQ(compiled->Attr("row", "spacing"), "8");
        EXPECT_EQ(compiled->Attr("row", "padding"), "6,4,6,4") << "top-bottom 4, left-right 6, written as left,top,right,bottom";
        EXPECT_EQ(compiled->Attr("row", "wrapChildren"), "true");
        EXPECT_EQ(compiled->Attr("row", "crossAlign"), "Center");
        EXPECT_FALSE(compiled->Has("col", "direction")) << "a column is the default of the stack";
        EXPECT_EQ(compiled->Attr("a", "flex"), "1");
        EXPECT_EQ(compiled->Attr("b", "flex"), "2");
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    TEST_F(UIWebTest, PositionsBecomeAnAnchorAndAnOffset) {
        const auto compiled = Compile(R"(<div id="tl"></div><div id="br"></div><div id="fill"></div><div id="c"></div>)",
                                      "#tl { left: 10px; top: 20px } #br { right: 5px; bottom: 6px } #fill { inset: 8px 4px; margin: 1px }\n"
                                      "#c { anchor: center; left: 3px }");
        EXPECT_FALSE(compiled->Has("tl", "anchor"));
        EXPECT_EQ(compiled->Attr("tl", "x"), "10");
        EXPECT_EQ(compiled->Attr("tl", "y"), "20");
        EXPECT_EQ(compiled->Attr("br", "anchor"), "BottomRight");
        EXPECT_EQ(compiled->Attr("br", "x"), "-5");
        EXPECT_EQ(compiled->Attr("br", "y"), "-6");
        EXPECT_EQ(compiled->Attr("fill", "anchor"), "Stretch");
        EXPECT_EQ(compiled->Attr("fill", "margin"), "5,9,5,9") << "the inset plus the margin, as left,top,right,bottom";
        EXPECT_EQ(compiled->Attr("c", "anchor"), "Center");
        EXPECT_EQ(compiled->Attr("c", "x"), "3");
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    TEST_F(UIWebTest, HiddenElements) {
        const auto compiled =
            Compile(R"(<div id="a" hidden></div><div id="b"></div><div id="c"></div><div id="d"></div>)", "#b { display: none } #c { visibility: hidden } #d { display: none } #d { display: block }");
        EXPECT_EQ(compiled->Attr("a", "visible"), "false");
        EXPECT_EQ(compiled->Attr("b", "visible"), "false");
        EXPECT_EQ(compiled->Attr("c", "visible"), "false");
        EXPECT_FALSE(compiled->Has("d", "visible"));
    }

    TEST_F(UIWebTest, ImagesAndPanelsWithASprite) {
        const auto compiled = Compile(R"(<img id="i" src="Hero"><img id="s" src="Hero"><div id="p"></div><div id="q"></div>)", "#s { object-fit: fill; aspect-ratio: 16 / 9 }\n"
                                                                                                                               "#p { background-image: url('Panel9'); border-image-slice: 12 8 }\n"
                                                                                                                               "#q { background: #112233 url(Plate) no-repeat }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_EQ(compiled->Find("i").attribute("type").as_string(), std::string("UIImage"));
        EXPECT_EQ(compiled->Attr("i", "sprite"), "Hero");
        EXPECT_EQ(compiled->Attr("s", "stretch"), "true");
        EXPECT_NEAR(std::stof(compiled->Attr("s", "aspectRatio")), 16.0f / 9.0f, 1e-4f);
        EXPECT_EQ(compiled->Find("p").attribute("type").as_string(), std::string("UIPanel"));
        EXPECT_EQ(compiled->Attr("p", "sprite"), "Panel9");
        EXPECT_EQ(compiled->Attr("p", "sliceBorder"), "8,12,8,12");
        EXPECT_FALSE(compiled->Has("p", "blocksInput")) << "a panel takes clicks by itself";
        EXPECT_EQ(compiled->Attr("q", "sprite"), "Plate");
        EXPECT_EQ(compiled->Attr("q", "color"), "17,34,51,255");
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    TEST_F(UIWebTest, AnElementWithOnlyTextIsALabelAndItsBackgroundIsTheFill) {
        const auto compiled = Compile(R"(<div id="t" class="tag">  Hello,
            <b>big</b> world &amp; co<br>second line </div><p id="e"></p>)",
                                      ".tag { background: rgba(0, 0, 0, 0.5); white-space: nowrap; text-overflow: clip; line-height: 1.5; font-family: sans-serif }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_EQ(compiled->Find("t").attribute("type").as_string(), std::string("UILabel"));
        EXPECT_EQ(compiled->Attr("t", "text"), "Hello, big world & co\nsecond line");
        EXPECT_EQ(compiled->Attr("t", "color"), "0,0,0,128");
        EXPECT_EQ(compiled->Attr("t", "blocksInput"), "true") << "a visible plate takes clicks";
        EXPECT_EQ(compiled->Attr("t", "wrap"), "false");
        EXPECT_EQ(compiled->Attr("t", "overflow"), "Clip");
        EXPECT_EQ(compiled->Attr("t", "lineSpacing"), "1.5");
        EXPECT_FALSE(compiled->Has("t", "font")) << "a generic family is the default font";
        EXPECT_EQ(compiled->Find("e").attribute("type").as_string(), std::string("UILabel")) << "an empty <p> is a label that a variable can fill";
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    TEST_F(UIWebTest, AHeadingTakesAFamilyAWeightAShadowAndAnOutline) {
        const auto compiled = Compile(R"(<h1 id="h">Title</h1>)", R"(h1 { font-family: "Russo One", sans-serif; font-weight: 700; text-shadow: 2px 2px #000000A0; -be-text-outline: 2px #000; })");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_EQ(compiled->Attr("h", "fontFamily"), "Russo One");
        EXPECT_FALSE(compiled->Has("h", "font"));
        EXPECT_EQ(compiled->Attr("h", "fontWeight"), "700");
        EXPECT_EQ(compiled->Attr("h", "textShadow"), "2,2,#000000A0");
        EXPECT_EQ(compiled->Attr("h", "textOutline"), "2,#000000FF");
        EXPECT_TRUE(compiled->report.issues.empty());

        auto* label = compiled->Element("h")->Cast<UILabel>();
        ASSERT_NE(label, nullptr);
        EXPECT_EQ(label->fontFamily, PoolString::Intern("Russo One"));
        EXPECT_EQ(label->fontWeight, 700);
        EXPECT_FLOAT_EQ(label->textEffects.outlineWidth, 2.0f);
        EXPECT_EQ(label->textEffects.shadowColor.a, 160);
    }

    TEST_F(UIWebTest, AButtonTakesThemTooAndAHoverThatLeavesThemAloneChangesColorsOnly) {
        const auto compiled =
            Compile(R"(<button id="b" data-action="Go">Go</button>)",
                    "button { background: #202020; font-weight: bold; font-family: Roboto; text-shadow: #000 0 1px; -be-text-outline: #FFF 1px } button:hover { background: #303030 }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_EQ(compiled->Attr("b", "fontFamily"), "Roboto");
        EXPECT_EQ(compiled->Attr("b", "fontWeight"), "700");
        EXPECT_EQ(compiled->Attr("b", "textShadow"), "0,1,#000000FF") << "the colour may come first";
        EXPECT_EQ(compiled->Attr("b", "textOutline"), "1,#FFFFFFFF");
        EXPECT_EQ(compiled->Attr("b", "hoverColor"), "48,48,48,255");
        EXPECT_TRUE(compiled->report.issues.empty());
        auto* button = compiled->Element("b")->Cast<UIButton>();
        ASSERT_NE(button, nullptr);
        EXPECT_EQ(button->fontWeight, 700);
        EXPECT_FLOAT_EQ(button->textEffects.shadowOffsetY, 1.0f);
    }

    TEST_F(UIWebTest, TheTextPropertiesAreInheritedFromAContainerAndAChildMayChangeThem) {
        const auto compiled = Compile(R"(<div id="box"><span id="a">A</span><span id="b" class="plain">B</span><button id="c" data-action="X">C</button></div>)",
                                      "#box { font-weight: 700; font-family: \"Russo One\"; text-shadow: 1px 1px #000; -be-text-outline: 2px #fff } "
                                      ".plain { font-weight: normal; text-shadow: none; -be-text-outline: none; font-family: sans-serif }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_FALSE(compiled->Has("box", "fontWeight")) << "a group has no text";
        EXPECT_FALSE(compiled->Has("box", "textShadow"));
        EXPECT_EQ(compiled->Attr("a", "fontWeight"), "700");
        EXPECT_EQ(compiled->Attr("a", "fontFamily"), "Russo One");
        EXPECT_EQ(compiled->Attr("a", "textOutline"), "2,#FFFFFFFF");
        EXPECT_EQ(compiled->Attr("c", "textShadow"), "1,1,#000000FF");
        EXPECT_FALSE(compiled->Has("b", "fontWeight"));
        EXPECT_FALSE(compiled->Has("b", "fontFamily"));
        EXPECT_FALSE(compiled->Has("b", "textShadow"));
        EXPECT_FALSE(compiled->Has("b", "textOutline"));
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    TEST_F(UIWebTest, ImportantAndAPathAndTheNumbersOfAWeight) {
        const auto compiled =
            Compile(R"(<span id="a" style="font-weight: 300">A</span><span id="b" class="x" style="font-weight: 400">B</span><span id="c" style="font-family: assets/fonts/Other.ttf">C</span>)",
                    ".x { font-weight: 900 !important }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_EQ(compiled->Attr("a", "fontWeight"), "300");
        EXPECT_EQ(compiled->Attr("b", "fontWeight"), "900") << "!important beats style=\"\"";
        EXPECT_EQ(compiled->Attr("c", "font"), "assets/fonts/Other.ttf");
        EXPECT_FALSE(compiled->Has("c", "fontFamily"));
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    TEST_F(UIWebTest, BolderALightWeightABlurAndSeveralShadowsAreReported) {
        const auto compiled = Compile(R"(<span id="a" class="a">A</span><span id="b" class="b">B</span><span id="c" class="c">C</span><span id="d" class="d">D</span><span id="e" class="e">E</span>)",
                                      ".a { font-weight: bolder } .b { text-shadow: 1px 1px 4px #000 } .c { text-shadow: 1px 1px #000, 2px 2px #fff } .d { font-weight: 450; }"
                                      " .e { text-shadow: 1px 1px; -be-text-outline: -1px #000 }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_EQ(compiled->IssuesMentioning("font-weight: bolder"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("font-weight: 450"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("blur"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("several text shadows"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("text-shadow: 1px 1px'"), 1u) << "no colour is not understood";
        EXPECT_EQ(compiled->IssuesMentioning("-be-text-outline: -1px"), 1u);
        EXPECT_FALSE(compiled->Has("a", "fontWeight"));
        EXPECT_EQ(compiled->Attr("b", "textShadow"), "1,1,#000000FF") << "a blurred shadow is drawn hard";
        EXPECT_EQ(compiled->Attr("c", "textShadow"), "1,1,#000000FF") << "the first of several";
        EXPECT_FALSE(compiled->Has("e", "textShadow"));
        EXPECT_FALSE(compiled->Has("e", "textOutline"));
    }

    TEST_F(UIWebTest, AStringReferenceIsTextLikeAVariableAndReachesTheXmlUntouched) {
        const auto compiled = Compile(R"(<button id="b" data-action="Start">{#menu.start}</button><span id="s">{#score.title}: {score}</span><p id="q">{{#menu.start}}</p>)");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_EQ(compiled->Find("b").attribute("type").as_string(), std::string("UIButton"));
        EXPECT_EQ(compiled->Attr("b", "text"), "{#menu.start}");
        EXPECT_EQ(compiled->Find("s").attribute("type").as_string(), std::string("UILabel"));
        EXPECT_EQ(compiled->Attr("s", "text"), "{#score.title}: {score}");
        EXPECT_EQ(compiled->Attr("q", "text"), "{{#menu.start}}") << "an escaped brace stays escaped";
        EXPECT_TRUE(compiled->report.issues.empty());

        // and the control made from it shows the string of the current language
        UIStrings& strings = CoreManager::GetUIStrings();
        strings.Set("en"_intern, "menu.start", "Start");
        strings.Set("en"_intern, "score.title", "Score");
        strings.Set("de"_intern, "menu.start", "Los");
        strings.Set("de"_intern, "score.title", "Punkte");
        CoreManager::GetUIVars().Set("score"_intern, 3);
        auto* button = compiled->Element("b")->Cast<UIButton>();
        auto* span = compiled->Element("s")->Cast<UILabel>();
        auto* quoted = compiled->Element("q")->Cast<UILabel>();
        ASSERT_NE(button, nullptr) << compiled->xml.c_str();
        ASSERT_NE(span, nullptr);
        ASSERT_NE(quoted, nullptr);
        EXPECT_EQ(button->GetDisplayText(), "Start");
        EXPECT_EQ(span->GetDisplayText(), "Score: 3");
        EXPECT_EQ(quoted->GetDisplayText(), "{#menu.start}");
        strings.SetLanguage("de"_intern);
        EXPECT_EQ(button->GetDisplayText(), "Los");
        EXPECT_EQ(span->GetDisplayText(), "Punkte: 3");
    }

    TEST_F(UIWebTest, PointerEventsDecideWhetherAGroupTakesClicks) {
        const auto compiled = Compile(R"(<div id="plain"></div><div id="plate"></div><div id="through"></div><div id="solid"></div>)",
                                      "#plate { background: red } #through { background: red; pointer-events: none } #solid { pointer-events: auto }");
        EXPECT_FALSE(compiled->Has("plain", "blocksInput")) << "a group with no background must not swallow the clicks of the game under it";
        EXPECT_EQ(compiled->Attr("plate", "blocksInput"), "true");
        EXPECT_FALSE(compiled->Has("through", "blocksInput"));
        EXPECT_EQ(compiled->Attr("solid", "blocksInput"), "true");
    }

    TEST_F(UIWebTest, ButtonStates) {
        const auto compiled = Compile(R"(<button id="b" action="Go" disabled>Go</button>)",
                                      "button { background: #202020 } button:hover { background: #303030 } button:active { background: #101010 } button:disabled { background: #555555 }");
        EXPECT_EQ(compiled->Attr("b", "color"), "32,32,32,255");
        EXPECT_EQ(compiled->Attr("b", "hoverColor"), "48,48,48,255");
        EXPECT_EQ(compiled->Attr("b", "pressedColor"), "16,16,16,255");
        EXPECT_EQ(compiled->Attr("b", "disabledColor"), "85,85,85,255");
        EXPECT_EQ(compiled->Attr("b", "disabled"), "true");
        EXPECT_EQ(compiled->Attr("b", "action"), "Go") << "action works as well as data-action";
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    // ---- variants from @media ------------------------------------------------------------------------------------------------

    TEST_F(UIWebTest, AMediaBlockBecomesOnlyWhatItChanged) {
        const auto compiled = Compile(R"(<div id="a"></div><div></div>)", "div { width: 100px; height: 50px; left: 5px }\n"
                                                                          "@media (max-width: 600px) { #a { width: 80% } div { display: none } }\n");
        ASSERT_EQ(compiled->content.variants.size(), 1u);
        const UIVariant& variant = compiled->content.variants[0];
        EXPECT_EQ(variant.when.maxWidth, 600.0f);
        ASSERT_EQ(variant.overrides.size(), 2u);
        EXPECT_EQ(variant.overrides[0].element.ToStringView(), "a");
        ASSERT_TRUE(variant.overrides[0].width);
        EXPECT_EQ(variant.overrides[0].width->unit, UIUnit::Percent);
        EXPECT_EQ(variant.overrides[0].visible, std::optional<bool>(false));
        EXPECT_FALSE(variant.overrides[0].height) << "unchanged properties are not overridden";
        // The second div has no id: the compile names it so that the variant can find it
        EXPECT_FALSE(variant.overrides[1].element.ToStringView().empty());
        EXPECT_TRUE(compiled->Find(variant.overrides[1].element.CStr()));
    }

    TEST_F(UIWebTest, AMediaBlockCanUndoWhatTheBaseDid) {
        const auto compiled = Compile(R"(<div id="a"></div>)", "#a { display: none; width: 10px; height: 10px } @media (min-width: 800px) { #a { display: block } }");
        ASSERT_EQ(compiled->content.variants.size(), 1u);
        EXPECT_EQ(compiled->content.variants[0].overrides.at(0).visible, std::optional<bool>(true));
        // The base screen has it hidden; wide enough (in canvas units: the scale is kept at 0.7 or more, so 480 px are 685 units), the variant shows it
        LaidOut web(compiled->content);
        web.At(480.0f, 270.0f);
        EXPECT_TRUE(web.Find("a")->rect.IsEmpty());
        web.At(1920.0f, 1080.0f);
        EXPECT_FALSE(web.Find("a")->rect.IsEmpty());
    }

    TEST_F(UIWebTest, AQueryListMakesOneVariantForEachAlternative) {
        const auto compiled = Compile(R"(<div id="a"></div>)", "@media (orientation: portrait), (max-width: 500px) { #a { width: 50% } }");
        EXPECT_EQ(compiled->content.variants.size(), 2u);
        EXPECT_EQ(compiled->report.variants, 2u);
    }

    TEST_F(UIWebTest, AMediaBlockCannotChangeWhatAVariantCannot) {
        const auto compiled = Compile(R"(<div id="a"></div>)", "@media (max-width: 500px) { #a { background: red; width: 5px } }");
        EXPECT_EQ(compiled->content.variants.size(), 1u) << "the width still becomes a variant";
        EXPECT_EQ(compiled->IssuesMentioning("@media changes 'color'"), 1u);
    }

    // ---- what is reported ----------------------------------------------------------------------------------------------------

    TEST_F(UIWebTest, UnsupportedPropertiesValuesAndTagsAreReported) {
        const auto compiled = Compile("<table id=\"t\"></table>\n<div id=\"a\"></div>", "#a { opacity: .5;\n  width: 2em;\n  justify-content: center; }\n"
                                                                                        "#a { cursor: pointer; position: absolute; box-sizing: border-box; width: 10px }\n");
        EXPECT_EQ(compiled->IssuesMentioning("property 'opacity' is not supported"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("'width: 2em'"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("property 'justify-content'"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("<table> is not supported"), 1u);
        EXPECT_EQ(compiled->report.declarationsUnsupported, 3u);
        EXPECT_EQ(compiled->report.issues.size(), 4u) << "cursor, position: absolute and box-sizing mean nothing here and are not defects";
        // The rest of the screen is built
        EXPECT_EQ(compiled->Attr("a", "width"), "10");
        // The line of the declaration is the line of the style text, counted from the <style> tag
        for (const UIWebIssue& issue : compiled->report.issues) {
            if (issue.message.find("opacity") != eastl::string::npos) {
                EXPECT_EQ(issue.line, 1u);
                EXPECT_EQ(issue.source, "css");
            } else if (issue.message.find("2em") != eastl::string::npos) {
                EXPECT_EQ(issue.line, 2u);
            }
        }
    }

    TEST_F(UIWebTest, AnIssueIsReportedOnceWhateverNumberOfElementsItTouches) {
        const auto compiled = Compile(R"(<div class="x"></div><div class="x"></div><div class="x"></div>)", ".x { opacity: 0.5; margin: 3px }");
        EXPECT_EQ(compiled->IssuesMentioning("opacity"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("margin only applies"), 1u);
    }

    TEST_F(UIWebTest, TextBesideElementsIsReported) {
        const auto compiled = Compile(R"(<div id="a">loose <span>kept</span></div>)");
        EXPECT_EQ(compiled->IssuesMentioning("text next to other elements"), 0u) << "inline text with inline elements is one label";
        EXPECT_EQ(compiled->Find("a").attribute("type").as_string(), std::string("UILabel"));
        const auto mixed = Compile(R"(<div id="a">loose <div id="b"></div></div>)");
        EXPECT_EQ(mixed->IssuesMentioning("text next to other elements"), 1u);
        EXPECT_TRUE(mixed->Find("b"));
    }

    TEST_F(UIWebTest, DuplicateIdsAreReported) {
        const auto compiled = Compile(R"(<div id="a"></div><div id="a"></div>)");
        EXPECT_EQ(compiled->IssuesMentioning("used twice"), 1u);
    }

    TEST_F(UIWebTest, StyleSheetsAreReadThroughTheLoader) {
        UIWebReport report;
        eastl::vector<eastl::string> asked;
        const std::string html = R"(<link rel="stylesheet" href="a.css"><link rel="stylesheet" href="missing.css"><div id="x" class="k"></div><style>.k { height: 9px }</style>)";
        const eastl::string xml = UIWebCompiler::Compile(
            eastl::string_view(html.data(), html.size()),
            [&](eastl::string_view href) -> std::optional<eastl::string> {
                asked.push_back(eastl::string(href));
                if (href == "a.css") {
                    return eastl::string(".k { width: 7px; height: 1px }");
                }
                return std::nullopt;
            },
            report);
        ASSERT_EQ(asked.size(), 2u);
        pugi::xml_document document;
        ASSERT_TRUE(document.load_string(xml.c_str()));
        const pugi::xml_node element = document.document_element().child("elements").child("element");
        EXPECT_EQ(std::string(element.attribute("width").as_string()), "7");
        EXPECT_EQ(std::string(element.attribute("height").as_string()), "9") << "the <style> after the <link> comes later in the cascade";
        ASSERT_EQ(report.issues.size(), 1u);
        EXPECT_EQ(report.issues[0].source, "html");
        EXPECT_TRUE(report.issues[0].message.find("missing.css") != eastl::string::npos);
    }

    // ---- as a resource -------------------------------------------------------------------------------------------------------

    namespace {

        class TempDirectory {
        public:
            // One directory per test: ctest runs them in parallel processes, and the constructor clears the directory
            TempDirectory() : _path(TestTempDirectory("BagiEngineUIWebTest")) {
                std::filesystem::remove_all(_path);
                std::filesystem::create_directories(_path);
            }
            ~TempDirectory() {
                std::error_code ignored;
                std::filesystem::remove_all(_path, ignored);
            }

            std::string Write(const char* name, const std::string& content) const {
                const auto file = _path / name;
                std::ofstream(file) << content;
                return file.generic_string();
            }

        private:
            std::filesystem::path _path;
        };

    }  // namespace

    TEST_F(UIWebTest, ADocumentInHtmlLoadsThroughTheResourceManagerAndBuildsTheSameElements) {
        TempDirectory temp;
        temp.Write("hud.css", kHudCss);
        const std::string path = temp.Write("hud.ui.html", std::string("<link rel=\"stylesheet\" href=\"hud.css\">") + kHudHtml);

        auto handle = CoreManager::GetResourceManager().Load<UIDocument>(eastl::string_view(path.data(), path.size()));
        ASSERT_TRUE(handle);
        ASSERT_EQ(handle->GetState(), ResourceState::Loaded);

        UIScreenContent content;
        EXPECT_TRUE(handle->Instantiate(content));
        const UIScreenContent expected = ReadXml(kHudXml);
        ExpectSameElements(expected.elements, content.elements);
        EXPECT_TRUE(expected.variants == content.variants);
    }

    TEST_F(UIWebTest, AMissingHtmlFileFails) {
        auto handle = CoreManager::GetResourceManager().Load<UIDocument>("no/such/file.ui.html");
        EXPECT_FALSE(handle && handle->GetState() == ResourceState::Loaded);
    }

}  // namespace BECore::Tests
