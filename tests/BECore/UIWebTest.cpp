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
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UIWebCompiler.h>
#include <BECore/UI/UIWebText.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
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

    }  // namespace

    class UIWebTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
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
            TempDirectory() : _path(std::filesystem::temp_directory_path() / Format("BagiEngineUIWebTest_{}", ::testing::UnitTest::GetInstance()->current_test_info()->name()).c_str()) {
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
