#include "FuzzSeeds.h"

namespace BECore::Fuzz {

    namespace {

        constexpr const char* kWebUiFolder = "web_ui";
        constexpr const char* kUiTablesFolder = "ui_tables";

        /// How deep the nesting seed goes: far more than a screen has, and far less than the limit of the compiler
        constexpr size_t kNestingDepth = 64;

        /// The length of the long attribute and the long selector seeds
        constexpr size_t kLongTextBytes = 4096;

        FuzzBytes TextBytes(eastl::string_view text) {
            const auto* begin = reinterpret_cast<const uint8_t*>(text.data());
            return FuzzBytes(begin, begin + text.size());
        }

        /// The input of FuzzWebUi: the html, a NUL byte, the style sheet
        FuzzBytes HtmlAndCss(eastl::string_view html, eastl::string_view css) {
            FuzzBytes bytes = TextBytes(html);
            bytes.push_back(0);
            const FuzzBytes cssBytes = TextBytes(css);
            bytes.insert(bytes.end(), cssBytes.begin(), cssBytes.end());
            return bytes;
        }

        // The screens of tests/BECore/UIWebTest.cpp (the HUD of the plan's sketch, the Match3 menu and HUD, the settings panel), as a game writes them

        constexpr eastl::string_view kHudHtml = R"xml(<link rel="stylesheet" href="hud.css">
<div id="Hud" class="hud">
  <span id="Score">Score: {score}</span>
  <button id="Reset" data-action="Match3Reset">Reset</button>
</div>)xml";

        constexpr eastl::string_view kHudCss = R"xml(.hud    { anchor: top-left; left: 16px; top: 16px; width: 220px; height: 96px; background: #000000A0; }
#Score  { left: 12px; top: 10px; width: 150px; height: 32px; font-size: 28px; }
#Reset  { anchor: bottom-right; right: 12px; bottom: 10px; width: 96px; height: 48px; font-size: 24px; }
button  { background: rgb(40, 90, 60); }
button:hover { background: #3a6; }
@media (orientation: portrait) { .hud { anchor: top; left: 0; width: 90%; } }
)xml";

        constexpr eastl::string_view kMenuHtml = R"xml(<div id="Column">
  <h1 id="Title">Match 3</h1>
  <button id="Play" data-action="Match3Play">Play</button>
  <button id="Quit" data-action="Match3Quit">Quit</button>
</div>)xml";

        constexpr eastl::string_view kMenuCss = R"xml(#Column { anchor: center; display: flex; flex-direction: column; align-items: stretch; gap: 16px; width: 90%; max-width: 320px; height: 232px; }
#Title  { height: 88px; font-size: 56px; text-align: center; vertical-align: middle; }
button  { height: 56px; font-size: 28px; }
#Play   { background: rgb(40, 90, 60); }
#Quit   { background: rgb(90, 50, 50); }
)xml";

        constexpr eastl::string_view kMatch3HudHtml = R"xml(<span id="Score">Score: {score}</span>
<div id="Buttons">
  <button id="Reset" data-action="Match3Reset">Reset</button>
  <button id="Menu" data-action="Match3Menu">Menu</button>
</div>
<span id="Seed">seed {seed}</span>)xml";

        constexpr eastl::string_view kMatch3HudCss = R"xml(#Score   { left: 96px; top: 12px; width: 250px; height: 40px; font-size: 28px; vertical-align: middle; }
