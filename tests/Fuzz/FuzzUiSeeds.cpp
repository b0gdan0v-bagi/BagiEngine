#include "FuzzSeeds.h"
#include "FuzzUiGeometryInput.h"

namespace BECore::Fuzz {

    namespace {

        constexpr const char* kWebUiFolder = "web_ui";
        constexpr const char* kUiTablesFolder = "ui_tables";
        constexpr const char* kUiGeometryFolder = "ui_geometry";

        /// How deep the nesting seed goes: far more than a screen has, and far less than the limit of the compiler
        constexpr size_t kNestingDepth = 64;

        /// The length of the long attribute and the long selector seeds
        constexpr size_t kLongTextBytes = 4096;

        FuzzBytes TextBytes(StringView text) {
            const auto* begin = reinterpret_cast<const uint8_t*>(text.data());
            return FuzzBytes(begin, begin + text.size());
        }

        /// The input of FuzzWebUi: the html, a NUL byte, the style sheet
        FuzzBytes HtmlAndCss(StringView html, StringView css) {
            FuzzBytes bytes = TextBytes(html);
            bytes.push_back(0);
            const FuzzBytes cssBytes = TextBytes(css);
            bytes.insert(bytes.end(), cssBytes.begin(), cssBytes.end());
            return bytes;
        }

        // The screens of tests/BECore/UIWebTest.cpp (the HUD of the plan's sketch, the Match3 menu and HUD, the settings panel), as a game writes them

        constexpr StringView kHudHtml = R"xml(<link rel="stylesheet" href="hud.css">
<div id="Hud" class="hud">
  <span id="Score">Score: {score}</span>
  <button id="Reset" data-action="Match3Reset">Reset</button>
</div>)xml";

        constexpr StringView kHudCss = R"xml(.hud    { anchor: top-left; left: 16px; top: 16px; width: 220px; height: 96px; background: #000000A0; }
#Score  { left: 12px; top: 10px; width: 150px; height: 32px; font-size: 28px; }
#Reset  { anchor: bottom-right; right: 12px; bottom: 10px; width: 96px; height: 48px; font-size: 24px; }
button  { background: rgb(40, 90, 60); }
button:hover { background: #3a6; }
@media (orientation: portrait) { .hud { anchor: top; left: 0; width: 90%; } }
)xml";

        constexpr StringView kMenuHtml = R"xml(<div id="Column">
  <h1 id="Title">Match 3</h1>
  <button id="Play" data-action="Match3Play">Play</button>
  <button id="Quit" data-action="Match3Quit">Quit</button>
</div>)xml";

        constexpr StringView kMenuCss = R"xml(#Column { anchor: center; display: flex; flex-direction: column; align-items: stretch; gap: 16px; width: 90%; max-width: 320px; height: 232px; }
#Title  { height: 88px; font-size: 56px; text-align: center; vertical-align: middle; }
button  { height: 56px; font-size: 28px; }
#Play   { background: rgb(40, 90, 60); }
#Quit   { background: rgb(90, 50, 50); }
)xml";

        constexpr StringView kMatch3HudHtml = R"xml(<span id="Score">Score: {score}</span>
<div id="Buttons">
  <button id="Reset" data-action="Match3Reset">Reset</button>
  <button id="Menu" data-action="Match3Menu">Menu</button>
</div>
<span id="Seed">seed {seed}</span>)xml";

        constexpr StringView kMatch3HudCss = R"xml(#Score   { left: 96px; top: 12px; width: 250px; height: 40px; font-size: 28px; vertical-align: middle; }
