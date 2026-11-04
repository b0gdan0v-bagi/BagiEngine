#include "UIWebCompiler.h"

#include <BECore/UI/UICssParser.h>
#include <BECore/UI/UIHtmlParser.h>
#include <BECore/UI/UIValueText.h>
#include <BECore/UI/UIWebText.h>
#include <EASTL/hash_set.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view HtmlSource = "html";
        constexpr eastl::string_view CssSource = "css";
        constexpr eastl::string_view WidgetAttribute = "data-widget";
        constexpr eastl::string_view SegmentedWidget = "segmented";
        constexpr char LineBreakMarker = '\x01';  // a `<br>` while the text of a label is gathered; the white space of the source is never this
        constexpr float DefaultFontSizeForLineHeight = 24.0f;
        constexpr size_t BoxValueMax = 4;

        // The kinds of element the compile writes
        CORE_ENUM(WebKind, uint8_t, Group, Panel, Label, Button, Image, Segmented)

        const char* TypeName(WebKind kind) {
            switch (kind) {
                case WebKind::Panel:
                    return "UIPanel";
                case WebKind::Label:
                    return "UILabel";
                case WebKind::Button:
                    return "UIButton";
                case WebKind::Image:
                    return "UIImage";
                case WebKind::Segmented:
                    return "UISegmented";
                case WebKind::Group:
                    break;
            }
            return "UIElement";
        }

        bool IsTextKind(WebKind kind) {
            return kind == WebKind::Label || kind == WebKind::Button;
        }

        bool IsOneOf(eastl::string_view tag, std::initializer_list<eastl::string_view> names) {
            return std::find(names.begin(), names.end(), tag) != names.end();
        }

        bool IsIgnoredTag(eastl::string_view tag) {
            return IsOneOf(tag, {"head", "style", "link", "script", "meta", "title", "noscript", "template", "br"});
        }

        bool IsTransparentTag(eastl::string_view tag) {
            return IsOneOf(tag, {"html", "body"});
        }

        bool IsContainerTag(eastl::string_view tag) {
            return IsOneOf(tag, {"div", "section", "article", "nav", "header", "footer", "main", "aside", "ul", "ol", "li", "form", "fieldset", "figure"});
        }

        bool IsLabelTag(eastl::string_view tag) {
            return IsOneOf(tag, {"span", "p", "h1", "h2", "h3", "h4", "h5", "h6", "label", "strong", "b", "i", "em", "small", "a", "code"});
        }

        /// A tag that may sit inside the text of a label
        bool IsInlineTag(eastl::string_view tag) {
            return IsOneOf(tag, {"span", "b", "i", "u", "em", "strong", "small", "a", "code", "mark", "sub", "sup", "br", "label"});
        }

        /// The properties CSS inherits that the compile follows
        bool IsInherited(eastl::string_view property) {
            return IsOneOf(property, {"color", "font-size", "font-family", "text-align", "line-height", "white-space"});
        }

        // ---- the values of the properties --------------------------------------------------------------------------------

        /// What the declarations of one element come to, last one winning. Every field is "not set" until a declaration sets it
        struct Resolved {
            bool displayNone = false;
            bool visibilityHidden = false;
            bool flex = false;
            std::optional<float> left;
            std::optional<float> right;
            std::optional<float> top;
            std::optional<float> bottom;
            eastl::string anchor;
            std::optional<UILength> width;
            std::optional<UILength> height;
            std::optional<UILength> minWidth;
            std::optional<UILength> maxWidth;
            std::optional<UILength> minHeight;
            std::optional<UILength> maxHeight;
            std::optional<float> aspectRatio;
            eastl::string objectFit;
            std::optional<UIEdges> margin;
            std::optional<UIEdges> padding;
            std::optional<float> gap;
            std::optional<float> flexGrow;
            eastl::string flexDirection;
            std::optional<bool> wrapChildren;
            eastl::string alignItems;
            std::optional<Color> background;
            eastl::string sprite;
            std::optional<UIEdges> slice;
            std::optional<bool> pointerEvents;
            std::optional<Color> textColor;
            std::optional<float> fontSize;
            eastl::string fontPath;
            eastl::string textAlign;
            eastl::string verticalAlign;
            eastl::string textOverflow;
            std::optional<bool> textFit;
            std::optional<float> minFontSize;
            std::optional<bool> wrapText;
            std::optional<float> lineHeightRatio;
            std::optional<float> lineHeightPixels;
        };

        bool ParseOffset(eastl::string_view value, std::optional<float>& out) {
            if (UIWebText::ToLower(value) == "auto") {
                out.reset();
                return true;
            }
            const auto pixels = UIWebText::ParsePixels(value);
            if (!pixels) {
                return false;
            }
            out = *pixels;
            return true;
        }

        bool ParseSize(eastl::string_view value, std::optional<UILength>& out) {
            const eastl::string lower = UIWebText::ToLower(value);
            if (lower == "auto" || lower == "none") {
                out.reset();
                return true;
            }
            const auto length = UIText::ParseLength(value);
            if (!length) {
                return false;
            }
            out = *length;
            return true;
        }

        /// The one to four values of a CSS box shorthand (top right bottom left) as edges
        std::optional<UIEdges> ParseBox(eastl::string_view value) {
            const auto words = UIWebText::SplitWords(value);
            if (words.empty() || words.size() > BoxValueMax) {
                return std::nullopt;
            }
            float numbers[BoxValueMax] = {};
            for (size_t i = 0; i < words.size(); ++i) {
                const auto pixels = UIWebText::ParsePixels(words[i]);
                if (!pixels) {
                    return std::nullopt;
                }
                numbers[i] = *pixels;
            }
            float top = numbers[0];
            float right = words.size() > 1 ? numbers[1] : top;
            float bottom = words.size() > 2 ? numbers[2] : top;
            float left = words.size() > 3 ? numbers[3] : right;
            return UIEdges{left, top, right, bottom};
        }

        /// "top-left" to "TopLeft"; empty when it is not an anchor name
        eastl::string AnchorName(eastl::string_view value) {
            static const std::pair<eastl::string_view, const char*> Names[] = {
                {"top-left", "TopLeft"},       {"top", "Top"},       {"top-right", "TopRight"},       {"left", "Left"},      {"center", "Center"}, {"right", "Right"},
                {"bottom-left", "BottomLeft"}, {"bottom", "Bottom"}, {"bottom-right", "BottomRight"}, {"stretch", "Stretch"}};
            const eastl::string lower = UIWebText::ToLower(value);
            for (const auto& [css, name] : Names) {
                if (lower == css) {
                    return name;
                }
            }
            return {};
        }

        /// `flex`: the first number is the grow factor (`1`, `1 1 0`, `2 0 100px`); `auto` is 1, `none` is 0
        bool ParseFlexGrow(eastl::string_view value, std::optional<float>& out) {
            const eastl::string lower = UIWebText::ToLower(value);
            if (lower == "none") {
                out = 0.0f;
                return true;
            }
            if (lower == "auto") {
                out = 1.0f;
                return true;
            }
            const auto words = UIWebText::SplitWords(value);
            if (words.empty()) {
                return false;
            }
            const auto grow = UIText::ParseFloat(words[0]);
            if (!grow) {
                return false;
            }
            out = *grow;
            return true;
        }

        enum class ApplyResult { Applied, Ignored, BadValue, UnknownProperty };

        using Handler = bool (*)(Resolved&, eastl::string_view);

        struct PropertyHandler {
            eastl::string_view name;
            Handler handler;
        };

        // A handler changes the Resolved only when it understands the value
        const PropertyHandler PropertyHandlers[] = {
            {"display",
             [](Resolved& r, eastl::string_view v) {
                 const eastl::string lower = UIWebText::ToLower(v);
                 if (lower == "none") {
                     r.displayNone = true;
                 } else if (lower == "flex" || lower == "inline-flex") {
                     r.displayNone = false;
                     r.flex = true;
                 } else if (IsOneOf(lower, {"block", "inline", "inline-block", "flow-root", "contents"})) {
                     r.displayNone = false;
                     r.flex = false;
                 } else {
                     return false;
                 }
                 return true;
             }},
            {"visibility",
             [](Resolved& r, eastl::string_view v) {
                 const eastl::string lower = UIWebText::ToLower(v);
                 if (lower != "visible" && lower != "hidden") {
                     return false;
                 }
                 r.visibilityHidden = lower == "hidden";
                 return true;
             }},
            {"left", [](Resolved& r, eastl::string_view v) { return ParseOffset(v, r.left); }},
            {"right", [](Resolved& r, eastl::string_view v) { return ParseOffset(v, r.right); }},
            {"top", [](Resolved& r, eastl::string_view v) { return ParseOffset(v, r.top); }},
            {"bottom", [](Resolved& r, eastl::string_view v) { return ParseOffset(v, r.bottom); }},
            {"inset",
             [](Resolved& r, eastl::string_view v) {
                 const auto box = ParseBox(v);
                 if (!box) {
                     return false;
                 }
                 r.left = box->left;
                 r.top = box->top;
                 r.right = box->right;
                 r.bottom = box->bottom;
                 return true;
             }},
            {"anchor",
             [](Resolved& r, eastl::string_view v) {
                 eastl::string name = AnchorName(v);
                 if (name.empty()) {
                     return false;
                 }
                 r.anchor = std::move(name);
                 return true;
             }},
            {"width", [](Resolved& r, eastl::string_view v) { return ParseSize(v, r.width); }},
            {"height", [](Resolved& r, eastl::string_view v) { return ParseSize(v, r.height); }},
            {"min-width", [](Resolved& r, eastl::string_view v) { return ParseSize(v, r.minWidth); }},
            {"max-width", [](Resolved& r, eastl::string_view v) { return ParseSize(v, r.maxWidth); }},
            {"min-height", [](Resolved& r, eastl::string_view v) { return ParseSize(v, r.minHeight); }},
            {"max-height", [](Resolved& r, eastl::string_view v) { return ParseSize(v, r.maxHeight); }},
            {"aspect-ratio",
             [](Resolved& r, eastl::string_view v) {
                 if (UIWebText::ToLower(v) == "auto") {
                     r.aspectRatio.reset();
                     return true;
                 }
                 const size_t slash = v.find('/');
                 const auto numerator = UIText::ParseFloat(slash == eastl::string_view::npos ? v : v.substr(0, slash));
                 const auto denominator = slash == eastl::string_view::npos ? std::optional<float>{1.0f} : UIText::ParseFloat(v.substr(slash + 1));
                 if (!numerator || !denominator || *denominator <= 0.0f || *numerator <= 0.0f) {
                     return false;
                 }
                 r.aspectRatio = *numerator / *denominator;
                 return true;
             }},
            {"object-fit",
             [](Resolved& r, eastl::string_view v) {
                 const eastl::string lower = UIWebText::ToLower(v);
                 if (!IsOneOf(lower, {"contain", "cover", "fill", "none", "scale-down"})) {
                     return false;
                 }
                 r.objectFit = lower;
                 return true;
             }},
            {"margin",
             [](Resolved& r, eastl::string_view v) {
                 const auto box = ParseBox(v);
                 if (!box) {
                     return false;
                 }
                 r.margin = *box;
                 return true;
             }},
            {"padding",
             [](Resolved& r, eastl::string_view v) {
                 const auto box = ParseBox(v);
                 if (!box) {
                     return false;
                 }
                 r.padding = *box;
                 return true;
             }},
            {"gap",
             [](Resolved& r, eastl::string_view v) {
                 // `gap: row column`: a stack has one spacing, so the two must agree
                 const auto words = UIWebText::SplitWords(v);
                 if (words.empty() || words.size() > 2) {
                     return false;
                 }
                 const auto first = UIWebText::ParsePixels(words[0]);
                 const auto second = words.size() == 2 ? UIWebText::ParsePixels(words[1]) : first;
                 if (!first || !second || *first != *second) {
                     return false;
                 }
                 r.gap = *first;
                 return true;
             }},
            {"row-gap",
             [](Resolved& r, eastl::string_view v) {
                 const auto pixels = UIWebText::ParsePixels(v);
                 if (pixels) {
                     r.gap = *pixels;
                 }
                 return pixels.has_value();
             }},
            {"column-gap",
             [](Resolved& r, eastl::string_view v) {
                 const auto pixels = UIWebText::ParsePixels(v);
                 if (pixels) {
                     r.gap = *pixels;
                 }
                 return pixels.has_value();
             }},
            {"flex", [](Resolved& r, eastl::string_view v) { return ParseFlexGrow(v, r.flexGrow); }},
            {"flex-grow", [](Resolved& r, eastl::string_view v) { return ParseFlexGrow(v, r.flexGrow); }},
            {"flex-direction",
             [](Resolved& r, eastl::string_view v) {
                 const eastl::string lower = UIWebText::ToLower(v);
                 if (lower == "row") {
                     r.flexDirection = "Horizontal";
                 } else if (lower == "column") {
                     r.flexDirection = "Vertical";
                 } else {
                     return false;
                 }
                 return true;
             }},
            {"flex-wrap",
             [](Resolved& r, eastl::string_view v) {
                 const eastl::string lower = UIWebText::ToLower(v);
                 if (lower != "wrap" && lower != "nowrap") {
                     return false;
                 }
                 r.wrapChildren = lower == "wrap";
                 return true;
             }},
            {"align-items",
             [](Resolved& r, eastl::string_view v) {
                 const eastl::string lower = UIWebText::ToLower(v);
                 if (lower == "flex-start" || lower == "start") {
                     r.alignItems = "Start";
                 } else if (lower == "center") {
                     r.alignItems = "Center";
                 } else if (lower == "flex-end" || lower == "end") {
                     r.alignItems = "End";
                 } else if (lower == "stretch") {
                     r.alignItems = "Stretch";
                 } else {
                     return false;
                 }
                 return true;
             }},
            {"background-color",
             [](Resolved& r, eastl::string_view v) {
                 const auto color = UIWebText::ParseColor(v);
                 if (color) {
                     r.background = *color;
                 }
                 return color.has_value();
             }},
            {"background-image",
             [](Resolved& r, eastl::string_view v) {
                 if (UIWebText::ToLower(v) == "none") {
                     r.sprite.clear();
                     return true;
                 }
                 const auto url = UIWebText::ParseUrl(v);
                 if (url) {
                     r.sprite = *url;
                 }
                 return url.has_value();
             }},
            {"background",
             [](Resolved& r, eastl::string_view v) {
                 // A color and a url, in any order; the repeat and position words that mean nothing here are skipped
                 Resolved parsed;
                 for (const eastl::string_view word : UIWebText::SplitWords(v)) {
                     const eastl::string lower = UIWebText::ToLower(word);
                     if (lower == "none" || lower == "no-repeat" || lower == "center") {
                         continue;
                     }
                     if (const auto url = UIWebText::ParseUrl(word)) {
                         parsed.sprite = *url;
                     } else if (const auto color = UIWebText::ParseColor(word)) {
                         parsed.background = *color;
                     } else {
                         return false;
                     }
                 }
                 // The shorthand resets what it does not name
                 r.background = parsed.background ? parsed.background : std::optional<Color>{Color{0, 0, 0, 0}};
                 r.sprite = parsed.sprite;
                 return true;
             }},
            {"border-image-slice",
             [](Resolved& r, eastl::string_view v) {
                 // Plain numbers in pixels of the sprite (CSS says "pixels" for a bare number); `fill` is the only keyword and is the behavior of a sliced panel anyway
                 eastl::string cleaned;
                 for (const eastl::string_view word : UIWebText::SplitWords(v)) {
                     if (UIWebText::ToLower(word) == "fill") {
                         continue;
                     }
                     cleaned.append(word.data(), word.size());
                     cleaned.push_back(' ');
                 }
                 const auto box = ParseBox(cleaned);
                 if (!box) {
                     return false;
                 }
                 r.slice = *box;
                 return true;
             }},
            {"pointer-events",
             [](Resolved& r, eastl::string_view v) {
                 const eastl::string lower = UIWebText::ToLower(v);
                 if (lower == "none") {
                     r.pointerEvents = false;
                 } else if (lower == "auto" || lower == "all") {
                     r.pointerEvents = true;
                 } else {
                     return false;
                 }
                 return true;
             }},
            {"color",
             [](Resolved& r, eastl::string_view v) {
                 const auto color = UIWebText::ParseColor(v);
                 if (color) {
                     r.textColor = *color;
                 }
                 return color.has_value();
             }},
            {"font-size",
             [](Resolved& r, eastl::string_view v) {
                 const auto pixels = UIWebText::ParsePixels(v);
                 if (!pixels || *pixels <= 0.0f) {
                     return false;
                 }
                 r.fontSize = *pixels;
                 return true;
             }},
            {"font-family",
             [](Resolved& r, eastl::string_view v) {
                 // The engine has no family registry: a path to a font file is a font, a generic family is the default font, a name is not understood
                 const auto families = UIWebText::SplitTopLevel(v, ',');
                 eastl::string_view first = families.empty() ? eastl::string_view() : families[0];
                 if (first.size() >= 2 && (first.front() == '"' || first.front() == '\'') && first.back() == first.front()) {
                     first = first.substr(1, first.size() - 2);
                 }
                 if (first.empty()) {
                     return false;
                 }
                 if (first.find('/') != eastl::string_view::npos || first.find('.') != eastl::string_view::npos) {
                     r.fontPath = eastl::string(first);
                     return true;
                 }
                 if (IsOneOf(UIWebText::ToLower(first), {"sans-serif", "serif", "monospace", "system-ui", "cursive", "fantasy"})) {
                     r.fontPath.clear();
                     return true;
                 }
                 return false;
             }},
            {"text-align",
             [](Resolved& r, eastl::string_view v) {
                 const eastl::string lower = UIWebText::ToLower(v);
                 if (lower == "left" || lower == "start") {
                     r.textAlign = "Left";
                 } else if (lower == "center") {
                     r.textAlign = "Center";
                 } else if (lower == "right" || lower == "end") {
                     r.textAlign = "Right";
                 } else {
                     return false;
                 }
                 return true;
             }},
            {"vertical-align",
             [](Resolved& r, eastl::string_view v) {
                 const eastl::string lower = UIWebText::ToLower(v);
                 if (lower == "top") {
                     r.verticalAlign = "Top";
                 } else if (lower == "middle") {
                     r.verticalAlign = "Center";
                 } else if (lower == "bottom") {
                     r.verticalAlign = "Bottom";
                 } else {
                     return false;
                 }
                 return true;
             }},
            {"text-overflow",
             [](Resolved& r, eastl::string_view v) {
                 const eastl::string lower = UIWebText::ToLower(v);
                 if (lower == "ellipsis") {
                     r.textOverflow = "Ellipsis";
                 } else if (lower == "clip") {
                     r.textOverflow = "Clip";
                 } else {
                     return false;
                 }
                 return true;
             }},
            {"-be-text-fit",
             [](Resolved& r, eastl::string_view v) {
                 const eastl::string lower = UIWebText::ToLower(v);
                 if (lower != "shrink" && lower != "none") {
                     return false;
                 }
                 r.textFit = lower == "shrink";
                 return true;
             }},
            {"-be-min-font-size",
             [](Resolved& r, eastl::string_view v) {
                 const auto pixels = UIWebText::ParsePixels(v);
                 if (!pixels || *pixels <= 0.0f) {
                     return false;
                 }
                 r.minFontSize = *pixels;
                 return true;
             }},
            {"white-space",
             [](Resolved& r, eastl::string_view v) {
                 const eastl::string lower = UIWebText::ToLower(v);
                 if (lower != "normal" && lower != "nowrap") {
                     return false;
                 }
                 r.wrapText = lower == "normal";
                 return true;
             }},
            {"line-height",
             [](Resolved& r, eastl::string_view v) {
                 if (UIWebText::ToLower(v) == "normal") {
                     r.lineHeightRatio = 1.0f;
                     r.lineHeightPixels.reset();
                     return true;
                 }
                 const eastl::string_view trimmed = UIWebText::TrimSpace(v);
                 if (trimmed.size() > 2 && trimmed.substr(trimmed.size() - 2) == "px") {
                     const auto pixels = UIWebText::ParsePixels(trimmed);
                     if (!pixels || *pixels <= 0.0f) {
                         return false;
                     }
                     r.lineHeightPixels = *pixels;
                     r.lineHeightRatio.reset();
                     return true;
                 }
                 const auto ratio = UIText::ParseFloat(trimmed);
                 if (!ratio || *ratio <= 0.0f) {
                     return false;
                 }
                 r.lineHeightRatio = *ratio;
                 r.lineHeightPixels.reset();
                 return true;
             }},
        };

        // Properties that are accepted and mean nothing to a UI that is not a browser, so they are not a defect
        bool IsIgnorable(eastl::string_view property, eastl::string_view value) {
            if (property == "position") {
                return IsOneOf(UIWebText::ToLower(value), {"absolute", "relative", "static", "fixed"});
            }
            return IsOneOf(property, {"box-sizing", "cursor", "user-select", "-webkit-user-select", "-webkit-tap-highlight-color"});
        }

        ApplyResult ApplyDeclaration(Resolved& resolved, const UICssDeclaration& declaration) {
            for (const PropertyHandler& entry : PropertyHandlers) {
                if (entry.name == declaration.property) {
                    return entry.handler(resolved, declaration.value) ? ApplyResult::Applied : ApplyResult::BadValue;
                }
            }
            if (IsIgnorable(declaration.property, declaration.value)) {
                return ApplyResult::Ignored;
            }
            return IsOneOf(declaration.property, {"position", "box-sizing", "cursor", "user-select"}) ? ApplyResult::BadValue : ApplyResult::UnknownProperty;
        }

        // ---- the attributes of an element --------------------------------------------------------------------------------

        /// The attributes of one `<element>` in the order they are written; setting a key again replaces its value in place
        class Attrs {
        public:
            void Set(eastl::string_view key, eastl::string value) {
                for (auto& entry : _entries) {
                    if (entry.first == key) {
                        entry.second = std::move(value);
                        return;
                    }
                }
                _entries.emplace_back(eastl::string(key), std::move(value));
            }

            [[nodiscard]] const eastl::string* Find(eastl::string_view key) const {
                for (const auto& entry : _entries) {
                    if (entry.first == key) {
                        return &entry.second;
                    }
                }
                return nullptr;
            }

            [[nodiscard]] const eastl::vector<std::pair<eastl::string, eastl::string>>& Entries() const {
                return _entries;
            }

        private:
            eastl::vector<std::pair<eastl::string, eastl::string>> _entries;
        };

        eastl::string Number(float value) {
            return Format("{}", value + 0.0f);  // + 0 turns -0 into 0
        }

        // What a variant can change, and the value an absent attribute stands for
        struct OverridableKey {
            eastl::string_view name;
            eastl::string_view defaultValue;
        };
        constexpr OverridableKey OverridableKeys[] = {{"visible", "true"}, {"anchor", "TopLeft"},     {"x", "0"},       {"y", "0"}, {"width", "0"},
                                                      {"height", "0"},     {"direction", "Vertical"}, {"fontSize", "0"}};

        const OverridableKey* FindOverridable(eastl::string_view key) {
            for (const OverridableKey& candidate : OverridableKeys) {
                if (candidate.name == key) {
                    return &candidate;
                }
            }
            return nullptr;
        }

        // ---- the tree the compile writes ---------------------------------------------------------------------------------

        struct WebNode {
            int html = UIHtmlNode::NoNode;
            WebKind kind = WebKind::Group;
            eastl::string name;
            int parent = -1;
            eastl::vector<int> children;
            eastl::string text;
        };

        enum class CascadeState { None, Hover, Active, Disabled, Checked };

        UICssPseudo ToPseudo(CascadeState state) {
            switch (state) {
                case CascadeState::Hover:
                    return UICssPseudo::Hover;
                case CascadeState::Active:
                    return UICssPseudo::Active;
                case CascadeState::Disabled:
                    return UICssPseudo::Disabled;
                case CascadeState::Checked:
                    return UICssPseudo::Checked;
                case CascadeState::None:
                    break;
            }
            return UICssPseudo::None;
        }

        /// The declarations in effect for an element, the one that wins last
        using DeclarationList = eastl::vector<const UICssDeclaration*>;

        class Compiler {
        public:
            Compiler(eastl::string_view html, const UIWebCompiler::StylesheetLoader& loader, UIWebReport& report) : _loader(loader), _report(report) {
                _document = UIHtmlParser::Parse(html, report);
            }

            eastl::string Run() {
                CollectStyles();
                CheckDeclarations();

                const auto base = Cascade(-1, CascadeState::None);
                BuildTree(UIHtmlDocument::RootIndex, -1, base);
                _baseAttrs.reserve(_nodes.size());
                for (size_t i = 0; i < _nodes.size(); ++i) {
                    _baseAttrs.push_back(BuildAttrs(static_cast<int>(i), base, &_report));
                }

                eastl::vector<VariantOutput> variants = BuildVariants();
                ApplyStates();
                return Write(variants);
            }

        private:
            struct Override {
                int node = -1;
                eastl::vector<std::pair<eastl::string, eastl::string>> values;
            };

            struct VariantOutput {
                UIVariantCondition when;
                eastl::vector<Override> overrides;
            };

            // ---- reading the style sheets ----

            void CollectStyles() {
                _inlineStyles.resize(_document.nodes.size());
                for (size_t i = 0; i < _document.nodes.size(); ++i) {
                    const UIHtmlNode& node = _document.nodes[i];
                    if (!node.IsElement()) {
                        continue;
                    }
                    if (node.tag == "style") {
                        for (const int child : node.children) {
                            UICssParser::ParseInto(_sheet, _document.nodes[child].text, CssSource, _document.nodes[child].line, _report);
                        }
                    } else if (node.tag == "link") {
                        ReadLink(node);
                    }
                    if (const eastl::string* style = node.FindAttribute("style")) {
                        _inlineStyles[i] = UICssParser::ParseDeclarations(*style, node.line, UICssDeclaration::HtmlSource, _report);
                    }
                }
            }

            void ReadLink(const UIHtmlNode& link) {
                const eastl::string* rel = link.FindAttribute("rel");
                const eastl::string* href = link.FindAttribute("href");
                if (rel == nullptr || href == nullptr || UIWebText::ToLower(*rel) != "stylesheet") {
                    return;
                }
                std::optional<eastl::string> css = _loader ? _loader(*href) : std::nullopt;
                if (!css) {
                    _report.AddOnce(HtmlSource, link.line, Format("the style sheet '{}' cannot be read", href->c_str()));
                    return;
                }
                UICssParser::ParseInto(_sheet, *css, Format("css:{}", href->c_str()), 1, _report);
            }

            [[nodiscard]] eastl::string_view SourceOf(const UICssDeclaration& declaration) const {
                return declaration.sourceId == UICssDeclaration::HtmlSource ? HtmlSource : eastl::string_view(_sheet.sources[declaration.sourceId]);
            }

            /// Every declaration is looked at once, whether or not an element matches it, so a typo does not stay silent
            void CheckDeclarations() {
                for (const UICssRule& rule : _sheet.rules) {
                    for (const UICssDeclaration& declaration : rule.declarations) {
                        CheckDeclaration(declaration);
                    }
                }
                for (const auto& declarations : _inlineStyles) {
                    for (const UICssDeclaration& declaration : declarations) {
                        ++_report.declarations;
                        CheckDeclaration(declaration);
                    }
                }
            }

            void CheckDeclaration(const UICssDeclaration& declaration) {
                Resolved scratch;
                const ApplyResult result = ApplyDeclaration(scratch, declaration);
                if (result == ApplyResult::Applied || result == ApplyResult::Ignored) {
                    return;
                }
                const eastl::string message = result == ApplyResult::UnknownProperty ? Format("property '{}' is not supported", declaration.property.c_str())
                                                                                     : Format("'{}: {}' is not understood or not supported", declaration.property.c_str(), declaration.value.c_str());
                if (_report.AddOnce(SourceOf(declaration), declaration.line, message)) {
                    ++_report.declarationsUnsupported;
                }
            }

            // ---- the cascade ----

            [[nodiscard]] bool MatchesCompound(int node, const UICssCompound& compound) const {
                const UIHtmlNode& element = _document.nodes[node];
                if (!compound.tag.empty() && element.tag != compound.tag) {
                    return false;
                }
                if (!compound.id.empty()) {
                    const eastl::string* id = element.FindAttribute("id");
                    if (id == nullptr || *id != compound.id) {
                        return false;
                    }
                }
                return std::all_of(compound.classes.begin(), compound.classes.end(), [&](const eastl::string& className) { return element.HasClass(className); });
            }

            [[nodiscard]] bool MatchesSelector(int node, const UICssSelector& selector, size_t index) const {
                if (!MatchesCompound(node, selector.compounds[index])) {
                    return false;
                }
                if (index == 0) {
                    return true;
                }
                const int parent = _document.nodes[node].parent;
                if (selector.compounds[index].combinator == UICssCombinator::Child) {
                    return parent != UIHtmlDocument::RootIndex && MatchesSelector(parent, selector, index - 1);
                }
                for (int ancestor = parent; ancestor != UIHtmlDocument::RootIndex; ancestor = _document.nodes[ancestor].parent) {
                    if (MatchesSelector(ancestor, selector, index - 1)) {
                        return true;
                    }
                }
                return false;
            }

            struct Match {
                const UICssDeclaration* declaration = nullptr;
                bool important = false;
                bool inlineStyle = false;
                uint32_t specificity = 0;
                uint32_t order = 0;
                uint32_t index = 0;

                [[nodiscard]] bool operator<(const Match& other) const {
                    return std::tie(important, inlineStyle, specificity, order, index) < std::tie(other.important, other.inlineStyle, other.specificity, other.order, other.index);
                }
            };

            /// The declarations in effect for every HTML node when the rules of @p media apply (-1: only those outside `@media`) and every button is in @p state
            [[nodiscard]] eastl::vector<DeclarationList> Cascade(int media, CascadeState state) const {
                eastl::vector<DeclarationList> styles(_document.nodes.size());
                for (size_t i = 1; i < _document.nodes.size(); ++i) {
                    const UIHtmlNode& node = _document.nodes[i];
                    if (!node.IsElement()) {
                        continue;
                    }
                    DeclarationList& list = styles[i];
                    if (node.parent != UIHtmlDocument::RootIndex) {
                        for (const UICssDeclaration* inherited : styles[node.parent]) {
                            // A button keeps its own text alignment and wrapping, like a browser's default style sheet, so these two do not reach it
                            const bool buttonKeeps = node.tag == "button" && (inherited->property == "text-align" || inherited->property == "white-space");
                            if (IsInherited(inherited->property) && !buttonKeeps) {
                                list.push_back(inherited);
                            }
                        }
                    }

                    eastl::vector<Match> matches;
                    for (const UICssRule& rule : _sheet.rules) {
                        if (rule.media != -1 && rule.media != media) {
                            continue;
                        }
                        std::optional<uint32_t> specificity;
                        for (const UICssSelector& selector : rule.selectors) {
                            const UICssPseudo pseudo = selector.compounds.back().pseudo;
                            if (pseudo != UICssPseudo::None && pseudo != ToPseudo(state)) {
                                continue;
                            }
                            if (MatchesSelector(static_cast<int>(i), selector, selector.compounds.size() - 1)) {
                                specificity = std::max(specificity.value_or(0), selector.specificity);
                            }
                        }
                        if (!specificity) {
                            continue;
                        }
                        for (size_t d = 0; d < rule.declarations.size(); ++d) {
                            const UICssDeclaration& declaration = rule.declarations[d];
                            matches.push_back({&declaration, declaration.important, false, *specificity, rule.order, static_cast<uint32_t>(d)});
                        }
                    }
                    for (size_t d = 0; d < _inlineStyles[i].size(); ++d) {
                        const UICssDeclaration& declaration = _inlineStyles[i][d];
                        matches.push_back({&declaration, declaration.important, true, 0, 0, static_cast<uint32_t>(d)});
                    }
                    std::stable_sort(matches.begin(), matches.end());
                    for (const Match& match : matches) {
                        list.push_back(match.declaration);
                    }
                }
                return styles;
            }

            [[nodiscard]] static Resolved Resolve(const DeclarationList& list) {
                Resolved resolved;
                for (const UICssDeclaration* declaration : list) {
                    ApplyDeclaration(resolved, *declaration);
                }
                return resolved;
            }

            // ---- the tree ----

            /// The text of an element and of the inline elements in it: white space collapsed, a `<br>` a line break
            void GatherText(int node, eastl::string& out) const {
                for (const int child : _document.nodes[node].children) {
                    const UIHtmlNode& html = _document.nodes[child];
                    if (!html.IsElement()) {
                        out += html.text;
                    } else if (html.tag == "br") {
                        out.push_back(LineBreakMarker);
                    } else if (!IsIgnoredTag(html.tag)) {
                        GatherText(child, out);
                    }
                }
            }

            [[nodiscard]] static eastl::string CollapseWhitespace(const eastl::string& raw) {
                eastl::string result;
                bool pendingSpace = false;
                for (const char c : raw) {
                    if (c == LineBreakMarker) {
                        result.push_back('\n');
                        pendingSpace = false;
                    } else if (UIWebText::IsSpace(c)) {
                        pendingSpace = true;
                    } else {
                        if (pendingSpace && !result.empty() && result.back() != '\n') {
                            result.push_back(' ');
                        }
                        pendingSpace = false;
                        result.push_back(c);
                    }
                }
                while (!result.empty() && result.back() == '\n') {
                    result.pop_back();
                }
                return result;
            }

            /// An element that holds text and nothing but inline elements around it
            [[nodiscard]] bool HoldsOnlyText(int node, bool& hasText) const {
                for (const int child : _document.nodes[node].children) {
                    const UIHtmlNode& html = _document.nodes[child];
                    if (!html.IsElement()) {
                        hasText |= !UIWebText::TrimSpace(html.text).empty();
                    } else if (IsInlineTag(html.tag)) {
                        if (!HoldsOnlyText(child, hasText)) {
                            return false;
                        }
                    } else if (!IsIgnoredTag(html.tag)) {
                        return false;
                    }
                }
                return true;
            }

            /// A text of its own among the children (not only inside an inline element): `<div>Hello <b>big</b></div>`, not `<div><span>a</span><span>b</span></div>`
            [[nodiscard]] bool HasDirectText(int node) const {
                return std::any_of(_document.nodes[node].children.begin(), _document.nodes[node].children.end(), [&](int child) {
                    const UIHtmlNode& html = _document.nodes[child];
                    return !html.IsElement() && !UIWebText::TrimSpace(html.text).empty();
                });
            }

            [[nodiscard]] bool IsEmptyContent(int node) const {
                return std::all_of(_document.nodes[node].children.begin(), _document.nodes[node].children.end(), [&](int child) {
                    const UIHtmlNode& html = _document.nodes[child];
                    return html.IsElement() ? IsIgnoredTag(html.tag) : UIWebText::TrimSpace(html.text).empty();
                });
            }

            void BuildTree(int htmlParent, int parent, const eastl::vector<DeclarationList>& base) {
                for (const int child : _document.nodes[htmlParent].children) {
                    const UIHtmlNode& html = _document.nodes[child];
                    if (!html.IsElement()) {
                        if (parent != -1 && htmlParent != UIHtmlDocument::RootIndex && !UIWebText::TrimSpace(html.text).empty() && _nodes[parent].kind != WebKind::Label) {
                            _report.AddOnce(HtmlSource, html.line, "text next to other elements is left out: put it in a <span>");
                        }
                        continue;
                    }
                    if (IsIgnoredTag(html.tag)) {
                        continue;
                    }
                    if (IsTransparentTag(html.tag)) {
                        BuildTree(child, parent, base);
                        continue;
                    }
                    AddNode(child, parent, base);
                }
            }

            /// An element is a choice group when it says `data-widget="segmented"` and is not a button or an image (those keep their own kind and the attribute is reported)
            [[nodiscard]] static bool IsSegmented(const UIHtmlNode& html) {
                const eastl::string* widget = html.FindAttribute(WidgetAttribute);
                return widget != nullptr && UIWebText::ToLower(*widget) == SegmentedWidget && html.tag != "button" && html.tag != "img";
            }

            /// `data-widget`, `data-bind` and `data-value` outside the places that understand them are reported, never dropped in silence
            void CheckWidgetAttributes(const UIHtmlNode& html, WebKind kind) {
                if (const eastl::string* widget = html.FindAttribute(WidgetAttribute)) {
                    if (kind != WebKind::Segmented) {
                        const bool known = UIWebText::ToLower(*widget) == SegmentedWidget;
                        _report.AddOnce(HtmlSource, html.line,
                                        known ? Format("data-widget=\"{}\" on a <{}> is left out: it belongs on a <div>", widget->c_str(), html.tag.c_str())
                                              : Format("data-widget=\"{}\" is not supported (\"segmented\" is the only widget)", widget->c_str()));
                    }
                }
                if (kind != WebKind::Segmented && html.HasAttribute("data-bind")) {
                    _report.AddOnce(HtmlSource, html.line, "data-bind only applies to an element with data-widget=\"segmented\"");
                }
                if (kind != WebKind::Button && html.HasAttribute("data-value")) {
                    _report.AddOnce(HtmlSource, html.line, "data-value only applies to a <button>");
                }
            }

            void AddNode(int htmlIndex, int parent, const eastl::vector<DeclarationList>& base) {
                const UIHtmlNode& html = _document.nodes[htmlIndex];
                WebNode node;
                node.html = htmlIndex;
                node.parent = parent;

                bool hasText = false;
                const bool onlyText = HoldsOnlyText(htmlIndex, hasText);
                const bool segmented = IsSegmented(html);
                if (segmented) {
                    node.kind = WebKind::Segmented;
                    if (!Resolve(base[htmlIndex]).flex) {
                        _report.AddOnce(HtmlSource, html.line, "a data-widget=\"segmented\" group needs display: flex (it lays its buttons out as a row or a column)");
                    }
                } else if (html.tag == "button") {
                    node.kind = WebKind::Button;
                } else if (html.tag == "img") {
                    node.kind = WebKind::Image;
                } else if (onlyText && (HasDirectText(htmlIndex) || (IsLabelTag(html.tag) && (hasText || IsEmptyContent(htmlIndex))))) {
                    node.kind = WebKind::Label;
                } else {
                    node.kind = Resolve(base[htmlIndex]).sprite.empty() ? WebKind::Group : WebKind::Panel;
                    if (!IsContainerTag(html.tag) && !IsLabelTag(html.tag)) {
                        _report.AddOnce(HtmlSource, html.line, Format("<{}> is not supported, it is read as a <div>", html.tag.c_str()));
                    }
                }
                CheckWidgetAttributes(html, node.kind);
                if (IsTextKind(node.kind)) {
                    GatherText(htmlIndex, node.text);
                    node.text = CollapseWhitespace(node.text);
                }
                if (const eastl::string* id = html.FindAttribute("id")) {
                    node.name = *id;
                    if (!id->empty() && !_usedNames.insert(*id).second) {
                        _report.AddOnce(HtmlSource, html.line, Format("the id '{}' is used twice: a variant finds the first element only", id->c_str()));
                    }
                }

                const int index = static_cast<int>(_nodes.size());
                _nodes.push_back(std::move(node));
                if (parent != -1) {
                    _nodes[parent].children.push_back(index);
                }
                if (!IsTextKind(_nodes[index].kind) && _nodes[index].kind != WebKind::Image) {
                    BuildTree(htmlIndex, index, base);
                }
            }

            // ---- the attributes ----

            /// The attributes of node @p index when @p styles are in effect. @p report is null when the same node is built again for a variant or a state
            [[nodiscard]] Attrs BuildAttrs(int index, const eastl::vector<DeclarationList>& styles, UIWebReport* report) const {
                const WebNode& node = _nodes[index];
                const UIHtmlNode& html = _document.nodes[node.html];
                const Resolved r = Resolve(styles[node.html]);
                Attrs attrs;

                if (r.displayNone || r.visibilityHidden || html.HasAttribute("hidden")) {
                    attrs.Set("visible", "false");
                }
                if (r.flex) {
                    attrs.Set("layout", "Stack");
                }

                AddPosition(attrs, r, report);
                AddSize(attrs, r, node.kind);

                if (r.flexGrow && *r.flexGrow != 0.0f) {
                    attrs.Set("flex", Number(*r.flexGrow));
                }
                AddStack(attrs, r, report);

                if (IsTextKind(node.kind) && r.fontSize) {
                    attrs.Set("fontSize", Number(*r.fontSize));
                }
                const bool hasFill = r.background && r.background->a != 0;
                if (hasFill) {
                    attrs.Set("color", UIText::FormatColor(*r.background));
                }
                // A panel takes clicks by itself; any other element does when it is a visible plate, and `pointer-events` decides when it is said.
                // A button is interactive already, which is hit-testable, so it is left alone
                const bool defaultBlocks = node.kind == WebKind::Panel;
                const bool blocks = r.pointerEvents.value_or(defaultBlocks || hasFill);
                if (node.kind != WebKind::Button && blocks != defaultBlocks) {
                    attrs.Set("blocksInput", blocks ? "true" : "false");
                }

                switch (node.kind) {
                    case WebKind::Panel:
                        AddSprite(attrs, r, "sprite");
                        if (r.slice) {
                            attrs.Set("sliceBorder", UIText::FormatEdges(*r.slice));
                        }
                        break;
                    case WebKind::Image:
                        if (const eastl::string* src = html.FindAttribute("src")) {
                            attrs.Set("sprite", *src);
                        }
                        AddImageFit(attrs, r, report);
                        break;
                    case WebKind::Label:
                    case WebKind::Button:
                        AddText(attrs, r, node, html);
                        break;
                    case WebKind::Segmented:
                        if (const eastl::string* bind = html.FindAttribute("data-bind")) {
                            attrs.Set("bind", *bind);
                        }
                        if (html.HasAttribute("disabled")) {
                            attrs.Set("disabled", "true");
                        }
                        break;
                    case WebKind::Group:
                        break;
                }
                return attrs;
            }

            static void AddSprite(Attrs& attrs, const Resolved& r, eastl::string_view key) {
                if (!r.sprite.empty()) {
                    attrs.Set(key, r.sprite);
                }
            }

            static void Note(UIWebReport* report, eastl::string message) {
                if (report != nullptr) {
                    report->AddOnce(CssSource, 0, std::move(message));
                }
            }

            static void AddPosition(Attrs& attrs, const Resolved& r, UIWebReport* report) {
                const bool allSides = r.left && r.top && r.right && r.bottom;
                eastl::string anchor = r.anchor;
                if (anchor.empty()) {
                    if (allSides) {
                        anchor = "Stretch";
                    } else {
                        const bool toRight = r.right && !r.left;
                        const bool toBottom = r.bottom && !r.top;
                        anchor = toBottom ? (toRight ? "BottomRight" : "BottomLeft") : (toRight ? "TopRight" : "TopLeft");
                    }
                }
                if (anchor != "TopLeft") {
                    attrs.Set("anchor", anchor);
                }
                if (anchor == "Stretch") {
                    // The distance to each edge of the parent: the offsets of an inset plus the margin
                    UIEdges edges{r.left.value_or(0.0f), r.top.value_or(0.0f), r.right.value_or(0.0f), r.bottom.value_or(0.0f)};
                    if (r.margin) {
                        edges = {edges.left + r.margin->left, edges.top + r.margin->top, edges.right + r.margin->right, edges.bottom + r.margin->bottom};
                    }
                    if (!(edges == UIEdges{})) {
                        attrs.Set("margin", UIText::FormatEdges(edges));
                    }
                    return;
                }
                if (r.margin && !(*r.margin == UIEdges{})) {
                    Note(report, "margin only applies to an element that fills its parent (inset on all sides, or anchor: stretch); use left, top, right, bottom or gap");
                }
                const float x = r.left.value_or(0.0f) - r.right.value_or(0.0f);
                const float y = r.top.value_or(0.0f) - r.bottom.value_or(0.0f);
                if (x != 0.0f) {
                    attrs.Set("x", Number(x));
                }
                if (y != 0.0f) {
                    attrs.Set("y", Number(y));
                }
            }

            static void AddSize(Attrs& attrs, const Resolved& r, WebKind kind) {
                if (r.width) {
                    attrs.Set("width", UIText::FormatLength(*r.width));
                }
                if (r.height) {
                    attrs.Set("height", UIText::FormatLength(*r.height));
                }
                if (r.minWidth) {
                    attrs.Set("minWidth", UIText::FormatLength(*r.minWidth));
                }
                if (r.maxWidth) {
                    attrs.Set("maxWidth", UIText::FormatLength(*r.maxWidth));
                }
                if (r.minHeight) {
                    attrs.Set("minHeight", UIText::FormatLength(*r.minHeight));
                }
                if (r.maxHeight) {
                    attrs.Set("maxHeight", UIText::FormatLength(*r.maxHeight));
                }
                if (r.aspectRatio) {
                    attrs.Set("aspectRatio", Number(*r.aspectRatio));
                    if (r.objectFit == "cover" && kind != WebKind::Image) {
                        attrs.Set("aspectMode", "Fill");
                    }
                }
            }

            static void AddImageFit(Attrs& attrs, const Resolved& r, UIWebReport* report) {
                if (r.objectFit == "fill") {
                    attrs.Set("stretch", "true");
                } else if (r.objectFit == "cover") {
                    Note(report, "object-fit: cover is not supported on an <img>: it keeps its proportions inside the box (contain) or fills it (fill)");
                }
            }

            static void AddStack(Attrs& attrs, const Resolved& r, UIWebReport* report) {
                if (!r.flex) {
                    if (r.gap || r.padding || !r.flexDirection.empty() || r.wrapChildren || !r.alignItems.empty()) {
                        Note(report, "gap, padding, flex-direction, flex-wrap and align-items only apply to an element with display: flex");
                    }
                    return;
                }
                // CSS lays a flex container out as a row unless it says otherwise; the stack is a column unless it says otherwise
                const eastl::string direction = r.flexDirection.empty() ? eastl::string("Horizontal") : r.flexDirection;
                if (direction != "Vertical") {
                    attrs.Set("direction", direction);
                }
                if (r.gap && *r.gap != 0.0f) {
                    attrs.Set("spacing", Number(*r.gap));
                }
                if (r.padding && !(*r.padding == UIEdges{})) {
                    attrs.Set("padding", UIText::FormatEdges(*r.padding));
                }
                if (r.wrapChildren.value_or(false)) {
                    attrs.Set("wrapChildren", "true");
                }
                if (!r.alignItems.empty() && r.alignItems != "Start") {
                    attrs.Set("crossAlign", r.alignItems);
                }
            }

            static void AddText(Attrs& attrs, const Resolved& r, const WebNode& node, const UIHtmlNode& html) {
                attrs.Set("text", node.text);
                if (!r.fontPath.empty()) {
                    attrs.Set("font", r.fontPath);
                }
                if (r.textColor) {
                    attrs.Set("textColor", UIText::FormatColor(*r.textColor));
                }
                if (!r.textAlign.empty()) {
                    attrs.Set("align", r.textAlign);
                }
                if (!r.verticalAlign.empty()) {
                    attrs.Set("verticalAlign", r.verticalAlign);
                }
                if (r.textFit.value_or(false)) {
                    attrs.Set("overflow", "AutoFit");
                } else if (!r.textOverflow.empty()) {
                    attrs.Set("overflow", r.textOverflow);
                }
                if (r.minFontSize) {
                    attrs.Set("minFontSize", Number(*r.minFontSize));
                }
                if (r.wrapText) {
                    attrs.Set("wrap", *r.wrapText ? "true" : "false");
                }
                if (r.lineHeightRatio) {
                    attrs.Set("lineSpacing", Number(*r.lineHeightRatio));
                } else if (r.lineHeightPixels) {
                    attrs.Set("lineSpacing", Number(*r.lineHeightPixels / r.fontSize.value_or(DefaultFontSizeForLineHeight)));
                }
                if (node.kind == WebKind::Button) {
                    const eastl::string* action = html.FindAttribute("data-action");
                    if (action == nullptr) {
                        action = html.FindAttribute("action");
                    }
                    if (action != nullptr) {
                        attrs.Set("action", *action);
                    }
                    if (const eastl::string* value = html.FindAttribute("data-value")) {
                        attrs.Set("value", *value);
                    }
                    if (html.HasAttribute("disabled")) {
                        attrs.Set("disabled", "true");
                    }
                }
            }

            // ---- variants and states ----

            [[nodiscard]] eastl::vector<VariantOutput> BuildVariants() {
                eastl::vector<VariantOutput> variants;
                for (size_t m = 0; m < _sheet.medias.size(); ++m) {
                    const UICssMedia& media = _sheet.medias[m];
                    const bool used = std::any_of(_sheet.rules.begin(), _sheet.rules.end(), [&](const UICssRule& rule) { return rule.media == static_cast<int>(m); });
                    if (!used) {
                        continue;
                    }
                    const auto styles = Cascade(static_cast<int>(m), CascadeState::None);
                    eastl::vector<Override> overrides;
                    for (size_t i = 0; i < _nodes.size(); ++i) {
                        Override entry = DiffForVariant(static_cast<int>(i), BuildAttrs(static_cast<int>(i), styles, nullptr), media);
                        if (!entry.values.empty()) {
                            overrides.push_back(std::move(entry));
                        }
                    }
                    if (overrides.empty()) {
                        continue;
                    }
                    for (const UIVariantCondition& alternative : media.alternatives) {
                        variants.push_back({alternative, overrides});
                        ++_report.variants;
                    }
                }
                return variants;
            }

            [[nodiscard]] Override DiffForVariant(int index, const Attrs& changed, const UICssMedia& media) {
                const Attrs& base = _baseAttrs[index];
                Override entry;
                entry.node = index;
                auto consider = [&](eastl::string_view key) {
                    const OverridableKey* overridable = FindOverridable(key);
                    const eastl::string* before = base.Find(key);
                    const eastl::string* after = changed.Find(key);
                    const eastl::string_view beforeText = before ? eastl::string_view(*before) : (overridable ? overridable->defaultValue : eastl::string_view());
                    const eastl::string_view afterText = after ? eastl::string_view(*after) : (overridable ? overridable->defaultValue : eastl::string_view());
                    if (beforeText == afterText) {
                        return;
                    }
                    if (overridable == nullptr) {
                        _report.AddOnce(_sheet.sources[media.sourceId], media.line,
                                        Format("@media changes '{}': a variant can change only visibility, anchor, offset, size, direction and font size", eastl::string(key).c_str()));
                        return;
                    }
                    entry.values.emplace_back(eastl::string(key), eastl::string(afterText));
                };
                for (const auto& [key, value] : base.Entries()) {
                    consider(key);
                }
                for (const auto& [key, value] : changed.Entries()) {
                    if (base.Find(key) == nullptr) {
                        consider(key);
                    }
                }
                return entry;
            }

            /// `:hover`, `:active`, `:disabled` and `:checked` of a button become its hoverColor, pressedColor, disabledColor and selectedColor
            void ApplyStates() {
                static const std::pair<CascadeState, const char*> States[] = {
                    {CascadeState::Hover, "hoverColor"}, {CascadeState::Active, "pressedColor"}, {CascadeState::Disabled, "disabledColor"}, {CascadeState::Checked, "selectedColor"}};
                for (const auto& [state, colorKey] : States) {
                    const UICssPseudo pseudo = ToPseudo(state);
                    const bool used = std::any_of(_sheet.rules.begin(), _sheet.rules.end(), [&](const UICssRule& rule) {
                        return std::any_of(rule.selectors.begin(), rule.selectors.end(), [&](const UICssSelector& selector) { return selector.compounds.back().pseudo == pseudo; });
                    });
                    if (!used) {
                        continue;
                    }
                    const auto styles = Cascade(-1, state);
                    for (size_t i = 0; i < _nodes.size(); ++i) {
                        const Attrs stateAttrs = BuildAttrs(static_cast<int>(i), styles, nullptr);
                        Attrs& base = _baseAttrs[i];
                        for (const auto& [key, value] : stateAttrs.Entries()) {
                            const eastl::string* before = base.Find(key);
                            if (before != nullptr && *before == value) {
                                continue;
                            }
                            if (_nodes[i].kind == WebKind::Button && key == "color") {
                                base.Set(colorKey, value);
                            } else {
                                _report.AddOnce(CssSource, 0,
                                                Format("a state rule can change only the background of a <button>, not '{}' of a <{}>", key.c_str(), _document.nodes[_nodes[i].html].tag.c_str()));
                            }
                        }
                    }
                }
            }

            // ---- the output ----

            [[nodiscard]] eastl::string UniqueName(const eastl::string& tag) {
                for (int suffix = 1;; ++suffix) {
                    eastl::string candidate = Format("{}-{}", tag.c_str(), suffix);
                    if (_usedNames.insert(candidate).second) {
                        return candidate;
                    }
                }
            }

            void WriteNode(pugi::xml_node parent, int index) {
                const WebNode& node = _nodes[index];
                pugi::xml_node element = parent.append_child("element");
                element.append_attribute("type") = TypeName(node.kind);
                if (!node.name.empty()) {
                    element.append_attribute("name") = node.name.c_str();
                }
                for (const auto& [key, value] : _baseAttrs[index].Entries()) {
                    element.append_attribute(key.c_str()) = value.c_str();
                }
                ++_report.elements;
                if (!node.children.empty()) {
                    pugi::xml_node children = element.append_child("children");
                    for (const int child : node.children) {
                        WriteNode(children, child);
                    }
                }
            }

            [[nodiscard]] eastl::string Write(const eastl::vector<VariantOutput>& variants) {
                // An element a variant names needs a name to be found by
                for (const VariantOutput& variant : variants) {
                    for (const Override& entry : variant.overrides) {
                        if (_nodes[entry.node].name.empty()) {
                            _nodes[entry.node].name = UniqueName(_document.nodes[_nodes[entry.node].html].tag);
                        }
                    }
                }

                pugi::xml_document document;
                pugi::xml_node ui = document.append_child("ui");
                pugi::xml_node elements = ui.append_child("elements");
                for (size_t i = 0; i < _nodes.size(); ++i) {
                    if (_nodes[i].parent == -1) {
                        WriteNode(elements, static_cast<int>(i));
                    }
                }
                if (!variants.empty()) {
                    pugi::xml_node variantsNode = ui.append_child("variants");
                    for (const VariantOutput& variant : variants) {
                        pugi::xml_node variantNode = variantsNode.append_child("variant");
                        const eastl::string when = variant.when.ToText();
                        if (!when.empty()) {
                            variantNode.append_attribute("when") = when.c_str();
                        }
                        pugi::xml_node overrides = variantNode.append_child("overrides");
                        for (const Override& entry : variant.overrides) {
                            pugi::xml_node set = overrides.append_child("set");
                            set.append_attribute("element") = _nodes[entry.node].name.c_str();
                            for (const auto& [key, value] : entry.values) {
                                set.append_attribute(key.c_str()) = value.c_str();
                            }
                        }
                    }
                }

                struct StringWriter : pugi::xml_writer {
                    eastl::string text;
                    void write(const void* data, size_t size) override {
                        text.append(static_cast<const char*>(data), size);
                    }
                } writer;
                document.save(writer, "  ", pugi::format_default | pugi::format_no_declaration);
                return writer.text;
            }

            const UIWebCompiler::StylesheetLoader& _loader;
            UIWebReport& _report;
            UIHtmlDocument _document;
            UICssSheet _sheet;
            eastl::vector<eastl::vector<UICssDeclaration>> _inlineStyles;
            eastl::vector<WebNode> _nodes;
            eastl::vector<Attrs> _baseAttrs;
            eastl::hash_set<eastl::string> _usedNames;
        };

    }  // namespace

    eastl::string UIWebCompiler::Compile(eastl::string_view html, const StylesheetLoader& loadStylesheet, UIWebReport& report) {
        return Compiler(html, loadStylesheet, report).Run();
    }

}  // namespace BECore