#Buttons { anchor: top-right; right: 96px; top: 12px; width: 260px; height: 48px; display: flex; gap: 12px; align-items: stretch; }
button   { width: 124px; height: 48px; font-size: 24px; }
#Reset   { background: rgb(40, 90, 60); }
#Menu    { background: rgb(70, 70, 100); }
#Seed    { left: 96px; bottom: 26px; width: 300px; height: 24px; font-size: 16px; color: rgb(190, 190, 190); }
)xml";

        constexpr eastl::string_view kSettingsHtml = R"xml(<div id="Plate">
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

        constexpr eastl::string_view kSettingsCss =
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

        constexpr eastl::string_view kStyleBlockHtml = R"xml(<style>
  .card { anchor: center; width: 200px; height: 120px; background: linear-gradient(to right, #112233, #445566); border: 2px solid #ffffff; border-radius: 12px 4px; box-shadow: 0 4px 8px #00000080, 0 0 12px #ffcc00; }
  .card:hover { background: #3a6; }
  /* a comment */
  #t { color: var(--accent); font-size: 20px; text-shadow: 1px 1px #000000; -be-text-outline: 1px #000000; }
  @media (min-width: 600px) and (max-aspect-ratio: 2), (orientation: landscape) { .card { width: 50%; } }
</style>
<div class="card" id="card"><span id="t">Title</span></div>)xml";

        constexpr eastl::string_view kStyleAttributesHtml =
            R"xml(<div id="a" style="left: 10px; top: 4px; width: 100px; height: 40px; background: #fff !important"><span id="b" style="color: red; font-size: 12px;;">x</span><img id="c" src="Gem" style="width: 32px; height: 32px"></div>)xml";

        constexpr eastl::string_view kMediaCss = R"xml(.a { width: 10px; height: 10px; }
@media (orientation: portrait) { .a { width: 90%; anchor: top; } .missing { width: 1px; } }
@media (min-width: 100px) and (max-width: 200px), (min-height: 5px) { .a { height: 20px; display: none; } }
@media (orientation: square) { .a { width: 1px; } }
@media (min-width: abc) { .a { width: 1px; } }
@media { .a { width: 1px; } }
@media (orientation: portrait) { @media (orientation: landscape) { .a { width: 2px; } } }
)xml";

        constexpr eastl::string_view kEntitiesHtml =
            "<div id=\"e\" title=\"a &amp; b &lt; c &gt; d &quot;q&quot; &apos;s&apos; &nbsp; &unknown; &#65; &#x41; &#X41;\">&amp;&lt;&gt; &#0; &#xD800; &#x10FFFF; "
            "&#x110000; &#4294967295; &#99999999999; &#; &#x; &# ; &amp</div>";

        constexpr eastl::string_view kUnclosedHtml = "<div id=\"a\"><span id=\"b\">text</div><p id=\"c\"";
        constexpr eastl::string_view kStrayEndTagHtml = "</div></span><div id=\"a\"></p></div></div>";
        constexpr eastl::string_view kBrokenMarkupHtml = "<!DOCTYPE html><?xml version=\"1\"?><!-- never closed <div id=a <b =x /> <1> < > <div id=\"\" class=\"a b\" hidden data-x='y'>";
        constexpr eastl::string_view kUnknownTagsHtml =
            "<table><tr><td id=\"x\">cell</td></tr></table><custom-tag id=\"y\">z</custom-tag><svg><path d=\"M0 0\"/></svg><script>var a = '<div>';</script>";
        constexpr eastl::string_view kTokenHtml = R"xml(<div id="a"></div><div id="b"></div>)xml";
        constexpr eastl::string_view kTokenCss = R"xml(#a { background: var(--panel); border: 1px solid var(--line); box-shadow: 0 2px 4px var(--shade); }
#b { background: var(--bad name); color: var(--a, red); --own: 1px; }
)xml";
        constexpr eastl::string_view kUnsupportedCss =
            R"xml(a[href] { color: red; } div + p { x: y } div ~ p { x: y } p::before { x: y } p:first-child { x: y } @import url(a.css); @font-face { src: url(a); }
@keyframes k { from { left: 0 } to { left: 9px } }
div { margin: 1em; width: calc(100% - 4px); opacity: .5; transform: rotate(3deg); width: 5vw; z-index: 3; }
)xml";

        /// Nested groups, each with an id: the depth a document of a screen does not reach
        eastl::string NestedHtml(size_t depth) {
            eastl::string html;
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

        constexpr eastl::string_view kStringsXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
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

        constexpr eastl::string_view kStringsWithFaultsXml = R"xml(<UIStrings default="de">
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

        constexpr eastl::string_view kThemesXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
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

        constexpr eastl::string_view kThemesWithFaultsXml = R"xml(<UIThemes default="missing">
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

    }  // namespace

    void AddUiSeeds(eastl::vector<FuzzSeedFile>& seeds) {
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

        const eastl::string nested = NestedHtml(kNestingDepth);
        seeds.push_back({kWebUiFolder, "deep-nesting.bin", HtmlAndCss(eastl::string_view(nested.data(), nested.size()), ".n { padding: 1px; display: flex; }")});

        const eastl::string longValue(kLongTextBytes, 'a');
        const eastl::string longAttribute = Format("<div id=\"a\" title=\"{}\" class=\"{}\" data-x={}></div>", longValue.c_str(), longValue.c_str(), longValue.c_str());
        seeds.push_back({kWebUiFolder, "long-attributes.bin", HtmlAndCss(eastl::string_view(longAttribute.data(), longAttribute.size()), {})});
        const eastl::string longSelector = Format(".{} {{ width: 1px }} #{} .{}:hover {{ color: red }}", longValue.c_str(), longValue.c_str(), longValue.c_str());
        seeds.push_back({kWebUiFolder, "long-selectors.bin", HtmlAndCss("<div id=\"a\" class=\"a\"></div>", eastl::string_view(longSelector.data(), longSelector.size()))});

        seeds.push_back({kUiTablesFolder, "strings.xml", TextBytes(kStringsXml)});
        seeds.push_back({kUiTablesFolder, "strings-with-faults.xml", TextBytes(kStringsWithFaultsXml)});
        seeds.push_back({kUiTablesFolder, "themes.xml", TextBytes(kThemesXml)});
        seeds.push_back({kUiTablesFolder, "themes-with-faults.xml", TextBytes(kThemesWithFaultsXml)});
    }

}  // namespace BECore::Fuzz