#Buttons { anchor: top-right; right: 96px; top: 12px; width: 260px; height: 48px; display: flex; gap: 12px; align-items: stretch; }
button   { width: 124px; height: 48px; font-size: 24px; }
#Reset   { background: rgb(40, 90, 60); }
#Menu    { background: rgb(70, 70, 100); }
#Seed    { left: 96px; bottom: 26px; width: 300px; height: 24px; font-size: 16px; color: rgb(190, 190, 190); }
)xml";

        constexpr StringView kSettingsHtml = R"xml(<div id="Plate">
  <span id="Title">{#settings.title}</span>
  <div id="Sound" data-widget="toggle" data-bind="soundOn" data-action="SettingsSound">{#settings.sound}</div>
  <div id="MusicRow" class="row centered">
    <span id="MusicCaption" class="caption">{#settings.music}</span>
    <div id="Volume" data-widget="slider" data-bind="volume" data-action="SettingsVolume" data-min="0" data-max="1" data-step="0.1"></div>
  </div>
  <div id="DownloadRow" class="row centered">
    <div id="Progress" data-widget="progress" data-bind="progress" data-min="0" data-max="100"></div>
    <span id="Percent">{progress}%</span>
  </div>
  <div id="LanguageRow" class="row">
    <div id="Language" class="group" data-widget="segmented" data-bind="language">
      <button id="LanguageEn" data-action="SettingsLanguage" data-value="en">{#lang.en}</button>
      <button id="LanguageRu" data-action="SettingsLanguage" data-value="ru">{#lang.ru}</button>
    </div>
  </div>
  <button id="Ok" data-action="SettingsClose" data-icon="check" title="{#settings.ok}">{#settings.ok}</button>
</div>)xml";

        constexpr StringView kSettingsCss =
            R"xml(#Plate    { anchor: center; display: flex; flex-direction: column; align-items: stretch; gap: 12px; padding: 16px; width: 94%; max-width: 640px; height: 416px; background: rgb(36, 40, 52); }
#Title    { height: 44px; font-size: 30px; text-align: center; }
#Sound    { height: 56px; font-size: 24px; vertical-align: middle; -be-track-color: #445566; -be-thumb-color: #ffffff; }
.row      { display: flex; flex-direction: row; align-items: stretch; gap: 8px; height: 56px; }
.centered { align-items: center; }
.caption  { width: 220px; height: 56px; font-size: 24px; vertical-align: middle; }
#Volume   { flex: 1; height: 56px; -be-fill-color: #33aa66; }
#Progress { flex: 1; height: 24px; -be-shape: round; }
.group    { display: flex; flex-direction: row; gap: 4px; flex: 1; height: 56px; background: rgb(30, 30, 30); }
.group button { flex: 1; height: 56px; font-size: 24px; background: rgb(70, 70, 100); }
#Ok       { height: 56px; font-size: 26px; background: rgb(40, 90, 60); -be-icon-color: #ffffff; }
@media (orientation: portrait) {
  #Plate          { height: 456px; }
  #LanguageRow    { flex-direction: column; height: 96px; }
}
)xml";

        // One form of the markup or the style each, the places where a hand-written reader is most likely to go wrong

        constexpr StringView kStyleBlockHtml = R"xml(<style>
  .card { anchor: center; width: 200px; height: 120px; background: linear-gradient(to right, #112233, #445566); border: 2px solid #ffffff; border-radius: 12px 4px; box-shadow: 0 4px 8px #00000080, 0 0 12px #ffcc00; }
  .card:hover { background: #3a6; }
  /* a comment */
  #t { color: var(--accent); font-size: 20px; text-shadow: 1px 1px #000000; -be-text-outline: 1px #000000; }
  @media (min-width: 600px) and (max-aspect-ratio: 2), (orientation: landscape) { .card { width: 50%; } }
</style>
<div class="card" id="card"><span id="t">Title</span></div>)xml";

        constexpr StringView kStyleAttributesHtml =
            R"xml(<div id="a" style="left: 10px; top: 4px; width: 100px; height: 40px; background: #fff !important"><span id="b" style="color: red; font-size: 12px;;">x</span><img id="c" src="Gem" style="width: 32px; height: 32px"></div>)xml";

        constexpr StringView kMediaCss = R"xml(.a { width: 10px; height: 10px; }
@media (orientation: portrait) { .a { width: 90%; anchor: top; } .missing { width: 1px; } }
@media (min-width: 100px) and (max-width: 200px), (min-height: 5px) { .a { height: 20px; display: none; } }
@media (orientation: square) { .a { width: 1px; } }
@media (min-width: abc) { .a { width: 1px; } }
@media { .a { width: 1px; } }
@media (orientation: portrait) { @media (orientation: landscape) { .a { width: 2px; } } }
)xml";

        constexpr StringView kEntitiesHtml = "<div id=\"e\" title=\"a &amp; b &lt; c &gt; d &quot;q&quot; &apos;s&apos; &nbsp; &unknown; &#65; &#x41; &#X41;\">&amp;&lt;&gt; &#0; &#xD800; &#x10FFFF; "
                                             "&#x110000; &#4294967295; &#99999999999; &#; &#x; &# ; &amp</div>";

        constexpr StringView kUnclosedHtml = "<div id=\"a\"><span id=\"b\">text</div><p id=\"c\"";
        constexpr StringView kStrayEndTagHtml = "</div></span><div id=\"a\"></p></div></div>";
        constexpr StringView kBrokenMarkupHtml = "<!DOCTYPE html><?xml version=\"1\"?><!-- never closed <div id=a <b =x /> <1> < > <div id=\"\" class=\"a b\" hidden data-x='y'>";
        constexpr StringView kUnknownTagsHtml = "<table><tr><td id=\"x\">cell</td></tr></table><custom-tag id=\"y\">z</custom-tag><svg><path d=\"M0 0\"/></svg><script>var a = '<div>';</script>";
        constexpr StringView kTokenHtml = R"xml(<div id="a"></div><div id="b"></div>)xml";
        constexpr StringView kTokenCss = R"xml(#a { background: var(--panel); border: 1px solid var(--line); box-shadow: 0 2px 4px var(--shade); }
#b { background: var(--bad name); color: var(--a, red); --own: 1px; }
)xml";
        constexpr StringView kUnsupportedCss =
            R"xml(a[href] { color: red; } div + p { x: y } div ~ p { x: y } p::before { x: y } p:first-child { x: y } @import url(a.css); @font-face { src: url(a); }
@keyframes k { from { left: 0 } to { left: 9px } }
div { margin: 1em; width: calc(100% - 4px); opacity: .5; transform: rotate(3deg); width: 5vw; z-index: 3; }
)xml";

        /// Nested groups, each with an id: the depth a document of a screen does not reach
        String NestedHtml(size_t depth) {
            String html;
            for (size_t i = 0; i < depth; ++i) {
                html += Format("<div id=\"n{}\" class=\"n\">", i);
            }
            html += "<span id=\"leaf\">leaf</span>";
            for (size_t i = 0; i < depth; ++i) {
                html += "</div>";
            }
            return html;
        }

        // The files of the string and theme tables, in the shape the games ship them

        constexpr StringView kStringsXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<UIStrings default="en">
    <language id="en">
        <string key="lang.en" text="English"/>
        <string key="lang.ru" text="Русский"/>
        <string key="menu.start" text="Start"/>
        <string key="menu.title" text="{#menu.start}: {score}"/>
        <string key="settings.title" text="Settings"/>
    </language>
    <language id="ru">
        <string key="lang.en" text="English"/>
        <string key="lang.ru" text="Русский"/>
        <string key="menu.start" text="Начать"/>
        <string key="menu.title" text="{#menu.start}: {score}"/>
        <string key="settings.title" text="Настройки"/>
    </language>
</UIStrings>)xml";

        constexpr StringView kStringsWithFaultsXml = R"xml(<UIStrings default="de">
    <language id="en">
        <string key="a" text="one"/>
        <string key="a" text="two"/>
        <string key="" text="empty key"/>
        <string key="b}c" text="brace"/>
        <string text="no key"/>
        <string key="no.text"/>
        <string key="{#a}" text="{#a} {#missing} {#} {{#a}} {#a"/>
        <other/>
    </language>
    <language id="en"><string key="a" text="third"/><string key="c" text="again"/></language>
    <language><string key="x" text="no id"/></language>
    <language id="empty"/>
    <country id="en"/>
</UIStrings>)xml";

        constexpr StringView kThemesXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<UIThemes default="light">
    <theme id="light">
        <color name="panel" value="#f0f0e6"/>
        <color name="line" value="#0a141e"/>
        <color name="accent" value="rgb(40, 90, 60)"/>
        <color name="shade" value="#00000078"/>
    </theme>
    <theme id="dark">
        <color name="panel" value="#1e2028"/>
        <color name="accent" value="rgba(200, 180, 40, 1)"/>
        <color name="shade" value="#000000dc"/>
    </theme>
</UIThemes>)xml";

        constexpr StringView kThemesWithFaultsXml = R"xml(<UIThemes default="missing">
    <theme id="a">
        <color name="x" value="#fff"/>
        <color name="x" value="#000"/>
        <color name="1bad" value="#fff"/>
        <color name="has space" value="#fff"/>
        <color name="" value="#fff"/>
        <color value="#fff"/>
        <color name="novalue"/>
        <color name="notcolor" value="blue-ish"/>
        <colour name="y" value="#fff"/>
    </theme>
    <theme id="b"><color name="x" value="#123"/><color name="only-b" value="#456"/></theme>
    <theme/>
    <theme id="empty"/>
    <palette id="p"/>
