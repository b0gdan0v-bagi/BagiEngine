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
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UILabel.h>
#include <BECore/UI/UIPanel.h>
#include <BECore/UI/UIProgressBar.h>
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UISlider.h>
#include <BECore/UI/UISliderComponent.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UISwitchComponent.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UIToggle.h>
#include <BECore/UI/UIToggleComponent.h>
#include <BECore/UI/UITooltipComponent.h>
#include <BECore/UI/UIWebCompiler.h>
#include <BECore/UI/UIWebText.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/TempPath.h>
#include <Common/UILayoutMatrix.h>
#include <Common/UITreeCompare.h>
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

            /// The component of that type of the element of that name (`UITextComponent`...); empty when there is none
            [[nodiscard]] pugi::xml_node Component(const char* name, const char* type) const {
                for (pugi::xml_node component : Find(name).child("components").children()) {
                    if (std::string(component.attribute("type").as_string()) == type) {
                        return component;
                    }
                }
                return {};
            }

            /// An attribute of a component of the element of that name; "" when there is none
            [[nodiscard]] std::string ComponentAttr(const char* name, const char* type, const char* attribute) const {
                return Component(name, type).attribute(attribute).as_string();
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
        <element type="UIElement" name="Reset" anchor="BottomRight" x="-12" y="-10" width="96" height="48" fontSize="24" color="40,90,60,255" hoverColor="51,170,102,255">
          <components>
            <component type="UITextComponent" text="Reset" align="Center" verticalAlign="Center" wrap="false"/>
            <component type="UIInputClicker" action="Match3Reset"/>
          </components>
        </element>
      </children>
    </element>
  </elements>
  <variants>
    <variant when="orientation:portrait">
      <overrides><set element="Hud" anchor="Top" x="0" width="90%"/></overrides>
    </variant>
  </variants>
</ui>)";

        // The main menu of Match3 as the game shipped it before its buttons became composed (games/Match3/config/SceneConfig.xml; the HTML compiler writes the composed form, whose layout is the
        // same), and the same menu in HTML + CSS
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

        // The HUD of Match3 as the game shipped it before its buttons became composed (same note as the menu), and the same HUD in HTML + CSS
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
          <element type="UILabel" name="ColorCaption" width="150" height="56" text="Play as" fontSize="24" verticalAlign="Center" />
          <element type="UISegmented" name="Color" bind="opponentColor" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255" blocksInput="true">
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
          <element type="UILabel" name="EngineCaption" width="150" height="56" text="Engine" fontSize="24" verticalAlign="Center" />
          <element type="UISegmented" name="Engine" bind="opponentEngine" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255" blocksInput="true">
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
          <element type="UILabel" name="LevelCaption" width="150" height="56" text="Level" fontSize="24" verticalAlign="Center" />
          <element type="UISegmented" name="Level" bind="opponentLevel" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255" blocksInput="true">
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
            // The components of a composed element: the same types in the same order, with the same attributes
            pugi::xml_node actualComponent = actual.child("components").first_child();
            for (pugi::xml_node expectedComponent : expected.child("components").children()) {
                ASSERT_TRUE(actualComponent) << name << " lacks the component " << expectedComponent.attribute("type").as_string();
                for (pugi::xml_attribute attribute : expectedComponent.attributes()) {
                    EXPECT_EQ(std::string(attribute.as_string()), std::string(actualComponent.attribute(attribute.name()).as_string())) << name << " " << attribute.name();
                }
                for (pugi::xml_attribute attribute : actualComponent.attributes()) {
                    EXPECT_FALSE(expectedComponent.attribute(attribute.name()).empty()) << name << " has the extra component attribute " << attribute.name();
                }
                actualComponent = actualComponent.next_sibling();
            }
            EXPECT_FALSE(actualComponent) << name << " has more components than expected";
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
      <element type="UIElement" name="Sound" height="56" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
        <components>
          <component type="UIInputClicker"/>
          <component type="UIToggleComponent" bind="soundOn" action="SettingsSound"/>
        </components>
        <children>
          <element type="UIElement" name="SoundCaption" flex="1" height="100%" fontSize="24">
            <components>
              <component type="UITextComponent" text="{#settings.sound}" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/>
            </components>
          </element>
          <element type="UIElement" name="SoundSwitch" height="100%" aspectRatio="1.25">
            <components>
              <component type="UISwitchComponent"/>
            </components>
          </element>
        </children>
      </element>
      <element type="UIStack" name="MusicRow" height="56" direction="Horizontal" spacing="8" crossAlign="Center">
        <children>
          <element type="UILabel" name="MusicCaption" width="220" height="56" text="{#settings.music}" fontSize="24" verticalAlign="Center"/>
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
            CoreManager::GetUIThemes().Clear();
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

    // ---- the values of the box properties --------------------------------------------------------------------------------------

    TEST_F(UIWebTest, CornerRadiiFollowTheCssOrder) {
        using UIWebText::ParseCornerRadii;
        EXPECT_EQ(*ParseCornerRadii("8px"), UICorners::Uniform(8.0f));
        EXPECT_EQ(*ParseCornerRadii("  12  "), UICorners::Uniform(12.0f));
        EXPECT_EQ(*ParseCornerRadii("0"), UICorners{});
        EXPECT_EQ(*ParseCornerRadii("1px 2px"), (UICorners{1.0f, 2.0f, 1.0f, 2.0f}));
        EXPECT_EQ(*ParseCornerRadii("1px 2px 3px"), (UICorners{1.0f, 2.0f, 3.0f, 2.0f}));
        EXPECT_EQ(*ParseCornerRadii("1px\t2px   3px 4px"), (UICorners{1.0f, 2.0f, 3.0f, 4.0f}));
        EXPECT_EQ(*ParseCornerRadii("999px"), UICorners::Uniform(999.0f));
        EXPECT_FALSE(ParseCornerRadii(""));
        EXPECT_FALSE(ParseCornerRadii("50%"));
        EXPECT_FALSE(ParseCornerRadii("10px / 5px"));
        EXPECT_FALSE(ParseCornerRadii("10px/5px"));
        EXPECT_FALSE(ParseCornerRadii("-1px"));
        EXPECT_FALSE(ParseCornerRadii("1px 2px 3px 4px 5px"));
        EXPECT_FALSE(ParseCornerRadii("2em"));
        EXPECT_FALSE(ParseCornerRadii("round"));
    }

    TEST_F(UIWebTest, BordersTakeAWidthAColorAndAStyleInAnyOrder) {
        using UIWebText::ParseBorder;
        const auto plain = ParseBorder("1px solid #fff");
        ASSERT_TRUE(plain);
        EXPECT_FALSE(plain->none);
        EXPECT_EQ(plain->width, 1.0f);
        EXPECT_EQ(plain->color.r, 255);
        const auto reordered = ParseBorder("  RED   SOLID  3 ");
        ASSERT_TRUE(reordered);
        EXPECT_EQ(reordered->width, 3.0f);
        EXPECT_EQ(reordered->color.r, 255);
        EXPECT_EQ(reordered->color.g, 0);
        const auto noStyle = ParseBorder("rgba(0, 0, 0, .5) 2px");
        ASSERT_TRUE(noStyle);
        EXPECT_EQ(noStyle->width, 2.0f);
        EXPECT_EQ(noStyle->color.a, 128);
        EXPECT_TRUE(ParseBorder("none")->none);
        EXPECT_TRUE(ParseBorder("0")->none);
        EXPECT_TRUE(ParseBorder("0px solid red")->none);
        EXPECT_TRUE(ParseBorder("2px none red")->none);
        EXPECT_FALSE(ParseBorder(""));
        EXPECT_FALSE(ParseBorder("2px dashed red"));
        EXPECT_FALSE(ParseBorder("2px dotted red"));
        EXPECT_FALSE(ParseBorder("-1px solid red"));
        EXPECT_FALSE(ParseBorder("2px solid"));
        EXPECT_FALSE(ParseBorder("solid red"));
        EXPECT_FALSE(ParseBorder("1px 2px red"));
        EXPECT_FALSE(ParseBorder("1px red blue"));
        EXPECT_FALSE(ParseBorder("1px solid solid red"));
        EXPECT_FALSE(ParseBorder("thin solid red"));
    }

    TEST_F(UIWebTest, LinearGradientsHaveTwoStopsAndAnAxis) {
        using UIWebText::ParseLinearGradient;
        const auto plain = ParseLinearGradient("linear-gradient(#112233, #445566)");
        ASSERT_TRUE(plain);
        EXPECT_EQ(plain->direction, UIGradientDirection::Vertical);
        EXPECT_EQ(plain->start.r, 0x11);
        EXPECT_EQ(plain->end.b, 0x66);
        const auto bottom = ParseLinearGradient("linear-gradient(to bottom, red, blue)");
        ASSERT_TRUE(bottom);
        EXPECT_EQ(bottom->direction, UIGradientDirection::Vertical);
        EXPECT_EQ(bottom->start.r, 255);
        EXPECT_EQ(bottom->end.b, 255);
        EXPECT_EQ(ParseLinearGradient("linear-gradient(180deg, red, blue)")->direction, UIGradientDirection::Vertical);
        const auto right = ParseLinearGradient("linear-gradient(to right, red, blue)");
        ASSERT_TRUE(right);
        EXPECT_EQ(right->direction, UIGradientDirection::Horizontal);
        EXPECT_EQ(right->start.r, 255);
        const auto ninety = ParseLinearGradient("linear-gradient(90deg, red, blue)");
        ASSERT_TRUE(ninety);
        EXPECT_EQ(ninety->direction, UIGradientDirection::Horizontal);
        EXPECT_EQ(ninety->start.r, 255);

        // To top and to left run against the axis of the attribute: the colors swap
        for (const char* text : {"linear-gradient(to top, red, blue)", "linear-gradient(0deg, red, blue)"}) {
            const auto top = ParseLinearGradient(text);
            ASSERT_TRUE(top) << text;
            EXPECT_EQ(top->direction, UIGradientDirection::Vertical) << text;
            EXPECT_EQ(top->start.b, 255) << text;
            EXPECT_EQ(top->end.r, 255) << text;
        }
        for (const char* text : {"linear-gradient(to left, red, blue)", "linear-gradient(270deg, red, blue)"}) {
            const auto left = ParseLinearGradient(text);
            ASSERT_TRUE(left) << text;
            EXPECT_EQ(left->direction, UIGradientDirection::Horizontal) << text;
            EXPECT_EQ(left->start.b, 255) << text;
            EXPECT_EQ(left->end.r, 255) << text;
        }

        // Stops with commas and spaces inside, white space and case
        const auto stops = ParseLinearGradient("  LINEAR-GRADIENT( To   Right ,  rgba(1, 2, 3, .5) ,rgb(4 5 6) )  ");
        ASSERT_TRUE(stops);
        EXPECT_EQ(stops->direction, UIGradientDirection::Horizontal);
        EXPECT_EQ(stops->start.g, 2);
        EXPECT_EQ(stops->start.a, 128);
        EXPECT_EQ(stops->end.b, 6);

        EXPECT_FALSE(ParseLinearGradient(""));
        EXPECT_FALSE(ParseLinearGradient("red"));
        EXPECT_FALSE(ParseLinearGradient("radial-gradient(red, blue)"));
        EXPECT_FALSE(ParseLinearGradient("linear-gradient(red, green, blue)"));
        EXPECT_FALSE(ParseLinearGradient("linear-gradient(to right, red, green, blue)"));
        EXPECT_FALSE(ParseLinearGradient("linear-gradient(red)"));
        EXPECT_FALSE(ParseLinearGradient("linear-gradient(to right, red)"));
        EXPECT_FALSE(ParseLinearGradient("linear-gradient(45deg, red, blue)"));
        EXPECT_FALSE(ParseLinearGradient("linear-gradient(to top right, red, blue)"));
        EXPECT_FALSE(ParseLinearGradient("linear-gradient(red 20%, blue)"));
        EXPECT_FALSE(ParseLinearGradient("linear-gradient(red, blue 80%)"));
        EXPECT_FALSE(ParseLinearGradient("linear-gradient(red, nonsense)"));
        EXPECT_FALSE(ParseLinearGradient("linear-gradient(red, blue"));
    }

    TEST_F(UIWebTest, BoxShadowsHaveOneOrTwoLayersAndAColor) {
        using UIWebText::ParseBoxShadows;
        const auto simple = ParseBoxShadows("2px 4px #000");
        ASSERT_TRUE(simple);
        ASSERT_EQ(simple->count, 1u);
        EXPECT_EQ(simple->layers[0].offsetX, 2.0f);
        EXPECT_EQ(simple->layers[0].offsetY, 4.0f);
        EXPECT_EQ(simple->layers[0].blur, 0.0f);
        EXPECT_EQ(simple->layers[0].spread, 0.0f);
        EXPECT_EQ(simple->layers[0].color.a, 255);
        const auto blurred = ParseBoxShadows("0 2px 8px rgba(0, 0, 0, .5)");
        ASSERT_TRUE(blurred);
        EXPECT_EQ(blurred->layers[0].blur, 8.0f);
        EXPECT_EQ(blurred->layers[0].color.a, 128);
        const auto spread = ParseBoxShadows("0 0 6px -2px #fff");
        ASSERT_TRUE(spread);
        EXPECT_EQ(spread->layers[0].spread, -2.0f);
        const auto colorFirst = ParseBoxShadows("red 1px 2px 3px 4px");
        ASSERT_TRUE(colorFirst);
        EXPECT_EQ(colorFirst->layers[0].color.r, 255);
        EXPECT_EQ(colorFirst->layers[0].offsetY, 2.0f);
        EXPECT_EQ(colorFirst->layers[0].spread, 4.0f);

        // Two layers, in the order of the text; the comma inside rgba() does not split
        const auto two = ParseBoxShadows(" 0 0 12px rgba(255, 200, 0, .5) ,  0 3px 0 #000 ");
        ASSERT_TRUE(two);
        ASSERT_EQ(two->count, 2u);
        EXPECT_EQ(two->layers[0].blur, 12.0f);
        EXPECT_EQ(two->layers[0].color.g, 200);
        EXPECT_EQ(two->layers[1].offsetY, 3.0f);
        EXPECT_EQ(two->layers[1].color.a, 255);

        // A layer past the limit is counted and not stored
        const auto three = ParseBoxShadows("0 0 1px red, 0 0 2px green, 0 0 3px blue");
        ASSERT_TRUE(three);
        EXPECT_EQ(three->count, 3u);
        EXPECT_EQ(three->layers[1].blur, 2.0f);

        const auto none = ParseBoxShadows("  NONE ");
        ASSERT_TRUE(none);
        EXPECT_EQ(none->count, 0u);

        EXPECT_FALSE(ParseBoxShadows(""));
        EXPECT_FALSE(ParseBoxShadows("inset 0 0 4px red"));
        EXPECT_FALSE(ParseBoxShadows("0 0 4px red inset"));
        EXPECT_FALSE(ParseBoxShadows("0 0 4px"));
        EXPECT_FALSE(ParseBoxShadows("4px"));
        EXPECT_FALSE(ParseBoxShadows("1px 2px 3px 4px 5px red"));
        EXPECT_FALSE(ParseBoxShadows("0 0 -4px red"));
        EXPECT_FALSE(ParseBoxShadows("0 0 4% red"));
        EXPECT_FALSE(ParseBoxShadows("0 0 4px red blue"));
        EXPECT_FALSE(ParseBoxShadows("0 0 4px red,"));
        EXPECT_FALSE(ParseBoxShadows("0 0 1px red, 0 0 2px green, inset 0 0 3px blue"));
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
        const UIElement* reset = compiled->Element("Reset");
        ASSERT_NE(reset, nullptr);
        ASSERT_NE(reset->GetComponent<UITextComponent>(), nullptr);
        ASSERT_NE(reset->GetComponent<UIInputClicker>(), nullptr);
        EXPECT_EQ(reset->GetComponent<UITextComponent>()->text, "Reset");
        EXPECT_EQ(reset->GetComponent<UIInputClicker>()->action.ToStringView(), "Match3Reset");
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
        const UIElement* max = compiled->Element("LevelMax");
        ASSERT_NE(max, nullptr);
        const auto* clicker = max->GetComponent<UIInputClicker>();
        ASSERT_NE(clicker, nullptr) << "a segment is a button with a clicker";
        EXPECT_EQ(clicker->value.ToStringView(), "max");
        EXPECT_EQ(clicker->action.ToStringView(), "SelectorLevel");
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
        UIElement* white = compiled->Element("ColorWhite");
        UIElement* black = compiled->Element("ColorBlack");
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
        EXPECT_EQ(compiled->ComponentAttr("a", "UIInputClicker", "disabled"), "") << "the group greys its buttons at run time, the button is not disabled itself";
        EXPECT_EQ(compiled->ComponentAttr("a", "UIInputClicker", "value"), "x");
        EXPECT_FALSE(compiled->report.HasIssues()) << compiled->report.issues[0].message.c_str();
        auto* group = dynamic_cast<UISegmented*>(compiled->Element("g"));
        ASSERT_NE(group, nullptr);
        EXPECT_TRUE(group->disabled);
        group->SyncChildren();
        EXPECT_TRUE(compiled->Element("a")->groupDisabled);
    }

    TEST_F(UIWebTest, CheckedAndHoverOnOneButton) {
        const auto compiled = Compile(R"(<button id="b" data-action="Go" data-value="v">Go</button>)",
                                      "button { background: #202020 } button:hover { background: #303030 } button:checked { background: #405060 } button:active { background: #101010 }");
        EXPECT_EQ(compiled->Attr("b", "color"), "32,32,32,255");
        EXPECT_EQ(compiled->Attr("b", "hoverColor"), "48,48,48,255");
        EXPECT_EQ(compiled->Attr("b", "selectedColor"), "64,80,96,255");
        EXPECT_EQ(compiled->Attr("b", "pressedColor"), "16,16,16,255");
        EXPECT_EQ(compiled->ComponentAttr("b", "UIInputClicker", "value"), "v");
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
        EXPECT_EQ(onButton->Find("b").attribute("type").as_string(), std::string("UIElement")) << "a button that is no segment is composed";

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
        const auto* sound = compiled->Element("Sound")->GetComponent<UIToggleComponent>();
        ASSERT_NE(sound, nullptr) << "a data-widget=\"toggle\" div is a row with a UIToggleComponent";
        EXPECT_EQ(compiled->Element("Sound")->Cast<UIToggle>(), nullptr) << "no UIToggle is written any more";
        EXPECT_EQ(sound->bind.ToStringView(), "soundOn");
        EXPECT_EQ(sound->action.ToStringView(), "SettingsSound");
        EXPECT_EQ(sound->onValue.ToStringView(), "true") << "data-on and data-off were not written, so the defaults stand";
        EXPECT_EQ(sound->offValue.ToStringView(), "false");
        EXPECT_EQ(compiled->Element("SoundCaption")->GetComponent<UITextComponent>()->text, "{#settings.sound}") << "the caption of a toggle is its text";
        EXPECT_NE(compiled->Element("SoundSwitch")->GetComponent<UISwitchComponent>(), nullptr);
        EXPECT_TRUE(compiled->Element("Sound")->IsInteractive());
        const auto* volume = compiled->Element("Volume")->GetComponent<UISliderComponent>();
        ASSERT_NE(volume, nullptr) << "a data-widget=\"slider\" div is an element with a UISliderComponent";
        EXPECT_EQ(compiled->Element("Volume")->Cast<UISlider>(), nullptr) << "no UISlider is written any more";
        EXPECT_FALSE(compiled->Element("Volume")->GetComponent<UIInputClicker>()->stateLook) << "the plate of a slider stays still";
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
        auto* sound = compiled->Element("Sound")->GetComponent<UIToggleComponent>();
        auto* caption = compiled->Element("SoundCaption")->GetComponent<UITextComponent>();
        auto* volume = compiled->Element("Volume")->GetComponent<UISliderComponent>();
        auto* progress = compiled->Element("Progress")->Cast<UIProgressBar>();
        ASSERT_TRUE(sound != nullptr && caption != nullptr && volume != nullptr && progress != nullptr);

        EXPECT_FALSE(sound->IsOn());
        EXPECT_FLOAT_EQ(progress->GetFraction(), 0.0f);
        CoreManager::GetUIVars().Set("soundOn"_intern, "true");
        CoreManager::GetUIVars().Set("volume"_intern, "0.5");
        CoreManager::GetUIVars().Set("progress"_intern, 25);
        EXPECT_TRUE(sound->IsOn());
        EXPECT_FLOAT_EQ(volume->GetValue(), 0.5f);
        EXPECT_FLOAT_EQ(volume->GetFraction(), 0.5f);
        EXPECT_FLOAT_EQ(progress->GetFraction(), 0.25f);

        EXPECT_EQ(caption->GetDisplayText(), "Sound");
        strings.SetLanguage("de"_intern);
        EXPECT_EQ(caption->GetDisplayText(), "Geraeuscheffekte") << "a {#key} caption of a toggle follows the language";
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

        EXPECT_FALSE(compiled->Component("t", "UIToggleComponent").empty()) << "the widget name is not case sensitive";
        EXPECT_EQ(compiled->Find("t").attribute("type").as_string(), std::string("UIElement"));
        EXPECT_EQ(compiled->ComponentAttr("t", "UIToggleComponent", "bind"), "muted");
        EXPECT_EQ(compiled->ComponentAttr("t", "UIToggleComponent", "action"), "Mute") << "the plain `action` attribute is accepted as on a button";
        EXPECT_EQ(compiled->ComponentAttr("t", "UIToggleComponent", "onValue"), "1");
        EXPECT_EQ(compiled->ComponentAttr("t", "UIToggleComponent", "offValue"), "0");
        EXPECT_EQ(compiled->ComponentAttr("t", "UIInputClicker", "disabled"), "true");
        EXPECT_EQ(compiled->ComponentAttr("tCaption", "UITextComponent", "text"), "Mute");
        EXPECT_EQ(compiled->Attr("t", "color"), "1,2,3,255") << "the background is the fill behind the caption";
        EXPECT_EQ(compiled->ComponentAttr("tSwitch", "UISwitchComponent", "trackColor"), "32,32,32,128");
        EXPECT_EQ(compiled->ComponentAttr("tSwitch", "UISwitchComponent", "fillColor"), "10,200,30,255");
        EXPECT_EQ(compiled->ComponentAttr("tSwitch", "UISwitchComponent", "thumbColor"), "255,238,221,255");
        for (const char* moved : {"bind", "action", "onValue", "offValue", "disabled", "text", "trackColor", "fillColor", "thumbColor"}) {
            EXPECT_FALSE(compiled->Has("t", moved)) << moved << " lives on a component";
        }
        EXPECT_FALSE(compiled->Has("t", "blocksInput")) << "a toggle takes input by itself, whatever its background";

        EXPECT_EQ(compiled->ComponentAttr("s", "UISliderComponent", "bind"), "v");
        EXPECT_EQ(compiled->ComponentAttr("s", "UISliderComponent", "action"), "Set");
        EXPECT_EQ(compiled->ComponentAttr("s", "UISliderComponent", "minValue"), "-5");
        EXPECT_EQ(compiled->ComponentAttr("s", "UISliderComponent", "maxValue"), "5.5");
        EXPECT_EQ(compiled->ComponentAttr("s", "UISliderComponent", "step"), "0.5");
        EXPECT_EQ(compiled->ComponentAttr("s", "UIInputClicker", "disabled"), "true");
        EXPECT_EQ(compiled->ComponentAttr("s", "UIInputClicker", "stateLook"), "false");
        EXPECT_EQ(compiled->ComponentAttr("s", "UISliderComponent", "thumbColor"), "255,238,221,255");
        EXPECT_EQ(compiled->ComponentAttr("s", "UISliderComponent", "fillColor"), "10,200,30,255");

        EXPECT_EQ(compiled->Attr("p", "maxValue"), "200");
        EXPECT_FALSE(compiled->Has("p", "minValue"));
        EXPECT_EQ(compiled->Attr("p", "color"), "4,5,6,255");
        EXPECT_EQ(compiled->Attr("p", "trackColor"), "32,32,32,128");
        EXPECT_FALSE(compiled->Has("p", "thumbColor"));
        EXPECT_FALSE(compiled->Has("p", "blocksInput")) << "a progress bar never takes input, a background or not";

        EXPECT_FALSE(compiled->Component("d", "UISliderComponent").attribute("minValue")) << "nothing written, nothing invented";
        EXPECT_FALSE(compiled->Component("d", "UISliderComponent").attribute("step"));

        // and what the controls read from it
        const auto* toggle = compiled->Element("t")->GetComponent<UIToggleComponent>();
        const auto* look = compiled->Element("tSwitch")->GetComponent<UISwitchComponent>();
        const auto* slider = compiled->Element("s")->GetComponent<UISliderComponent>();
        ASSERT_TRUE(toggle != nullptr && look != nullptr && slider != nullptr);
        EXPECT_TRUE(compiled->Element("t")->IsDisabled());
        EXPECT_EQ(toggle->onValue.ToStringView(), "1");
        EXPECT_EQ(look->fillColor.g, 200);
        EXPECT_FLOAT_EQ(slider->minValue, -5.0f);
        EXPECT_FLOAT_EQ(slider->step, 0.5f);
        EXPECT_TRUE(compiled->Element("s")->IsDisabled());
    }

    TEST_F(UIWebTest, NumbersOfTheWidgetsAreWrittenAsNumbers) {
        const auto compiled = Compile(R"(<div id="s" data-widget="slider" data-min=" 0.0 " data-max="1e2" data-step="1.50"></div>)");
        EXPECT_EQ(compiled->ComponentAttr("s", "UISliderComponent", "minValue"), "0");
        EXPECT_EQ(compiled->ComponentAttr("s", "UISliderComponent", "maxValue"), "100");
        EXPECT_EQ(compiled->ComponentAttr("s", "UISliderComponent", "step"), "1.5");
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
        EXPECT_EQ(onButton->Find("b").attribute("type").as_string(), std::string("UIElement")) << "a button that is no segment is composed";
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
        EXPECT_EQ(compiled->report.elements, 5u) << "the toggle is a row and two children, the slider and the bar one element each";
        EXPECT_EQ(compiled->report.rules, 1u);
        EXPECT_EQ(compiled->report.declarations, 2u);
        EXPECT_EQ(compiled->report.declarationsUnsupported, 0u);
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    TEST_F(UIWebTest, AToggleCaptionKeepsItsStringReferenceAndVariables) {
        const auto compiled = Compile(R"(<div id="t" data-widget="toggle" data-bind="a">{#settings.sound}: <b>{state}</b></div>)");
        EXPECT_EQ(compiled->ComponentAttr("tCaption", "UITextComponent", "text"), "{#settings.sound}: {state}") << "inline elements are part of the caption";
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
        EXPECT_EQ(compiled->ComponentAttr("b", "UITextComponent", "fontFamily"), "Roboto");
        EXPECT_EQ(compiled->ComponentAttr("b", "UITextComponent", "fontWeight"), "700");
        EXPECT_EQ(compiled->ComponentAttr("b", "UITextComponent", "textShadow"), "0,1,#000000FF") << "the colour may come first";
        EXPECT_EQ(compiled->ComponentAttr("b", "UITextComponent", "textOutline"), "1,#FFFFFFFF");
        EXPECT_EQ(compiled->Attr("b", "hoverColor"), "48,48,48,255");
        EXPECT_TRUE(compiled->report.issues.empty());
        const auto* text = compiled->Element("b")->GetComponent<UITextComponent>();
        ASSERT_NE(text, nullptr);
        EXPECT_EQ(text->fontWeight, 700);
        EXPECT_FLOAT_EQ(text->textEffects.shadowOffsetY, 1.0f);
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
        EXPECT_EQ(compiled->ComponentAttr("c", "UITextComponent", "textShadow"), "1,1,#000000FF") << "a button's text follows the container too";
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
        EXPECT_EQ(compiled->Find("b").attribute("type").as_string(), std::string("UIElement"));
        EXPECT_EQ(compiled->ComponentAttr("b", "UITextComponent", "text"), "{#menu.start}");
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
        auto* button = compiled->Element("b")->GetComponent<UITextComponent>();
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
        EXPECT_EQ(compiled->ComponentAttr("b", "UIInputClicker", "disabled"), "true");
        EXPECT_EQ(compiled->ComponentAttr("b", "UIInputClicker", "action"), "Go") << "action works as well as data-action";
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    TEST_F(UIWebTest, AButtonCompilesToWhatTheMigrationStepWritesForTheSameAttributes) {
        const auto compiled = Compile(R"(<button id="b" data-action="Go" data-value="v" disabled title="Tip">Go</button>)",
                                      "#b { width: 120px; height: 56px; font-size: 24px; color: #010203; font-weight: 700; background: #202020 } #b:hover { background: #303030 }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_TRUE(compiled->report.issues.empty());
        // The element `s0001_ui_button_to_components.py` makes of `<element type="UIButton" name="b" width="120" height="56" text="Go" fontSize="24" textColor="1,2,3,255" fontWeight="700"
        // action="Go" value="v" disabled="true" tooltip="Tip" color="32,32,32,255" hoverColor="48,48,48,255"/>`
        constexpr const char* kStepOutput = R"(<element type="UIElement" name="b" width="120" height="56" fontSize="24" color="32,32,32,255" hoverColor="48,48,48,255">
  <components>
    <component type="UITextComponent" text="Go" fontWeight="700" textColor="1,2,3,255" align="Center" verticalAlign="Center" wrap="false"/>
    <component type="UIInputClicker" action="Go" value="v" disabled="true"/>
    <component type="UITooltipComponent" text="Tip"/>
  </components>
</element>)";
        pugi::xml_document reference;
        ASSERT_TRUE(reference.load_string(kStepOutput));
        ExpectSameAttributes(reference.child("element"), compiled->Find("b"));
        EXPECT_EQ(compiled->report.elements, 1u);
    }

    TEST_F(UIWebTest, TheIconFormsAreTheOnesTheMigrationStepWrites) {
        const auto compiled = Compile(R"(<button id="Left" data-icon="Settings" data-action="Opt">Options</button><button id="Only" data-icon="Settings" title="Options" data-action="Opt"></button>)",
                                      "#Left { width: 136px; height: 56px; font-size: 24px; background: #202020 } #Only { width: 56px; height: 56px; background: #202020; -be-icon-color: #ffcc00 }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_TRUE(compiled->report.issues.empty());
        // What `s0001_ui_button_to_components.py` makes of the two `UIButton`s with the same attributes (`icon`, `text`, `fontSize`, `iconTint`, `tooltip`)
        constexpr const char* kStepOutput = R"(<elements>
  <element type="UIElement" name="Left" width="136" height="56" color="32,32,32,255" layout="Stack" direction="Horizontal" padding="8,0,0,0" spacing="6" crossAlign="Center">
    <components><component type="UIInputClicker" action="Opt"/></components>
    <children>
      <element type="UIElement" name="LeftIcon" height="50%" aspectRatio="1"><components><component type="UIImageComponent" sprite="Settings"/></components></element>
      <element type="UIElement" name="LeftCaption" flex="1" height="100%" fontSize="24">
        <components><component type="UITextComponent" text="Options" align="Center" verticalAlign="Center" wrap="false"/></components>
      </element>
    </children>
  </element>
  <element type="UIElement" name="Only" width="56" height="56" color="32,32,32,255">
    <components><component type="UIInputClicker" action="Opt"/><component type="UITooltipComponent" text="Options"/></components>
    <children>
      <element type="UIElement" name="OnlyIcon" anchor="Center" width="60%" height="60%" aspectRatio="1">
        <components><component type="UIImageComponent" sprite="Settings" tint="255,204,0,255"/></components>
      </element>
    </children>
  </element>
</elements>)";
        pugi::xml_document reference;
        ASSERT_TRUE(reference.load_string(kStepOutput));
        const pugi::xml_node actual = compiled->document.child("ui").child("elements");
        pugi::xml_node actualElement = actual.first_child();
        for (pugi::xml_node expected : reference.child("elements").children()) {
            ASSERT_TRUE(actualElement);
            ExpectSameAttributes(expected, actualElement);
            actualElement = actualElement.next_sibling();
        }
        EXPECT_FALSE(actualElement);
    }

    TEST_F(UIWebTest, ASegmentOfAGroupIsComposedLikeTheButtonBesideIt) {
        const auto compiled =
            Compile(R"(<div id="g" data-widget="segmented" data-bind="v"><button id="a" data-value="x" data-action="Pick">A</button></div><button id="b" data-action="Go">Go</button>)",
                    "#g { display: flex; height: 56px } #b { height: 56px }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_EQ(compiled->Attr("a", "type"), "UIElement");
        EXPECT_EQ(compiled->ComponentAttr("a", "UITextComponent", "text"), "A");
        EXPECT_EQ(compiled->ComponentAttr("a", "UIInputClicker", "action"), "Pick");
        EXPECT_EQ(compiled->ComponentAttr("a", "UIInputClicker", "value"), "x");
        EXPECT_EQ(compiled->Attr("b", "type"), "UIElement");
        EXPECT_NE(dynamic_cast<UISegmented*>(compiled->Element("g")), nullptr);
        EXPECT_EQ(dynamic_cast<UIButton*>(compiled->Element("a")), nullptr) << "the compiler writes no UIButton";
        EXPECT_NE(compiled->Element("a")->GetComponent<UIInputClicker>(), nullptr) << "the group finds its segments by the clicker";
        EXPECT_NE(compiled->Element("b")->GetComponent<UIInputClicker>(), nullptr);
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    // ---- the box: corners, border, gradient, shadow, glow ---------------------------------------------------------------------

    namespace {

        /// The box of an element of a compile, compared with the one the XML of the same attributes makes
        UIBoxStyle BoxOf(const Compiled& compiled, const char* name) {
            const UIElement* element = compiled.Element(name);
            EXPECT_NE(element, nullptr) << name;
            return element != nullptr ? element->box : UIBoxStyle{};
        }

        UIBoxStyle BoxFromXml(const char* attributes) {
            const std::string xml = std::string("<ui><elements><element type=\"UIElement\" name=\"x\" ") + attributes + "/></elements></ui>";
            const UIScreenContent content = ReadXml(xml);
            EXPECT_EQ(content.elements.size(), 1u);
            return content.elements.empty() ? UIBoxStyle{} : content.elements[0]->box;
        }

        bool HasBoxAttribute(const Compiled& compiled, const char* name) {
            for (const char* key : {"cornerRadius", "borderWidth", "borderColor", "gradientDirection", "gradientColor", "shadow", "glow", "highlight", "partShape"}) {
                if (compiled.Has(name, key)) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    TEST_F(UIWebTest, EachBoxPropertyAloneGivesItsAttributeAndTheBoxOfTheXml) {
        const auto compiled = Compile(R"(<div id="radius"></div><div id="border"></div><div id="gradient"></div><div id="shadow"></div><div id="highlight"></div><div id="plain"></div>)", R"(
#radius    { border-radius: 8px 4px; }
#border    { border: 2px solid #ffcc00; }
#gradient  { background: linear-gradient(to right, #112233, #445566); }
#shadow    { box-shadow: 0 2px 6px 1px rgba(0, 0, 0, .5); }
#highlight { -be-highlight: #ffffff40; }
)");
        EXPECT_TRUE(compiled->report.issues.empty()) << compiled->report.issues[0].message.c_str();
        EXPECT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();

        EXPECT_EQ(compiled->Attr("radius", "cornerRadius"), "8,4,8,4");
        EXPECT_EQ(BoxOf(*compiled, "radius"), BoxFromXml("cornerRadius=\"8,4,8,4\""));

        EXPECT_EQ(compiled->Attr("border", "borderWidth"), "2");
        EXPECT_EQ(compiled->Attr("border", "borderColor"), "255,204,0,255");
        EXPECT_EQ(BoxOf(*compiled, "border"), BoxFromXml("borderWidth=\"2\" borderColor=\"255,204,0,255\""));

        EXPECT_EQ(compiled->Attr("gradient", "color"), "17,34,51,255");
        EXPECT_EQ(compiled->Attr("gradient", "gradientColor"), "68,85,102,255");
        EXPECT_EQ(compiled->Attr("gradient", "gradientDirection"), "Horizontal");
        EXPECT_EQ(BoxOf(*compiled, "gradient"), BoxFromXml("gradientDirection=\"Horizontal\" gradientColor=\"68,85,102,255\""));
        EXPECT_EQ(compiled->Element("gradient")->color.r, 17);

        EXPECT_EQ(compiled->Attr("shadow", "shadow"), "0,2,6,1,#00000080");
        EXPECT_FALSE(compiled->Has("shadow", "glow"));
        EXPECT_EQ(BoxOf(*compiled, "shadow"), BoxFromXml("shadow=\"0,2,6,1,#00000080\""));

        EXPECT_EQ(compiled->Attr("highlight", "highlight"), "255,255,255,64");
        EXPECT_EQ(BoxOf(*compiled, "highlight"), BoxFromXml("highlight=\"255,255,255,64\""));

        EXPECT_FALSE(HasBoxAttribute(*compiled, "plain")) << "no box property, no box attribute";
        EXPECT_TRUE(BoxOf(*compiled, "plain") == UIBoxStyle{});
    }

    TEST_F(UIWebTest, ACardWithRadiusBorderGradientAndTwoShadowLayers) {
        const auto compiled = Compile(R"(<div id="card" class="card"></div>)", R"(
.card { left: 10px; top: 10px; width: 120px; height: 60px; border-radius: 12px; border: 1px solid #ffffff80;
        background: linear-gradient(180deg, #304050, #102030); box-shadow: 0 0 10px 2px #ffd700a0, 0 4px 8px #000000c0; }
)");
        EXPECT_TRUE(compiled->report.issues.empty());
        EXPECT_EQ(compiled->Attr("card", "cornerRadius"), "12");
        EXPECT_EQ(compiled->Attr("card", "gradientDirection"), "Vertical");
        // CSS paints the first layer on top; the engine paints the glow over the shadow
        EXPECT_EQ(compiled->Attr("card", "glow"), "0,0,10,2,#FFD700A0");
        EXPECT_EQ(compiled->Attr("card", "shadow"), "0,4,8,0,#000000C0");
        const UIBoxStyle expected = BoxFromXml(
            R"(cornerRadius="12" borderWidth="1" borderColor="255,255,255,128" gradientDirection="Vertical" gradientColor="16,32,48,255" glow="0,0,10,2,#FFD700A0" shadow="0,4,8,0,#000000C0")");
        EXPECT_EQ(BoxOf(*compiled, "card"), expected);
        EXPECT_EQ(compiled->Attr("card", "color"), "48,64,80,255");

        LaidOut web(compiled->content);
        web.At(1280.0f, 720.0f);
        EXPECT_NEAR(web.Find("card")->rect.w, 120.0f, kTolerance) << "the box paint is not layout";
    }

    TEST_F(UIWebTest, APillIsARadiusOfAThousandPixels) {
        const auto compiled = Compile(R"(<button id="b" data-action="Go">Go</button>)", "#b { width: 120px; height: 40px; border-radius: 999px; background: #336699 }");
        EXPECT_EQ(compiled->Attr("b", "cornerRadius"), "999");
        EXPECT_EQ(compiled->Element("b")->box.cornerRadius, UICorners::Uniform(999.0f));
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    TEST_F(UIWebTest, LonghandsAndShorthandsOfTheBoxAreOneCascadeByOrder) {
        const auto compiled = Compile(R"(<div id="a"></div><div id="b"></div><div id="c"></div>)", R"(
#a { border-top-left-radius: 20px; border-radius: 4px; }
#b { border-radius: 4px; border-top-left-radius: 20px; border-bottom-right-radius: 9px; }
#c { border: 3px solid red; border-width: 5px; border-color: blue; }
)");
        EXPECT_TRUE(compiled->report.issues.empty());
        EXPECT_EQ(compiled->Attr("a", "cornerRadius"), "4") << "the shorthand after the longhand wins";
        EXPECT_EQ(compiled->Attr("b", "cornerRadius"), "20,4,9,4") << "a longhand after the shorthand changes one corner";
        EXPECT_EQ(compiled->Attr("c", "borderWidth"), "5");
        EXPECT_EQ(compiled->Attr("c", "borderColor"), "0,0,255,255");

        const auto style = Compile(R"(<div id="a"></div><div id="b"></div><div id="c"></div>)", R"(
#a { border: 3px solid red; border-style: none; }
#b { border: 3px solid red; border: none; }
#c { border: 3px solid red; border-width: 0; }
)");
        EXPECT_FALSE(HasBoxAttribute(*style, "a"));
        EXPECT_FALSE(HasBoxAttribute(*style, "b"));
        EXPECT_FALSE(HasBoxAttribute(*style, "c"));
        EXPECT_TRUE(style->report.issues.empty());
    }

    TEST_F(UIWebTest, ABackgroundAfterAGradientResetsIt) {
        const auto compiled = Compile(R"(<div id="a"></div><div id="b"></div><div id="c"></div>)", R"(
#a { background: linear-gradient(red, blue); background: #123456; }
#b { background-image: linear-gradient(red, blue); background-color: #123456; }
#c { background: linear-gradient(red, blue); background-image: none; }
)");
        EXPECT_TRUE(compiled->report.issues.empty());
        EXPECT_FALSE(compiled->Has("a", "gradientColor"));
        EXPECT_EQ(compiled->Attr("a", "color"), "18,52,86,255");
        // The gradient is the image over the color: the color has no effect while the gradient is set
        EXPECT_EQ(compiled->Attr("b", "color"), "255,0,0,255");
        EXPECT_EQ(compiled->Attr("b", "gradientColor"), "0,0,255,255");
        EXPECT_FALSE(compiled->Has("c", "gradientColor"));
    }

    TEST_F(UIWebTest, AGradientBesideASpriteGivesBoth) {
        const auto compiled = Compile(R"(<div id="a"></div>)", "#a { width: 50px; height: 50px; background: linear-gradient(red, blue) url(Panel9) }");
        EXPECT_TRUE(compiled->report.issues.empty());
        EXPECT_EQ(compiled->Find("a").attribute("type").as_string(), std::string("UIPanel"));
        EXPECT_EQ(compiled->Attr("a", "sprite"), "Panel9");
        EXPECT_EQ(compiled->Attr("a", "gradientColor"), "0,0,255,255");
        EXPECT_EQ(compiled->Attr("a", "color"), "255,0,0,255");
    }

    TEST_F(UIWebTest, TheShapeOfThePartsIsForTheWidgetsOnly) {
        const auto compiled = Compile(
            R"(<div id="t" data-widget="toggle">T</div><div id="s" data-widget="slider"></div><div id="p" data-widget="progress"></div><div id="d"></div><div id="sq" data-widget="toggle">T</div>)",
            "#t, #s, #p, #d { -be-shape: round } #sq { -be-shape: Square }");
        EXPECT_EQ(compiled->ComponentAttr("tSwitch", "UISwitchComponent", "partShape"), "Round");
        EXPECT_EQ(compiled->ComponentAttr("s", "UISliderComponent", "partShape"), "Round");
        EXPECT_EQ(compiled->Attr("p", "partShape"), "Round");
        EXPECT_FALSE(compiled->Has("d", "partShape"));
        EXPECT_FALSE(compiled->Component("sqSwitch", "UISwitchComponent").attribute("partShape")) << "Square is the default";
        EXPECT_EQ(compiled->IssuesMentioning("-be-shape applies to a toggle, slider or progress bar only"), 1u);
        EXPECT_EQ(compiled->report.issues.size(), 1u);
        EXPECT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_EQ(compiled->Element("tSwitch")->GetComponent<UISwitchComponent>()->partShape, UIPartShape::Round);
        EXPECT_EQ(compiled->Element("s")->GetComponent<UISliderComponent>()->partShape, UIPartShape::Round);
        EXPECT_EQ(compiled->Element("p")->Cast<UIProgressBar>()->partShape, UIPartShape::Round);
    }

    TEST_F(UIWebTest, AToggleWithATextColorGetsTheTrackAndThumbTheClassDerivedFromIt) {
        const auto compiled = Compile(R"(<div id="dark" data-widget="toggle">A</div><div id="white" data-widget="toggle">B</div><div id="given" data-widget="toggle">C</div>
<div id="themed" data-widget="toggle">D</div><div id="plain" data-widget="toggle">E</div>)",
                                      R"(
#dark   { color: rgb(190, 196, 190); }
#white  { color: #ffffff; }
#given  { color: rgb(10, 20, 30); -be-track-color: rgb(1, 2, 3); }
#themed { color: var(--text); -be-track-color: var(--muted); }
)");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_TRUE(compiled->report.issues.empty()) << compiled->report.issues[0].message.c_str();
        // The same numbers as tools/migrate/steps/_ui_value.py: the text colour at the alpha of UISwitchComponent::kOffTrackAlpha (cut, not rounded), and the text colour
        EXPECT_EQ(compiled->ComponentAttr("darkSwitch", "UISwitchComponent", "trackColor"), "190,196,190,89");
        EXPECT_EQ(compiled->ComponentAttr("darkSwitch", "UISwitchComponent", "thumbColor"), "190,196,190,255");
        EXPECT_EQ(compiled->ComponentAttr("darkCaption", "UITextComponent", "textColor"), "190,196,190,255");
        EXPECT_TRUE(compiled->Component("whiteSwitch", "UISwitchComponent").attribute("trackColor").empty()) << "a white caption is what the switch assumes";
        EXPECT_TRUE(compiled->Component("whiteSwitch", "UISwitchComponent").attribute("thumbColor").empty());
        EXPECT_EQ(compiled->ComponentAttr("givenSwitch", "UISwitchComponent", "trackColor"), "1,2,3,255") << "the author's colour stays";
        EXPECT_EQ(compiled->ComponentAttr("givenSwitch", "UISwitchComponent", "thumbColor"), "10,20,30,255");
        EXPECT_EQ(compiled->ComponentAttr("themedSwitch", "UISwitchComponent", "trackColor"), "@muted");
        EXPECT_EQ(compiled->ComponentAttr("themedSwitch", "UISwitchComponent", "thumbColor"), "@text") << "a token is copied to the thumb";
        EXPECT_TRUE(compiled->Component("plainSwitch", "UISwitchComponent").attribute("thumbColor").empty());
    }

    TEST_F(UIWebTest, AThemeTokenTextColorOfAToggleNeedsATrackColor) {
        const auto compiled = Compile(R"(<div id="t" data-widget="toggle">A</div>)", "#t { color: var(--text); }");
        EXPECT_EQ(compiled->IssuesMentioning("needs -be-track-color"), 1u);
        EXPECT_EQ(compiled->ComponentAttr("tSwitch", "UISwitchComponent", "thumbColor"), "@text");
    }

    TEST_F(UIWebTest, ALayoutOnAToggleIsLeftOutAndAFontSizeVariantMovesToTheCaption) {
        const auto compiled = Compile(R"(<div id="t" data-widget="toggle">A</div>)", R"(
#t { display: flex; gap: 20px; height: 50px; font-size: 20px; }
@media (max-width: 480px) { #t { font-size: 14px; } }
)");
        EXPECT_EQ(compiled->IssuesMentioning("lays its caption and switch out itself"), 1u);
        EXPECT_EQ(compiled->Attr("t", "spacing"), "8");
        EXPECT_EQ(compiled->Attr("t", "direction"), "Horizontal");
        EXPECT_EQ(compiled->Attr("tCaption", "fontSize"), "20");
        EXPECT_FALSE(compiled->Has("t", "fontSize"));
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        const std::string xml = compiled->xml.c_str();
        EXPECT_NE(xml.find(R"(<set element="tCaption" fontSize="14")"), std::string::npos) << xml;
        EXPECT_EQ(xml.find(R"(<set element="t" fontSize)"), std::string::npos) << xml;
    }

    // ---- the icon and the tooltip of a button ----------------------------------------------------------------------------------

    namespace {

        constexpr const char* kIconButtonsHtml = R"(
<div id="Bar">
  <button id="Only" data-icon="Settings" title="Options" data-action="Opt"></button>
  <button id="Left" data-icon="Settings" data-action="Opt">Options</button>
  <button id="Above" data-icon="Settings" data-icon-position="above" title="{#opt.tip}" data-action="Opt">Options</button>
  <button id="Plain" data-action="Opt">Plain</button>
</div>)";

        constexpr const char* kIconButtonsCss = R"(
#Bar { anchor: top-left; left: 16px; top: 16px; width: 240px; height: 150px; display: flex; flex-wrap: wrap; gap: 12px; }
#Only { width: 56px; height: 56px; -be-icon-color: #ffcc00; }
#Left { width: 136px; height: 56px; }
#Above { width: 100px; height: 72px; }
#Plain { width: 80px; height: 56px; }
)";

    }  // namespace

    TEST_F(UIWebTest, AnIconButtonCompilesToTheComposedFormsWithTheIconInAChild) {
        const auto compiled = Compile(kIconButtonsHtml, kIconButtonsCss);
        // the one note: `above` has no composed form
        EXPECT_EQ(compiled->report.issues.size(), 1u) << compiled->xml.c_str();
        EXPECT_EQ(compiled->IssuesMentioning("data-icon-position=\"above\" is not supported on a <button> yet"), 1u);
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();

        // Icon only: the button keeps its box, the icon is a centred square child that takes the tint and the clicker the action
        EXPECT_EQ(compiled->Attr("Only", "type"), "UIElement");
        for (const char* key : {"icon", "iconTint", "iconPlacement", "tooltip", "text", "layout"}) {
            EXPECT_FALSE(compiled->Has("Only", key)) << key;
        }
        EXPECT_EQ(compiled->Attr("OnlyIcon", "anchor"), "Center");
        EXPECT_EQ(compiled->Attr("OnlyIcon", "width"), "60%");
        EXPECT_EQ(compiled->Attr("OnlyIcon", "height"), "60%");
        EXPECT_EQ(compiled->Attr("OnlyIcon", "aspectRatio"), "1");
        EXPECT_EQ(compiled->ComponentAttr("OnlyIcon", "UIImageComponent", "sprite"), "Settings");
        EXPECT_EQ(compiled->ComponentAttr("OnlyIcon", "UIImageComponent", "tint"), "255,204,0,255");
        EXPECT_EQ(compiled->ComponentAttr("Only", "UIInputClicker", "action"), "Opt");
        EXPECT_EQ(compiled->ComponentAttr("Only", "UITooltipComponent", "text"), "Options");
        EXPECT_TRUE(compiled->Component("Only", "UITextComponent").empty()) << "no caption, no text component";
        EXPECT_EQ(static_cast<float>(UIButton::kIconOnlyShare), 0.6f) << "the 60% above is the share of UIButton";

        // Icon left of a caption: a horizontal stack of the icon and the caption, the caption holds the text and the font size
        EXPECT_EQ(compiled->Attr("Left", "layout"), "Stack");
        EXPECT_EQ(compiled->Attr("Left", "direction"), "Horizontal");
        EXPECT_EQ(compiled->Attr("Left", "padding"), "8,0,0,0");
        EXPECT_EQ(compiled->Attr("Left", "spacing"), "6");
        EXPECT_EQ(compiled->Attr("Left", "crossAlign"), "Center");
        EXPECT_EQ(compiled->Attr("LeftIcon", "height"), "50%");
        EXPECT_EQ(compiled->Attr("LeftIcon", "aspectRatio"), "1");
        EXPECT_FALSE(compiled->Has("LeftIcon", "width"));
        EXPECT_FALSE(compiled->Has("LeftIcon", "anchor"));
        EXPECT_EQ(compiled->Attr("LeftCaption", "flex"), "1");
        EXPECT_EQ(compiled->Attr("LeftCaption", "height"), "100%");
        EXPECT_EQ(compiled->ComponentAttr("LeftCaption", "UITextComponent", "text"), "Options");
        EXPECT_EQ(compiled->ComponentAttr("LeftCaption", "UITextComponent", "align"), "Center");
        EXPECT_FALSE(compiled->Has("LeftIcon", "text"));
        EXPECT_TRUE(compiled->Component("Left", "UITextComponent").empty()) << "the text is the caption's";
        EXPECT_TRUE(compiled->Component("Left", "UITooltipComponent").empty());
        EXPECT_FALSE(compiled->Component("LeftIcon", "UIImageComponent").attribute("tint")) << "no tint: the icon follows the text colour";
        EXPECT_EQ(static_cast<float>(UIButton::kIconWithTextShare), 0.5f);
        EXPECT_EQ(static_cast<float>(UIButton::kIconPadding), 8.0f);
        EXPECT_EQ(static_cast<float>(UIButton::kIconGap), 6.0f);

        // Above is left, with the tooltip kept
        EXPECT_EQ(compiled->Attr("Above", "layout"), "Stack");
        EXPECT_EQ(compiled->ComponentAttr("Above", "UITooltipComponent", "text"), "{#opt.tip}");
        EXPECT_NE(compiled->Find("AboveCaption"), pugi::xml_node());

        // A plain button has no children
        EXPECT_FALSE(compiled->Find("Plain").child("children"));
        EXPECT_FALSE(compiled->Has("Plain", "layout"));

        const UIElement* only = compiled->Element("Only");
        ASSERT_NE(only, nullptr);
        ASSERT_EQ(only->children.size(), 1u);
        const auto* image = only->children[0]->GetComponent<UIImageComponent>();
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->sprite, "Settings"_intern);
        EXPECT_EQ(image->tint.r, 255);
        EXPECT_EQ(image->tint.g, 204);
        const UIElement* left = compiled->Element("Left");
        ASSERT_NE(left, nullptr);
        EXPECT_EQ(left->children.size(), 2u);
        EXPECT_EQ(compiled->report.elements, 10u) << "Bar, three icon buttons with two children or one, a plain button";
    }

    TEST_F(UIWebTest, AnIconWithoutAnIconColorTakesTheTextColorAndAnIconOnlyButtonKeepsItForItsBar) {
        const auto compiled = Compile(R"(<button id="a" data-icon="Star"></button><button id="b" data-icon="Star"></button>)", "#a { color: #ff0000 } #b { }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_EQ(compiled->ComponentAttr("aIcon", "UIImageComponent", "tint"), "255,0,0,255");
        EXPECT_EQ(compiled->ComponentAttr("a", "UITextComponent", "textColor"), "255,0,0,255") << "the bar of a selected button takes it";
        EXPECT_EQ(compiled->Component("a", "UITextComponent").attribute("text"), pugi::xml_attribute()) << "a colour and nothing else";
        EXPECT_FALSE(compiled->Component("bIcon", "UIImageComponent").empty());
        EXPECT_FALSE(compiled->Has("bIcon", "tint"));
        EXPECT_TRUE(compiled->Component("b", "UITextComponent").empty());
        EXPECT_TRUE(compiled->report.issues.empty());
    }

    TEST_F(UIWebTest, AnIconButtonWithoutAnIdHasChildrenWithoutNames) {
        const auto compiled = Compile(R"(<button data-icon="Star">Go</button>)");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        const pugi::xml_node button = compiled->document.child("ui").child("elements").child("element");
        ASSERT_TRUE(button);
        size_t children = 0;
        for (pugi::xml_node child : button.child("children").children()) {
            EXPECT_TRUE(child.attribute("name").empty());
            ++children;
        }
        EXPECT_EQ(children, 2u);
    }

    TEST_F(UIWebTest, AFlexOnAnIconButtonIsLeftOutAndReported) {
        const auto compiled = Compile(R"(<button id="b" data-icon="Star">Go</button>)", "#b { display: flex; gap: 20px; padding: 4px; width: 120px; height: 56px }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_EQ(compiled->IssuesMentioning("lays its icon and caption out itself"), 1u);
        EXPECT_EQ(compiled->Attr("b", "spacing"), "6") << "the form's own";
        EXPECT_EQ(compiled->Attr("b", "padding"), "8,0,0,0");
    }

    TEST_F(UIWebTest, AnIconSegmentIsTheIconFormAndTheVariableSelectsIt) {
        const auto compiled = Compile(
            R"(<div id="g" data-widget="segmented" data-bind="v"><button id="a" data-value="x" data-icon="Star" title="T">A</button><button id="b" data-value="y" data-icon="Star"></button></div>)",
            "div { display: flex; height: 56px } button { flex: 1; color: #112233 } #a { -be-icon-color: #fff } #a:checked { background: #405060 }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_FALSE(compiled->report.HasIssues()) << compiled->report.issues[0].message.c_str();
        EXPECT_EQ(compiled->Attr("a", "type"), "UIElement");
        EXPECT_EQ(compiled->Attr("a", "selectedColor"), "64,80,96,255");
        EXPECT_EQ(compiled->ComponentAttr("aIcon", "UIImageComponent", "sprite"), "Star");
        EXPECT_EQ(compiled->ComponentAttr("aIcon", "UIImageComponent", "tint"), "255,255,255,255");
        EXPECT_EQ(compiled->ComponentAttr("aCaption", "UITextComponent", "text"), "A");
        EXPECT_EQ(compiled->ComponentAttr("a", "UITooltipComponent", "text"), "T");
        EXPECT_EQ(compiled->ComponentAttr("b", "UITextComponent", "textColor"), "17,34,51,255") << "an icon-only segment keeps the colour for its bar";

        auto* group = dynamic_cast<UISegmented*>(compiled->Element("g"));
        UIElement* a = compiled->Element("a");
        UIElement* b = compiled->Element("b");
        ASSERT_TRUE(group != nullptr && a != nullptr && b != nullptr);
        CoreManager::GetUIVars().Set("v"_intern, "x");
        group->SyncChildren();
        EXPECT_TRUE(a->selected);
        EXPECT_FALSE(b->selected);
        EXPECT_EQ(a->GetStateAccent().r, 17) << "the bar takes the colour of the caption child";
        EXPECT_EQ(b->GetStateAccent().r, 17) << "and that of an icon-only segment its own text component";
        CoreManager::GetUIVars().Set("v"_intern, "y");
        group->SyncChildren();
        EXPECT_FALSE(a->selected);
        EXPECT_TRUE(b->selected);
    }

    TEST_F(UIWebTest, ASegmentOfAnIconAboveIsTheIconLeftFormAndSaysSo) {
        const auto compiled = Compile(R"(<div id="g" data-widget="segmented" data-bind="v"><button id="a" data-value="x" data-icon="Star" data-icon-position="above">A</button></div>)",
                                      "div { display: flex; height: 56px }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_EQ(compiled->IssuesMentioning("not supported on a <button> yet"), 1u);
        EXPECT_FALSE(compiled->Has("a", "iconPlacement"));
        EXPECT_NE(compiled->Find("aCaption"), pugi::xml_node());
    }

    TEST_F(UIWebTest, ACompiledSegmentedGroupPassesTheSizeMatrix) {
        const auto compiled = Compile(
            R"(<div id="g" data-widget="segmented" data-bind="v"><button data-value="x" data-icon="Star">One</button><button data-value="y">Two</button><button data-value="z" data-icon="Star"></button></div>)",
            "div { display: flex; gap: 4px; width: 90%; height: 56px } button { flex: 1; height: 56px; font-size: 20px }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        LaidOut web(compiled->content);
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const auto failures = ValidateOnMatrix(web.layout, options);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(UIWebTest, AMediaFontSizeOfAnIconLeftButtonIsWrittenForItsCaption) {
        const auto compiled = Compile(R"(<button id="b" data-icon="Star" data-action="Go">Go</button><button id="c" data-action="Go">Go</button><button id="d" data-icon="Star"></button>)",
                                      "button { font-size: 24px; width: 140px; height: 56px } @media (orientation: portrait) { button { font-size: 18px } }");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_FALSE(compiled->Has("b", "fontSize")) << "the caption has it";
        EXPECT_EQ(compiled->Attr("bCaption", "fontSize"), "24");
        EXPECT_EQ(compiled->Attr("c", "fontSize"), "24") << "a plain button keeps it";
        EXPECT_EQ(compiled->Attr("d", "fontSize"), "24") << "an icon-only button has no caption: it stays";
        ASSERT_EQ(compiled->content.variants.size(), 1u);
        const auto& overrides = compiled->content.variants[0].overrides;
        const auto find = [&](const char* element) -> const UIElementOverride* {
            for (const auto& entry : overrides) {
                if (entry.element == PoolString::Intern(element)) {
                    return &entry;
                }
            }
            return nullptr;
        };
        ASSERT_NE(find("bCaption"), nullptr) << compiled->xml.c_str();
        EXPECT_FLOAT_EQ(find("bCaption")->fontSize.value_or(0.0f), 18.0f);
        EXPECT_EQ(find("b"), nullptr) << "nothing of the button itself changes";
        ASSERT_NE(find("c"), nullptr);
        EXPECT_FLOAT_EQ(find("c")->fontSize.value_or(0.0f), 18.0f);
        EXPECT_EQ(compiled->report.issues.size(), 0u) << compiled->report.issues[0].message.c_str();
    }

    TEST_F(UIWebTest, TheIconButtonsInHtmlPassTheSizeMatrix) {
        const auto compiled = Compile(kIconButtonsHtml, kIconButtonsCss);
        LaidOut web(compiled->content);
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const auto failures = ValidateOnMatrix(web.layout, options);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(UIWebTest, AnIconOnlyButtonThatIsTooSmallIsReportedByItsRect) {
        const auto compiled = Compile(kIconButtonsHtml, std::string(kIconButtonsCss) + "#Only { width: 32px; height: 32px; }");
        LaidOut web(compiled->content);
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const auto failures = ValidateOnMatrix(web.layout, options);
        EXPECT_FALSE(failures.empty()) << "the rect is the touch target, whatever the icon inside";
        for (const UIMatrixFailure& failure : failures) {
            for (const UILayoutIssue& issue : failure.issues) {
                EXPECT_EQ(issue.kind, UILayoutIssueKind::TouchTargetTooSmall) << DescribeMatrixFailures(failures).c_str();
                EXPECT_EQ(issue.element, "Only"_intern);
            }
        }
    }

    TEST_F(UIWebTest, TheIconAndTooltipAttributesOffAButtonAreReported) {
        const auto compiled = Compile(R"(<div id="d" data-icon="Settings" data-icon-position="above" title="Tip"></div><span id="l">Text</span><div id="t" data-widget="toggle" title="Tip">T</div>)",
                                      "#l { -be-icon-color: #fff; } #d { -be-icon-color: #fff; }");
        EXPECT_EQ(compiled->IssuesMentioning("data-icon only applies to a <button>"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("data-icon-position only applies to a <button>"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("title only applies to a <button>"), 1u) << "one message for the div and the toggle";
        EXPECT_EQ(compiled->IssuesMentioning("-be-icon-color applies to a button only"), 1u);
        for (const char* name : {"d", "l", "t"}) {
            for (const char* key : {"icon", "iconTint", "iconPlacement", "tooltip"}) {
                EXPECT_FALSE(compiled->Has(name, key)) << name << " " << key;
            }
        }
    }

    TEST_F(UIWebTest, ABadIconPositionAndAnIconColorWithoutAnIconAreReported) {
        const auto compiled =
            Compile(R"(<button id="a" data-icon="Settings" data-icon-position="under"></button><button id="b" data-icon-position="above">B</button><button id="c" data-icon="">C</button>)",
                    "#b { -be-icon-color: #fff } #c { -be-icon-color: #fff }");
        EXPECT_EQ(compiled->IssuesMentioning("data-icon-position=\"under\" is not supported"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("data-icon-position has no effect without data-icon"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("data-icon needs the name of a sprite"), 1u);
        EXPECT_EQ(compiled->ComponentAttr("aIcon", "UIImageComponent", "sprite"), "Settings");
        EXPECT_FALSE(compiled->Has("a", "iconPlacement")) << "the default is kept";
        EXPECT_TRUE(compiled->Find("bIcon").empty()) << "no icon, no child";
        EXPECT_TRUE(compiled->Find("cIcon").empty());
        EXPECT_EQ(compiled->IssuesMentioning("-be-icon-color has no effect on a button without data-icon"), 1u) << "one message for both buttons";
        EXPECT_FALSE(compiled->Has("b", "iconTint"));
        EXPECT_FALSE(compiled->Has("c", "iconTint"));
        EXPECT_EQ(compiled->report.issues.size(), 4u);
    }

    TEST_F(UIWebTest, AButtonStateRuleWithAGradientSetsTheStateColorAndReportsTheEnd) {
        const auto compiled =
            Compile(R"(<button id="b" data-action="Go">Go</button>)", "button { background: linear-gradient(#202020, #101010) } button:hover { background: linear-gradient(#303030, #202020) } "
                                                                      "button:active { background: linear-gradient(#050505, #000000) }");
        EXPECT_EQ(compiled->Attr("b", "color"), "32,32,32,255");
        EXPECT_EQ(compiled->Attr("b", "gradientColor"), "16,16,16,255");
        EXPECT_EQ(compiled->Attr("b", "hoverColor"), "48,48,48,255");
        EXPECT_EQ(compiled->Attr("b", "pressedColor"), "5,5,5,255");
        EXPECT_EQ(compiled->IssuesMentioning("the far end of a state gradient is derived from its start color"), 1u) << "once, for both states";
        EXPECT_EQ(compiled->report.issues.size(), 1u);
        EXPECT_EQ(compiled->Element("b")->box.gradientColor.r, 16) << "the box of the base look, the state shifts come from the start color";
    }

    TEST_F(UIWebTest, ABoxPropertyInAStateRuleIsReported) {
        const auto compiled =
            Compile(R"(<button id="b" data-action="Go">Go</button>)",
                    "button { border: 2px solid red; box-shadow: 0 0 4px red; border-radius: 6px } button:hover { border-color: blue; box-shadow: 0 0 8px blue; border-radius: 12px }");
        EXPECT_EQ(compiled->IssuesMentioning("'borderColor' of a <button>"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("'shadow' of a <button>"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("'cornerRadius' of a <button>"), 1u);
        EXPECT_FALSE(compiled->Has("b", "hoverColor"));
        EXPECT_EQ(compiled->Attr("b", "borderColor"), "255,0,0,255") << "the base look is kept";
        EXPECT_EQ(compiled->Attr("b", "cornerRadius"), "6");
    }

    TEST_F(UIWebTest, ABadBoxValueIsReportedOnceWithItsLineAndLeavesTheDefault) {
        const auto compiled = Compile(
            R"(<div id="a" class="x"></div><div id="b" class="x"></div>)",
            ".x { border: 2px dashed red;\n  box-shadow: inset 0 0 4px red;\n  border-radius: 50%;\n  background: linear-gradient(red, green, blue);\n  -be-shape: oval;\n  -be-highlight: nope }\n");
        EXPECT_EQ(compiled->IssuesMentioning("'border: 2px dashed red'"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("'box-shadow: inset 0 0 4px red'"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("'border-radius: 50%'"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("'background: linear-gradient(red, green, blue)'"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("'-be-shape: oval'"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("'-be-highlight: nope'"), 1u);
        EXPECT_EQ(compiled->report.declarationsUnsupported, 6u);
        for (const UIWebIssue& issue : compiled->report.issues) {
            if (issue.message.find("'box-shadow") != eastl::string::npos) {
                EXPECT_EQ(issue.line, 2u);
            } else if (issue.message.find("'border-radius") != eastl::string::npos) {
                EXPECT_EQ(issue.line, 3u);
            }
        }
        EXPECT_FALSE(HasBoxAttribute(*compiled, "a"));
        EXPECT_FALSE(compiled->Has("a", "color"));
        EXPECT_TRUE(BoxOf(*compiled, "b") == UIBoxStyle{});
    }

    TEST_F(UIWebTest, AThirdShadowLayerAndABorderWithoutAColorAreReported) {
        const auto compiled = Compile(R"(<div id="a"></div><div id="b"></div>)", "#a { box-shadow: 0 0 1px red, 0 1px 2px green, 0 2px 3px blue } #b { border-width: 2px }");
        EXPECT_EQ(compiled->IssuesMentioning("box-shadow holds at most 2 layers"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("a border needs a color"), 1u);
        EXPECT_EQ(compiled->Attr("a", "glow"), "0,0,1,0,#FF0000FF");
        EXPECT_EQ(compiled->Attr("a", "shadow"), "0,1,2,0,#008000FF");
        EXPECT_FALSE(HasBoxAttribute(*compiled, "b"));
    }

    TEST_F(UIWebTest, ABoxEffectAloneDoesNotTakeClicksAndAGradientPlateDoes) {
        const auto compiled = Compile(R"(<div id="shadow"></div><div id="border"></div><div id="radius"></div><div id="gradient"></div><div id="clear"></div><div id="faint"></div>)", R"(
#shadow   { box-shadow: 0 0 4px red }
#border   { border: 1px solid red }
#radius   { border-radius: 8px }
#gradient { background: linear-gradient(red, blue) }
#clear    { background: linear-gradient(transparent, blue) }
#faint    { background: linear-gradient(red, transparent) }
)");
        for (const char* name : {"shadow", "border", "radius"}) {
            EXPECT_FALSE(compiled->Has(name, "blocksInput")) << name;
        }
        EXPECT_EQ(compiled->Attr("gradient", "blocksInput"), "true");
        EXPECT_EQ(compiled->Attr("faint", "blocksInput"), "true");
        EXPECT_FALSE(compiled->Has("clear", "blocksInput")) << "only a visible start color makes a plate";
        EXPECT_EQ(compiled->Attr("clear", "gradientColor"), "0,0,255,255");
    }

    TEST_F(UIWebTest, AMediaBlockCannotChangeTheBox) {
        const auto compiled = Compile(R"(<div id="a"></div>)", "#a { width: 10px; border-radius: 4px } @media (max-width: 500px) { #a { border-radius: 12px; width: 5px } }");
        EXPECT_EQ(compiled->content.variants.size(), 1u) << "the width still becomes a variant";
        EXPECT_EQ(compiled->IssuesMentioning("@media changes 'cornerRadius'"), 1u);
    }

    TEST_F(UIWebTest, ARoundedGradientCardIsDrawnAsOneGeometryCall) {
        const auto compiled = Compile(R"(<div id="card"></div>)", "#card { left: 0px; top: 0px; width: 100px; height: 40px; border-radius: 10px; background: linear-gradient(#ff0000, #0000ff) }");
        ASSERT_FALSE(compiled->deserializeErrors);
        UIElement* card = compiled->Element("card");
        ASSERT_NE(card, nullptr);
        card->rect = {0.0f, 0.0f, 100.0f, 40.0f};
        RecordingRenderer renderer;
        card->DrawSelf(renderer);
        ASSERT_EQ(renderer.calls.size(), 1u);
        EXPECT_EQ(renderer.calls[0].kind, DrawKind::Geometry);

        const auto flat = Compile(R"(<div id="card"></div>)", "#card { width: 100px; height: 40px; background: #ff0000 }");
        UIElement* plain = flat->Element("card");
        plain->rect = {0.0f, 0.0f, 100.0f, 40.0f};
        renderer.ClearCalls();
        plain->DrawSelf(renderer);
        ASSERT_EQ(renderer.calls.size(), 1u);
        EXPECT_EQ(renderer.calls[0].kind, DrawKind::Rect) << "no box property, the plain rect as before";
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

    // ---- theme tokens: var(--name) ---------------------------------------------------------------------------------------------

    namespace {

        /// Two themes of the tokens the tests below use; the second one leaves `line` to the default theme
        void FillThemes(UIThemes& themes) {
            const PoolString light = PoolString::Intern("light");
            const PoolString dark = PoolString::Intern("dark");
            themes.Set(light, "panel", Color{240, 240, 230, 255});
            themes.Set(light, "line", Color{10, 20, 30, 255});
            themes.Set(light, "accent", Color{40, 90, 60, 255});
            themes.Set(light, "shade", Color{0, 0, 0, 120});
            themes.Set(dark, "panel", Color{30, 32, 40, 255});
            themes.Set(dark, "accent", Color{200, 180, 40, 255});
            themes.Set(dark, "shade", Color{0, 0, 0, 220});
            themes.SetDefaultTheme(light);
        }

        constexpr const char* kTokenMenuCss = R"(
#Column { anchor: center; display: flex; flex-direction: column; align-items: stretch; gap: 16px; width: 90%; max-width: 320px; height: 232px; }
#Title  { height: 88px; font-size: 56px; text-align: center; vertical-align: middle; }
button  { height: 56px; font-size: 28px; }
#Play   { background: var(--play); }
#Quit   { background: var(--quit); }
)";

        constexpr const char* kTokenMenuXml = R"(<ui><elements>
  <element type="UIStack" name="Column" anchor="Center" width="90%" maxWidth="320" height="232" direction="Vertical" spacing="16" crossAlign="Stretch">
    <children>
      <element type="UILabel" name="Title" height="88" text="Match 3" fontSize="56" align="Center" verticalAlign="Center" />
      <element type="UIElement" name="Play" height="56" fontSize="28" color="@play">
        <components><component type="UITextComponent" text="Play" align="Center" verticalAlign="Center" wrap="false"/><component type="UIInputClicker" action="Match3Play"/></components>
      </element>
      <element type="UIElement" name="Quit" height="56" fontSize="28" color="@quit">
        <components><component type="UITextComponent" text="Quit" align="Center" verticalAlign="Center" wrap="false"/><component type="UIInputClicker" action="Match3Quit"/></components>
      </element>
    </children>
  </element>
</elements></ui>)";

    }  // namespace

    TEST_F(UIWebTest, AVarOfAColorPropertyBecomesATokenAndEveryFormOfItCompilesLikeTheLiteral) {
        const auto compiled = Compile(R"(
<div id="card"></div>
<div id="grad"></div>
<div id="glow"></div>
<span id="text">Hi</span>
<div id="bar" data-widget="progress"></div>
<div id="knob" data-widget="slider"></div>
<button id="icon" data-action="Go" data-icon="Star">Go</button>)",
                                      R"(
#card { width: 100px; height: 40px; background: var(--panel); border: 2px solid var(--line); box-shadow: 0 2px 4px var(--shade); -be-highlight: var(--accent); }
#grad { width: 100px; height: 40px; background: linear-gradient(to right, var(--panel), var(--accent)); }
#glow { width: 100px; height: 40px; box-shadow: 0 0 6px var(--accent), 1px 2px var(--shade); }
#text { width: 100px; height: 40px; color: var(--line); text-shadow: 1px 1px var(--shade); -be-text-outline: 2px var(--accent); }
#bar  { width: 100px; height: 20px; -be-track-color: var(--panel); -be-fill-color: var(--accent); }
#knob { width: 100px; height: 20px; -be-thumb-color: var(--line); }
#icon { width: 100px; height: 40px; -be-icon-color: var(--accent); }
)");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_TRUE(compiled->report.issues.empty()) << compiled->report.issues[0].message.c_str() << " / " << compiled->xml.c_str();

        EXPECT_EQ(compiled->Attr("card", "color"), "@panel");
        EXPECT_EQ(compiled->Attr("card", "borderColor"), "@line");
        EXPECT_EQ(compiled->Attr("card", "borderWidth"), "2");
        EXPECT_EQ(compiled->Attr("card", "shadow"), "0,2,4,0,@shade");
        EXPECT_EQ(compiled->Attr("card", "highlight"), "@accent");
        EXPECT_EQ(compiled->Attr("grad", "color"), "@panel");
        EXPECT_EQ(compiled->Attr("grad", "gradientColor"), "@accent");
        EXPECT_EQ(compiled->Attr("grad", "gradientDirection"), "Horizontal");
        EXPECT_EQ(compiled->Attr("glow", "glow"), "0,0,6,0,@accent");
        EXPECT_EQ(compiled->Attr("glow", "shadow"), "1,2,0,0,@shade");
        EXPECT_EQ(compiled->Attr("text", "textColor"), "@line");
        EXPECT_EQ(compiled->Attr("text", "textShadow"), "1,1,@shade");
        EXPECT_EQ(compiled->Attr("text", "textOutline"), "2,@accent");
        EXPECT_EQ(compiled->Attr("bar", "trackColor"), "@panel");
        EXPECT_EQ(compiled->Attr("bar", "fillColor"), "@accent");
        EXPECT_EQ(compiled->ComponentAttr("knob", "UISliderComponent", "thumbColor"), "@line");
        EXPECT_EQ(compiled->ComponentAttr("iconIcon", "UIImageComponent", "tint"), "@accent");

        // The XML front-end reads it back into the fields, as tokens
        const auto* card = compiled->Element("card");
        ASSERT_NE(card, nullptr);
        EXPECT_EQ(card->color.token.ToStringView(), "panel");
        const auto* label = compiled->Element("text")->Cast<UILabel>();
        ASSERT_NE(label, nullptr);
        EXPECT_EQ(label->textColor.token.ToStringView(), "line");
        EXPECT_EQ(label->textEffects.shadowColor.token.ToStringView(), "shade");
    }

    TEST_F(UIWebTest, AVarIsAlsoWrittenWithSpacesAndInTheCaseOfTheFunctionName) {
        using UIWebText::ParseColor;
        for (const char* text : {"var(--panel)", "  VAR( --panel )  ", "var(  --panel)"}) {
            const auto color = ParseColor(text);
            ASSERT_TRUE(color) << text;
            EXPECT_TRUE(color->IsToken()) << text;
            EXPECT_EQ(color->token.ToStringView(), "panel") << text;
            EXPECT_EQ(color->a, 0) << text;
            EXPECT_EQ(UIWebText::FormatWebColor(*color), "@panel") << text;
        }
        EXPECT_FALSE(ParseColor("#fff")->IsToken());
        EXPECT_EQ(UIWebText::FormatWebColor(*ParseColor("#102030")), "16,32,48,255");
        // The name keeps its case: a token is not a keyword
        EXPECT_EQ(ParseColor("var(--Panel_2)")->token.ToStringView(), "Panel_2");
        for (const char* text : {"var(--panel, red)", "var(panel)", "var(--)", "var(--1a)", "var()", "var(--a b)", "var(--panel", "vary(--panel)", "env(--panel)", "calc(--panel)"}) {
            EXPECT_FALSE(ParseColor(text)) << text;
        }
    }

    TEST_F(UIWebTest, AStateRuleOfAButtonTakesATokenAndAnEqualTokenIsNoChange) {
        const auto compiled = Compile(R"(<button id="b" data-action="Go">Go</button><button id="same" data-action="Go">Same</button>)",
                                      R"(
button { background: var(--panel); }
button:hover { background: var(--accent); }
button:active { background: var(--line); }
button:disabled { background: var(--shade); }
button:checked { background: var(--panel); }
#same:hover { background: var(--panel); }
)");
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_TRUE(compiled->report.issues.empty()) << compiled->report.issues[0].message.c_str();
        EXPECT_EQ(compiled->Attr("b", "color"), "@panel");
        EXPECT_EQ(compiled->Attr("b", "hoverColor"), "@accent");
        EXPECT_EQ(compiled->Attr("b", "pressedColor"), "@line");
        EXPECT_EQ(compiled->Attr("b", "disabledColor"), "@shade");
        EXPECT_FALSE(compiled->Has("b", "selectedColor")) << "the same token as the base is no change, as an equal literal is none";
        EXPECT_FALSE(compiled->Has("same", "hoverColor"));
    }

    TEST_F(UIWebTest, ATokenFillIsAFillAndTransparentIsNot) {
        const auto compiled = Compile(R"(<div id="a"></div><div id="b"></div><div id="c"></div>)",
                                      "#a { width: 10px; height: 10px; background: var(--panel); } #b { width: 10px; height: 10px; background: transparent; }"
                                      "#c { width: 10px; height: 10px; background: linear-gradient(var(--panel), transparent); }");
        EXPECT_EQ(compiled->Attr("a", "color"), "@panel");
        EXPECT_FALSE(compiled->Has("b", "color"));
        EXPECT_EQ(compiled->Attr("c", "color"), "@panel");
        EXPECT_EQ(compiled->Attr("c", "gradientColor"), "0,0,0,0");
    }

    TEST_F(UIWebTest, AVarThatIsNotUnderstoodIsReportedWithItsLineAndLeavesTheDefault) {
        const auto compiled = Compile(R"(<div id="a"></div>)", "#a { width: 10px; height: 10px;\n"
                                                               "  background: var(--panel, red);\n"
                                                               "  border: 1px solid var(panel);\n"
                                                               "  --panel: #fff;\n"
                                                               "  width: var(--w); }\n"
                                                               "#a { color: var(--bad name); }\n");
        EXPECT_EQ(compiled->IssuesMentioning("'background: var(--panel, red)': var() is read only as var(--name)"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("'border: 1px solid var(panel)'"), 1u);
        EXPECT_EQ(compiled->IssuesMentioning("property '--panel' is not supported"), 1u) << "a custom property is no definition: the tokens are the engine's table";
        EXPECT_EQ(compiled->IssuesMentioning("'width: var(--w)': var() is read only"), 1u) << "a var() of a length is not a token";
        EXPECT_EQ(compiled->IssuesMentioning("'color: var(--bad name)'"), 1u);
        EXPECT_EQ(compiled->report.issues.size(), 5u);
        EXPECT_EQ(compiled->report.declarationsUnsupported, 5u);
        for (const UIWebIssue& issue : compiled->report.issues) {
            EXPECT_EQ(issue.source, "css");
            EXPECT_GE(issue.line, 1u) << issue.message.c_str();
        }
        EXPECT_FALSE(compiled->Has("a", "color")) << "nothing written for the rejected values";
        EXPECT_FALSE(compiled->Has("a", "borderColor"));
        EXPECT_EQ(compiled->Attr("a", "width"), "10") << "a rejected width leaves the earlier one";
    }

    TEST_F(UIWebTest, AVarInAStyleAttributeCompilesAndInAMediaBlockIsAChangeOfTheBox) {
        const auto inlineStyle = Compile(R"HTML(<div id="a" style="width: 10px; height: 10px; background: var(--panel)"></div>)HTML");
        EXPECT_EQ(inlineStyle->Attr("a", "color"), "@panel");
        EXPECT_TRUE(inlineStyle->report.issues.empty());
        const auto media = Compile(R"(<div id="a"></div>)", "#a { width: 10px; height: 10px; background: var(--panel); } @media (orientation: portrait) { #a { background: var(--accent); } }");
        EXPECT_EQ(media->IssuesMentioning("@media changes 'color'"), 1u) << "a token is a box change like a literal colour";
    }

    TEST_F(UIWebTest, TheMenuWrittenWithTokensCompilesToTheMenuWithTokensAndFollowsTheTheme) {
        UIThemes& themes = CoreManager::GetUIThemes();
        themes.Set(PoolString::Intern("light"), "play", Color{40, 90, 60, 255});
        themes.Set(PoolString::Intern("light"), "quit", Color{90, 50, 50, 255});
        themes.Set(PoolString::Intern("dark"), "play", Color{20, 160, 90, 255});
        themes.Set(PoolString::Intern("dark"), "quit", Color{160, 40, 60, 255});
        themes.SetDefaultTheme(PoolString::Intern("light"));

        const auto compiled = Compile(kMenuHtml, kTokenMenuCss);
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_FALSE(compiled->report.HasIssues()) << compiled->report.issues[0].message.c_str() << " / " << compiled->xml.c_str();

        // Compiles to the XML of the menu with `@token`, and that XML lays out as the literal menu does
        const UIScreenContent tokens = ReadXml(kTokenMenuXml);
        ExpectSameElements(tokens.elements, compiled->content.elements);
        LaidOut web(compiled->content);
        LaidOut literal(ReadXml(kMenuXml));
        web.At(1280.0f, 720.0f);
        literal.At(1280.0f, 720.0f);
        for (const char* name : {"Column", "Title", "Play", "Quit"}) {
            ExpectSameRect(literal.Find(name), web.Find(name), name);
        }

        // and draws in the colours of the current theme, and in the other ones after a switch
        UIElement* play = compiled->Element("Play");
        ASSERT_NE(play, nullptr);
        play->ResolveTheme(themes);
        EXPECT_TRUE(ColorsEqual(play->color, Color{40, 90, 60, 255}));
        ASSERT_TRUE(themes.SetTheme(PoolString::Intern("dark")));
        play->ResolveTheme(themes);
        EXPECT_TRUE(ColorsEqual(play->color, Color{20, 160, 90, 255}));
        EXPECT_EQ(play->color.token.ToStringView(), "play");
        UIElement* quit = compiled->Element("Quit");
        quit->ResolveTheme(themes);
        EXPECT_TRUE(ColorsEqual(quit->color, Color{160, 40, 60, 255}));
        EXPECT_TRUE(themes.GetMissing().empty());
    }

    TEST_F(UIWebTest, ACardWithTokensIsDrawnInTheColorsOfTheThemeAndTheOtherOnesAfterASwitch) {
        UIThemes& themes = CoreManager::GetUIThemes();
        FillThemes(themes);
        const auto compiled = Compile(R"(<div id="card"></div>)", "#card { width: 100px; height: 40px; background: var(--panel); }");
        ASSERT_FALSE(compiled->deserializeErrors);
        UIElement* card = compiled->Element("card");
        ASSERT_NE(card, nullptr);
        card->rect = {0.0f, 0.0f, 100.0f, 40.0f};
        RecordingRenderer renderer;

        const auto drawn = [&] {
            renderer.ClearCalls();
            card->ResolveTheme(themes);
            card->DrawSelf(renderer);
            return renderer.calls.empty() ? Color{} : renderer.calls[0].color;
        };
        EXPECT_TRUE(ColorsEqual(drawn(), Color{240, 240, 230, 255}));
        ASSERT_TRUE(themes.SetTheme(PoolString::Intern("dark")));
        EXPECT_TRUE(ColorsEqual(drawn(), Color{30, 32, 40, 255}));
        EXPECT_TRUE(themes.GetMissing().empty());

        // A token the table lacks is magenta and is recorded once
        const auto ghost = Compile(R"(<div id="ghost"></div>)", "#ghost { width: 10px; height: 10px; background: var(--nowhere); }");
        UIElement* element = ghost->Element("ghost");
        element->rect = {0.0f, 0.0f, 10.0f, 10.0f};
        element->ResolveTheme(themes);
        EXPECT_TRUE(ColorsEqual(element->color, kMissingTokenColor));
        ASSERT_EQ(themes.GetMissing().size(), 1u);
        EXPECT_EQ(themes.GetMissing()[0].ToStringView(), "nowhere");
    }

}  // namespace BECore::Tests