</UIThemes>)xml";

        // =============================================================================
        // ui_geometry: the numbers of the boxes, the nine-slice panels and the layouts of a screen of the games (a 1280 x 720 canvas, a 220 x 48 button, a stack of rows), then the
        // same rounds with every number replaced by an extreme
        // =============================================================================

        using namespace UiGeometryInput;

        SpecCase PinnedSpec(UIAnchor anchor, float x, float y, float width, float height) {
            SpecCase spec;
            const auto& anchors = EnumUtils<UIAnchor>::Values();
            for (size_t i = 0; i < anchors.size(); ++i) {
                if (anchors[i] == anchor) {
                    spec.anchor = static_cast<uint8_t>(i);
                }
            }
            spec.x = UILength::Px(x);
            spec.y = UILength::Px(y);
            spec.width = UILength::Px(width);
            spec.height = UILength::Px(height);
            return spec;
        }

        /// A stretched element with a margin, limits and an aspect ratio: every optional field of a spec set to a number a screen could use
        SpecCase StretchedSpec() {
            SpecCase spec = PinnedSpec(UIAnchor::Stretch, 0.0f, 0.0f, 0.0f, 0.0f);
            spec.optionals = 0x7F;
            spec.anchorMin = {0.1f, 0.0f};
            spec.anchorMax = {0.9f, 0.5f};
            spec.pivot = {0.5f, 0.5f};
            spec.minWidth = UILength::Px(100.0f);
            spec.maxWidth = UILength::Percent(80.0f);
            spec.minHeight = UILength::Px(40.0f);
            spec.maxHeight = UILength::Percent(50.0f);
            spec.unitBits = 0xB0;
            spec.aspectRatio = 1.5f;
            spec.margin = {8.0f, 8.0f, 8.0f, 8.0f};
            return spec;
        }

        BoxCase ButtonBox() {
            BoxCase box;
            box.rect = {16.0f, 16.0f, 220.0f, 48.0f};
            box.corners = UICorners::Uniform(12.0f);
            box.borderWidth = 2.0f;
            box.color = Color{40, 90, 160, 255};
            box.gradientEnd = Color{20, 50, 110, 255};
            box.gradientDirection = 1;
            box.shadow = UIShadowSpec{0.0f, 4.0f, 8.0f, 0.0f, Color{0, 0, 0, 120}};
            box.pixelsPerUnit = 1.5f;
            box.radiusPixels = 18.0f;
            box.inset = 2.0f;
            return box;
        }

        StackCase RowsStack() {
            StackCase stack;
            stack.parent = {0.0f, 0.0f, 320.0f, 480.0f};
            stack.spacing = 8.0f;
            stack.padding = {12.0f, 12.0f, 12.0f, 12.0f};
            stack.childCount = 4;
            stack.children[0] = PinnedSpec(UIAnchor::TopLeft, 0.0f, 0.0f, 0.0f, 40.0f);
            stack.children[1] = PinnedSpec(UIAnchor::TopLeft, 0.0f, 0.0f, 200.0f, 48.0f);
            stack.children[2] = PinnedSpec(UIAnchor::TopLeft, 0.0f, 0.0f, 0.0f, 0.0f);
            stack.children[2].flex = 1.0f;
            stack.children[3] = PinnedSpec(UIAnchor::TopLeft, 0.0f, 0.0f, 120.0f, 0.0f);
            stack.children[3].aspectRatio = 2.0f;
            return stack;
        }

        CanvasCase HudCanvas() {
            CanvasCase canvas;
            canvas.flags = 0x01;
            canvas.referenceWidth = 1280.0f;
            canvas.referenceHeight = 720.0f;
            canvas.match = 0.5f;
            canvas.minScale = 0.25f;
            canvas.maxScale = 8.0f;
            canvas.context.outputWidth = 1280.0f;
            canvas.context.outputHeight = 720.0f;
            canvas.context.safeArea = {0.0f, 44.0f, 1280.0f, 676.0f};
            canvas.context.sceneViewWidth = 1280.0f;
            canvas.context.sceneViewHeight = 720.0f;
            canvas.context.worldBox = {100.0f, 100.0f, 400.0f, 300.0f};
            canvas.childCount = 3;
            canvas.grandChildCount = 3;
            canvas.stackBits = 0x01;
            canvas.children[0] = PinnedSpec(UIAnchor::TopLeft, 16.0f, 16.0f, 220.0f, 96.0f);
            canvas.children[1] = PinnedSpec(UIAnchor::BottomRight, -16.0f, -16.0f, 96.0f, 48.0f);
            canvas.children[2] = StretchedSpec();
            canvas.grandChildren[0] = PinnedSpec(UIAnchor::TopLeft, 12.0f, 10.0f, 120.0f, 28.0f);
            canvas.grandChildren[1] = PinnedSpec(UIAnchor::TopLeft, 0.0f, 0.0f, 100.0f, 24.0f);
            canvas.grandChildren[2] = PinnedSpec(UIAnchor::Center, 0.0f, 0.0f, 50.0f, 50.0f);
            canvas.stack = RowsStack();
            canvas.pointX = 40.0f;
            canvas.pointY = 40.0f;
            return canvas;
        }

        RoundCase ScreenRound() {
            RoundCase round;
            round.box = ButtonBox();
            round.slice = {{0.0f, 0.0f, 64.0f, 64.0f}, {16.0f, 16.0f, 16.0f, 16.0f}, {0.0f, 0.0f, 220.0f, 48.0f}};
            round.parent = {0.0f, 0.0f, 1280.0f, 720.0f};
            round.spec = StretchedSpec();
            round.stack = RowsStack();
            round.canvas = HudCanvas();
            return round;
        }

        /// A stream that replaces every float field it passes by one value
        struct FloatFiller {
            float value;

            void Float(float& field) const {
                field = value;
            }
            void Byte(uint8_t&) const {}
        };

        /// A stream that gives the float fields the values in turn, so a round has several extremes at once
        struct FloatCycler {
            const float* values;
            size_t count;
            size_t next = 0;

            void Float(float& field) {
                field = values[next++ % count];
            }
            void Byte(uint8_t&) const {}
        };

        FuzzBytes WriteRounds(std::initializer_list<RoundCase> rounds) {
            FuzzBytes bytes;
            Writer writer(bytes);
            for (RoundCase round : rounds) {
                Transfer(writer, round);
            }
            return bytes;
        }

        void AddGeometrySeeds(Vector<FuzzSeedFile>& seeds) {
            const RoundCase screen = ScreenRound();
            seeds.push_back({kUiGeometryFolder, "screen.bin", WriteRounds({screen})});

            RoundCase small = screen;
            small.box.rect = {0.0f, 0.0f, 6.0f, 4.0f};
            small.box.corners = UICorners::Uniform(40.0f);
            small.box.borderWidth = 30.0f;
            small.box.shadow.blur = 32.0f;
            small.box.pixelsPerUnit = 4.0f;
            small.slice.destination = {0.0f, 0.0f, 20.0f, 10.0f};
            small.canvas.context.outputWidth = 320.0f;
            small.canvas.context.outputHeight = 568.0f;
            small.canvas.space = 1;
            small.canvas.flags = 0x02;
            seeds.push_back({kUiGeometryFolder, "small-box.bin", WriteRounds({small, screen})});

            RoundCase percent = screen;
            percent.spec.unitBits = 0xFF;
            percent.spec.x = UILength::Percent(25.0f);
            percent.spec.width = UILength::Percent(50.0f);
            percent.stack.wrap = 1;
            percent.stack.align = 3;
            percent.canvas.scaleMode = 2;
            percent.canvas.space = 2;
            percent.canvas.stackBits = 0x03;
            seeds.push_back({kUiGeometryFolder, "percent-wrap-stretch.bin", WriteRounds({percent})});

            // The extremes, one kind for every number of a round, and all of them in turn
            constexpr float kInfinity = std::numeric_limits<float>::infinity();
            constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
            constexpr float kLargest = std::numeric_limits<float>::max();
            constexpr float kSmallest = std::numeric_limits<float>::denorm_min();
            constexpr float kJustOverTheBound = 1.0e8f;
            const struct {
                const char* name;
                float value;
            } extremes[] = {{"nan", kNan},
                            {"infinity", kInfinity},
                            {"minus-infinity", -kInfinity},
                            {"largest", kLargest},
                            {"minus-largest", -kLargest},
                            {"denormal", kSmallest},
                            {"over-the-bound", kJustOverTheBound},
                            {"minus-denormal", -kSmallest}};
            for (const auto& extreme : extremes) {
                RoundCase round = screen;
                FloatFiller filler{extreme.value};
                Transfer(filler, round);
                seeds.push_back({kUiGeometryFolder, Format("every-number-{}.bin", extreme.name).c_str(), WriteRounds({round})});
            }
            constexpr float kMixed[] = {kNan, kInfinity, kLargest, -kLargest, kSmallest, 0.0f, -1.0f, 1.0f, kJustOverTheBound, -kInfinity, 1.0e-3f, 1.0e7f};
            RoundCase mixed = screen;
            FloatCycler cycler{kMixed, std::size(kMixed)};
            Transfer(cycler, mixed);
            RoundCase shifted = screen;
            FloatCycler shiftedCycler{kMixed, std::size(kMixed), 5};
            Transfer(shiftedCycler, shifted);
            seeds.push_back({kUiGeometryFolder, "mixed-extremes.bin", WriteRounds({mixed, shifted})});
        }

    }  // namespace

    void AddUiSeeds(Vector<FuzzSeedFile>& seeds) {
        seeds.push_back({kWebUiFolder, "hud-with-link.bin", HtmlAndCss(kHudHtml, kHudCss)});
        seeds.push_back({kWebUiFolder, "menu.bin", HtmlAndCss(kMenuHtml, kMenuCss)});
        seeds.push_back({kWebUiFolder, "match3-hud.bin", HtmlAndCss(kMatch3HudHtml, kMatch3HudCss)});
        seeds.push_back({kWebUiFolder, "settings-widgets.bin", HtmlAndCss(kSettingsHtml, kSettingsCss)});
        seeds.push_back({kWebUiFolder, "style-block.bin", HtmlAndCss(kStyleBlockHtml, {})});
        seeds.push_back({kWebUiFolder, "style-attributes.bin", HtmlAndCss(kStyleAttributesHtml, {})});
        seeds.push_back({kWebUiFolder, "media-rules.bin", HtmlAndCss("<div id=\"a\" class=\"a\"></div>", kMediaCss)});
        seeds.push_back({kWebUiFolder, "entities.bin", HtmlAndCss(kEntitiesHtml, {})});
        seeds.push_back({kWebUiFolder, "unclosed-tag.bin", HtmlAndCss(kUnclosedHtml, {})});
        seeds.push_back({kWebUiFolder, "stray-end-tags.bin", HtmlAndCss(kStrayEndTagHtml, {})});
        seeds.push_back({kWebUiFolder, "broken-markup.bin", HtmlAndCss(kBrokenMarkupHtml, {})});
        seeds.push_back({kWebUiFolder, "unknown-tags.bin", HtmlAndCss(kUnknownTagsHtml, {})});
        seeds.push_back({kWebUiFolder, "theme-tokens.bin", HtmlAndCss(kTokenHtml, kTokenCss)});
        seeds.push_back({kWebUiFolder, "outside-the-subset.bin", HtmlAndCss("<div id=\"a\"><p>text</p></div>", kUnsupportedCss)});

        const String nested = NestedHtml(kNestingDepth);
        seeds.push_back({kWebUiFolder, "deep-nesting.bin", HtmlAndCss(StringView(nested), ".n { padding: 1px; display: flex; }")});

        const String longValue(kLongTextBytes, 'a');
        const String longAttribute = Format("<div id=\"a\" title=\"{}\" class=\"{}\" data-x={}></div>", longValue.c_str(), longValue.c_str(), longValue.c_str());
        seeds.push_back({kWebUiFolder, "long-attributes.bin", HtmlAndCss(StringView(longAttribute), {})});
        const String longSelector = Format(".{} {{ width: 1px }} #{} .{}:hover {{ color: red }}", longValue.c_str(), longValue.c_str(), longValue.c_str());
        seeds.push_back({kWebUiFolder, "long-selectors.bin", HtmlAndCss("<div id=\"a\" class=\"a\"></div>", StringView(longSelector))});

        seeds.push_back({kUiTablesFolder, "strings.xml", TextBytes(kStringsXml)});
        seeds.push_back({kUiTablesFolder, "strings-with-faults.xml", TextBytes(kStringsWithFaultsXml)});
        seeds.push_back({kUiTablesFolder, "themes.xml", TextBytes(kThemesXml)});
        seeds.push_back({kUiTablesFolder, "themes-with-faults.xml", TextBytes(kThemesWithFaultsXml)});

        AddGeometrySeeds(seeds);
    }

}  // namespace BECore::Fuzz
