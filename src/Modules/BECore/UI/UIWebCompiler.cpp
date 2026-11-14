#include "UIWebCompiler.h"

#include <BECore/UI/UICssParser.h>
#include <BECore/UI/UIHtmlParser.h>
#include <BECore/UI/UISwitchComponent.h>
#include <BECore/UI/UIValueText.h>
#include <BECore/UI/UIWebText.h>
#include <EASTL/hash_set.h>

namespace BECore {

    namespace {

        constexpr StringView HtmlSource = "html";
        constexpr StringView CssSource = "css";
        constexpr StringView WidgetAttribute = "data-widget";
        constexpr char LineBreakMarker = '\x01';  // a `<br>` while the text of a label is gathered; the white space of the source is never this
        constexpr float DefaultFontSizeForLineHeight = 24.0f;
        constexpr size_t BoxValueMax = 4;
        constexpr int FontWeightNormal = 400;
        constexpr int FontWeightBold = 700;
        constexpr float FontWeightMin = 100.0f;
        constexpr float FontWeightMax = 900.0f;
        constexpr float FontWeightStep = 100.0f;
        constexpr size_t TextShadowMinNumbers = 2;  // offsetX, offsetY
        constexpr size_t TextShadowMaxNumbers = 3;  // ... and the blur
        constexpr size_t TextShadowBlurIndex = 2;
        constexpr float MillisecondsToSeconds = 0.001f;
        constexpr float PercentToFraction = 0.01f;

        // The composed button (`ui-controls.md`, "Components"): the attributes that live on its components, the forms of an icon (`UIImageComponent::kIconOnlyShare`, `kIconWithTextShare`,
        // `kIconPadding`, `kIconGap`, which the tests hold these to) and the name suffixes of its children. A plain text element carries the same text component, a picture the image one
        constexpr StringView TextComponentKeys[] = {"text",     "font", "fontFamily",  "fontWeight",  "textColor",   "align",     "verticalAlign",
                                                    "overflow", "wrap", "lineSpacing", "minFontSize", "textOutline", "textShadow"};
        constexpr StringView ClickerKeys[] = {"action", "value", "disabled"};
        // The fields of the `UITransitionComponent` a `transition` makes (written as attributes of the node, moved into the component by the writers)
        constexpr StringView TransitionKeys[] = {"fade", "scaleFrom", "slideX", "slideY", "duration", "ease", "inputDuringTransition"};
        constexpr StringView ImageKeys[] = {"sprite", "stretch"};  // of the `UIImageComponent` of an `img`
        constexpr StringView IconKeys[] = {"icon", "iconTint"};
        constexpr StringView StackKeys[] = {"layout", "direction", "spacing", "padding", "crossAlign", "wrapChildren"};
        // What a button or a toggle does with its text and a `UITextComponent` does not
        struct ButtonTextDefault {
            StringView key;
            StringView value;
        };
        constexpr ButtonTextDefault ButtonTextDefaults[] = {{"align", "Center"}, {"verticalAlign", "Center"}, {"wrap", "false"}};
        constexpr const char* IconOnlySide = "60%";     ///< width and height of the icon of a button with no caption
        constexpr const char* IconCaptionSide = "50%";  ///< height of the icon of a button with a caption
        constexpr const char* IconPadding = "8,0,0,0";
        constexpr const char* IconGap = "6";
        constexpr StringView IconSuffix = "Icon";
        constexpr StringView CaptionSuffix = "Caption";

        // The composed toggle and slider (`ui-controls.md`, "Components"): the attributes of their components and the row of a toggle, which `tools/migrate/steps/_ui_value.py` writes the same
        constexpr StringView ToggleKeys[] = {"bind", "action", "onValue", "offValue"};
        constexpr StringView SwitchKeys[] = {"trackColor", "fillColor", "thumbColor", "partShape"};  // the switch of a toggle and the slider have them under the same names
        constexpr StringView SliderKeys[] = {"bind", "action", "minValue", "maxValue", "step"};
        constexpr ButtonTextDefault ToggleTextDefaults[] = {{"verticalAlign", "Center"}, {"wrap", "false"}};  // what the caption of a toggle does with its text
        constexpr StringView SwitchSuffix = "Switch";
        constexpr const char* FadeWhenDisabledKey = "fadeWhenDisabled";  ///< the caption of a toggle fades while it is disabled
        constexpr const char* ToggleGap = "8";                           ///< the air between the caption and the switch (`UISwitchComponent::kCaptionGap`)
        constexpr Color CaptionWhite{255, 255, 255, 255};                ///< the caption colour the switch component's own fallbacks assume

        template <size_t N>
        bool IsKeyOf(const StringView (&keys)[N], StringView key) {
            return std::find(std::begin(keys), std::end(keys), key) != std::end(keys);
        }

        // The kinds of element the compile writes
        CORE_ENUM(WebKind, uint8_t, Group, Panel, Label, Button, Image, Segmented, Toggle, Slider, Progress)

        const char* TypeName(WebKind kind) {
            switch (kind) {
                case WebKind::Panel:
                    return "UIPanel";
                case WebKind::Segmented:
                    return "UISegmented";
                case WebKind::Progress:
                    return "UIProgressBar";
                case WebKind::Group:
                case WebKind::Label:   // a `UIElement` with a `UITextComponent`
                case WebKind::Image:   // a `UIElement` with a `UIImageComponent`
                case WebKind::Button:  // written by `WriteButton`, a composed `UIElement`
                case WebKind::Toggle:  // by `WriteToggle`
                case WebKind::Slider:  // by `WriteSlider`
                    break;
            }
            return "UIElement";
        }

        /// A kind whose caption is its text (a toggle's is the text of its caption child)
        bool IsTextKind(WebKind kind) {
            return kind == WebKind::Label || kind == WebKind::Button || kind == WebKind::Toggle;
        }

        // The kinds a `data-widget` names: the value of the attribute (lower case) and what it becomes
        struct WidgetName {
            StringView name;
            WebKind kind;
        };
        constexpr WidgetName WidgetNames[] = {{"segmented", WebKind::Segmented}, {"toggle", WebKind::Toggle}, {"slider", WebKind::Slider}, {"progress", WebKind::Progress}};

        bool IsWidgetKind(WebKind kind) {
            return std::any_of(std::begin(WidgetNames), std::end(WidgetNames), [kind](const WidgetName& widget) { return widget.kind == kind; });
        }

        /// A kind that is a control of its own whose children (if the markup has any) are not read as elements: its content is its text or nothing
        bool ReadsChildren(WebKind kind) {
            return kind == WebKind::Group || kind == WebKind::Panel || kind == WebKind::Segmented;
        }

        bool IsOneOf(StringView tag, std::initializer_list<StringView> names) {
            return std::find(names.begin(), names.end(), tag) != names.end();
        }

        bool IsIgnoredTag(StringView tag) {
            return IsOneOf(tag, {"head", "style", "link", "script", "meta", "title", "noscript", "template", "br"});
        }

        bool IsTransparentTag(StringView tag) {
            return IsOneOf(tag, {"html", "body"});
        }

        bool IsContainerTag(StringView tag) {
            return IsOneOf(tag, {"div", "section", "article", "nav", "header", "footer", "main", "aside", "ul", "ol", "li", "form", "fieldset", "figure"});
        }

        bool IsLabelTag(StringView tag) {
            return IsOneOf(tag, {"span", "p", "h1", "h2", "h3", "h4", "h5", "h6", "label", "strong", "b", "i", "em", "small", "a", "code"});
        }

        /// A tag that may sit inside the text of a label
        bool IsInlineTag(StringView tag) {
            return IsOneOf(tag, {"span", "b", "i", "u", "em", "strong", "small", "a", "code", "mark", "sub", "sup", "br", "label"});
        }

        /// The properties CSS inherits that the compile follows
        bool IsInherited(StringView property) {
            return IsOneOf(property, {"color", "font-size", "font-family", "font-weight", "text-shadow", "-be-text-outline", "text-align", "line-height", "white-space"});
        }

        // ---- the values of the properties --------------------------------------------------------------------------------

        /// `-be-text-outline` (width in pixels, colour) and `text-shadow` (offsets in pixels, colour) after parsing
        struct TextOutlineValue {
            float width = 0.0f;
            UIWebText::WebColor color;
        };
        struct TextShadowValue {
            float offsetX = 0.0f;
            float offsetY = 0.0f;
            UIWebText::WebColor color;
        };

        /// A `transition` value: the seconds of each kind of change it names (not set: not named) and the easing of the fade and the slide
        struct TransitionValue {
            Optional<float> fade;                 ///< `opacity`
            Optional<float> transform;            ///< `transform`: the scale and the slide of `-be-transition-scale` / `-be-transition-slide`
            bool transformFirst = false;          ///< `transform` is named before `opacity`: its time is the shared duration
            Optional<float> state;                ///< `background-color`, `color`, `border-color`, `all`: the look of a state
            EaseType ease = EaseType::QuadInOut;  ///< CSS's default easing is `ease`
        };

        /// What the declarations of one element come to, last one winning. Every field is "not set" until a declaration sets it
        struct Resolved {
            bool displayNone = false;
            bool visibilityHidden = false;
            bool flex = false;
            Optional<float> left;
            Optional<float> right;
            Optional<float> top;
            Optional<float> bottom;
            String anchor;
            Optional<UILength> width;
            Optional<UILength> height;
            Optional<UILength> minWidth;
            Optional<UILength> maxWidth;
            Optional<UILength> minHeight;
            Optional<UILength> maxHeight;
            Optional<float> aspectRatio;
            String objectFit;
            Optional<UIEdges> margin;
            Optional<UIEdges> padding;
            Optional<float> gap;
            Optional<float> flexGrow;
            String flexDirection;
            Optional<bool> wrapChildren;
            String alignItems;
            Optional<UIWebText::WebColor> background;
            String sprite;
            Optional<UIWebText::GradientSpec> gradient;
            Optional<UICorners> cornerRadius;
            Optional<float> borderWidth;
            Optional<UIWebText::WebColor> borderColor;
            bool borderHidden = false;  ///< `border-style: none`
            Optional<UIWebText::BoxShadows> shadows;
            Optional<UIWebText::WebColor> highlight;
            Optional<UIPartShape> partShape;
            Optional<UIEdges> slice;
            Optional<bool> pointerEvents;
            Optional<UIWebText::WebColor> textColor;
            Optional<float> fontSize;
            String fontPath;
            String fontFamily;  ///< a name of the `families` table of TextConfig.xml, which the compiler does not know: a name nobody registered is the label's warning at run time
            Optional<int> fontWeight;
            Optional<TextOutlineValue> textOutline;
            Optional<TextShadowValue> textShadow;
            String textAlign;
            String verticalAlign;
            String textOverflow;
            Optional<bool> textFit;
            Optional<float> minFontSize;
            Optional<bool> wrapText;
            Optional<float> lineHeightRatio;
            Optional<float> lineHeightPixels;
            Optional<UIWebText::WebColor> trackColor;
            Optional<UIWebText::WebColor> fillColor;
            Optional<UIWebText::WebColor> thumbColor;
            Optional<UIWebText::WebColor> iconColor;
            Optional<float> opacity;
            Optional<TransitionValue> transition;
            Optional<float> transitionScale;
            Optional<std::pair<float, float>> transitionSlide;
            Optional<bool> transitionInput;
            /// Set by a handler that applied a value with a caveat (a blurred shadow drawn hard): the check of the declarations reports it, the value still counts
            String note;
        };

        /// A CSS time: `.2s` or `200ms`, not negative
        Optional<float> ParseTime(StringView text) {
            const String lower = UIWebText::ToLower(UIWebText::TrimSpace(text));
            float scale = 1.0f;
            StringView number = lower;
            if (number.size() > 2 && number.substr(number.size() - 2) == "ms") {
                number.remove_suffix(2);
                scale = MillisecondsToSeconds;
            } else if (number.size() > 1 && number.back() == 's') {
                number.remove_suffix(1);
            } else {
                return std::nullopt;
            }
            const auto value = UIText::ParseFiniteFloat(number);
            if (!value || *value < 0.0f) {
                return std::nullopt;
            }
            return *value * scale;
        }

        /// The CSS easings the engine has a curve for: `ease` is the quadratic in-out, the others the quadratic or the line
        Optional<EaseType> ParseEase(StringView word) {
            static const std::pair<StringView, EaseType> Names[] = {
                {"linear", EaseType::Linear}, {"ease", EaseType::QuadInOut}, {"ease-in", EaseType::QuadIn}, {"ease-out", EaseType::QuadOut}, {"ease-in-out", EaseType::QuadInOut}};
            for (const auto& [name, ease] : Names) {
                if (word == name) {
                    return ease;
                }
            }
            return std::nullopt;
        }

        /// `opacity`: a number or a percentage, 0 to 1
        bool ParseOpacity(StringView value, Optional<float>& out) {
            StringView text = UIWebText::TrimSpace(value);
            float scale = 1.0f;
            if (!text.empty() && text.back() == '%') {
                text.remove_suffix(1);
                scale = PercentToFraction;
            }
            const auto number = UIText::ParseFiniteFloat(text);
            if (!number) {
                return false;
            }
            const float opacity = *number * scale;
            if (opacity < 0.0f || opacity > 1.0f) {
                return false;
            }
            out = opacity;
            return true;
        }

        /// Adds @p text to the note of a declaration
        void AddNote(Resolved& r, StringView text) {
            if (!r.note.empty()) {
                r.note += "; ";
            }
            r.note += String(text);
        }

        /// `transition`: `<property> <time> [<easing>] [<delay>]` entries, comma separated. A delay, an easing the engine has no curve for and a property it does not animate are
        /// noted and left out; the declaration is not understood when no entry is
        bool ParseTransition(Resolved& r, StringView value) {
            if (UIWebText::ToLower(UIWebText::TrimSpace(value)) == "none") {
                r.transition.reset();
                return true;
            }
            TransitionValue parsed;
            bool any = false;
            bool easeSet = false;
            for (const StringView entry : UIWebText::SplitTopLevel(value, ',')) {
                Optional<float> duration;
                Optional<float> delay;
                Optional<EaseType> ease;
                String property;
                bool understood = true;
                for (const StringView word : UIWebText::SplitWords(entry)) {
                    const String lower = UIWebText::ToLower(word);
                    if (const auto time = ParseTime(lower)) {
                        (duration ? delay : duration) = *time;
                    } else if (const auto curve = ParseEase(lower)) {
                        ease = curve;
                    } else if (property.empty() && !lower.empty() && (std::isalpha(static_cast<unsigned char>(lower[0])) != 0)) {
                        property = lower;
                    } else {
                        understood = false;
                    }
                }
                if (property.empty()) {
                    property = "all";  // CSS: no property is all of them
                }
                Optional<float>* target = property == "opacity"                                                     ? &parsed.fade
                                          : property == "transform"                                                 ? &parsed.transform
                                          : IsOneOf(property, {"background-color", "color", "border-color", "all"}) ? &parsed.state
                                                                                                                    : nullptr;
                // The colour properties share the one state duration; the others are named once
                const bool repeated = target != nullptr && *target && target != &parsed.state;
                if (!understood || !duration || target == nullptr || repeated) {
                    AddNote(r, Format("'{}' is not supported: left out", UIText::Excerpt(entry).c_str()));
                    continue;
                }
                any = true;
                if (target == &parsed.transform && !parsed.fade) {
                    parsed.transformFirst = true;
                }
                if (target == &parsed.state && *target && **target != *duration) {
                    AddNote(r, "the colours of a state share one duration: the first is used");
                } else {
                    *target = *duration;
                }
                if (delay && *delay != 0.0f) {
                    AddNote(r, "a transition delay is not supported: left out");
                }
                if (ease && target != &parsed.state) {
                    if (easeSet && parsed.ease != *ease) {
                        AddNote(r, "fade and slide share one easing: the first is used");
                    } else {
                        parsed.ease = *ease;
                        easeSet = true;
                    }
                }
            }
            if (!any) {
                r.note.clear();
                return false;
            }
            if (parsed.fade && parsed.transform && *parsed.fade != *parsed.transform) {
                AddNote(r, "fade and slide share one duration: the first is used");
            }
            r.transition = parsed;
            return true;
        }

        bool ParseOffset(StringView value, Optional<float>& out) {
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

        bool ParseSize(StringView value, Optional<UILength>& out) {
            const String lower = UIWebText::ToLower(value);
            if (lower == "auto" || lower == "none") {
                out.reset();
                return true;
            }
            const auto length = UIText::ParseLength(value);
            if (!length || !std::isfinite(length->value)) {
                return false;
            }
            out = *length;
            return true;
        }

        /// The one to four values of a CSS box shorthand (top right bottom left) as edges
        Optional<UIEdges> ParseBox(StringView value) {
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
        String AnchorName(StringView value) {
            static const std::pair<StringView, const char*> Names[] = {
                {"top-left", "TopLeft"},       {"top", "Top"},       {"top-right", "TopRight"},       {"left", "Left"},      {"center", "Center"}, {"right", "Right"},
                {"bottom-left", "BottomLeft"}, {"bottom", "Bottom"}, {"bottom-right", "BottomRight"}, {"stretch", "Stretch"}};
            const String lower = UIWebText::ToLower(value);
            for (const auto& [css, name] : Names) {
                if (lower == css) {
                    return name;
                }
            }
            return {};
        }

        /// `flex`: the first number is the grow factor (`1`, `1 1 0`, `2 0 100px`); `auto` is 1, `none` is 0
        bool ParseFlexGrow(StringView value, Optional<float>& out) {
            const String lower = UIWebText::ToLower(value);
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
            const auto grow = UIText::ParseFiniteFloat(words[0]);
            if (!grow) {
                return false;
            }
            out = *grow;
            return true;
        }

        /// A radius of one corner (`border-top-left-radius`...): one px length, the other corners stay as they are
        bool SetCorner(Resolved& r, StringView value, float UICorners::* corner) {
            if (UIWebText::SplitWords(value).size() != 1) {
                return false;
            }
            const auto radius = UIWebText::ParseCornerRadii(value);
            if (!radius) {
                return false;
            }
            UICorners corners = r.cornerRadius.value_or(UICorners{});
            corners.*corner = radius->topLeft;
            r.cornerRadius = corners;
            return true;
        }

        /// A border width: one px length, not negative
        Optional<float> ParseBorderWidth(StringView value) {
            const auto width = UIWebText::ParsePixels(value);
            if (!width || !std::isfinite(*width) || *width < 0.0f) {
                return std::nullopt;
            }
            return width;
        }

        /// One word of a `background` or `background-image`: a `url()` or a `linear-gradient()`, which go into @p parsed
        bool ParseImageWord(StringView word, Resolved& parsed) {
            if (const auto url = UIWebText::ParseUrl(word)) {
                parsed.sprite = *url;
                return true;
            }
            if (const auto gradient = UIWebText::ParseLinearGradient(word)) {
                parsed.gradient = *gradient;
                return true;
            }
            return false;
        }

        enum class ApplyResult { Applied, Ignored, BadValue, UnknownProperty };

        using Handler = bool (*)(Resolved&, StringView);

        struct PropertyHandler {
            StringView name;
            Handler handler;
        };

        // A handler changes the Resolved only when it understands the value
        const PropertyHandler PropertyHandlers[] = {
            {"display",
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(v);
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
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(v);
                 if (lower != "visible" && lower != "hidden") {
                     return false;
                 }
                 r.visibilityHidden = lower == "hidden";
                 return true;
             }},
            {"left", [](Resolved& r, StringView v) { return ParseOffset(v, r.left); }},
            {"right", [](Resolved& r, StringView v) { return ParseOffset(v, r.right); }},
            {"top", [](Resolved& r, StringView v) { return ParseOffset(v, r.top); }},
            {"bottom", [](Resolved& r, StringView v) { return ParseOffset(v, r.bottom); }},
            {"inset",
             [](Resolved& r, StringView v) {
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
             [](Resolved& r, StringView v) {
                 String name = AnchorName(v);
                 if (name.empty()) {
                     return false;
                 }
                 r.anchor = std::move(name);
                 return true;
             }},
            {"width", [](Resolved& r, StringView v) { return ParseSize(v, r.width); }},
            {"height", [](Resolved& r, StringView v) { return ParseSize(v, r.height); }},
            {"min-width", [](Resolved& r, StringView v) { return ParseSize(v, r.minWidth); }},
            {"max-width", [](Resolved& r, StringView v) { return ParseSize(v, r.maxWidth); }},
            {"min-height", [](Resolved& r, StringView v) { return ParseSize(v, r.minHeight); }},
            {"max-height", [](Resolved& r, StringView v) { return ParseSize(v, r.maxHeight); }},
            {"aspect-ratio",
             [](Resolved& r, StringView v) {
                 if (UIWebText::ToLower(v) == "auto") {
                     r.aspectRatio.reset();
                     return true;
                 }
                 const size_t slash = v.find('/');
                 const auto numerator = UIText::ParseFiniteFloat(slash == StringView::npos ? v : v.substr(0, slash));
                 const auto denominator = slash == StringView::npos ? Optional<float>{1.0f} : UIText::ParseFiniteFloat(v.substr(slash + 1));
                 if (!numerator || !denominator || *denominator <= 0.0f || *numerator <= 0.0f) {
                     return false;
                 }
                 r.aspectRatio = *numerator / *denominator;
                 return true;
             }},
            {"object-fit",
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(v);
                 if (!IsOneOf(lower, {"contain", "cover", "fill", "none", "scale-down"})) {
                     return false;
                 }
                 r.objectFit = lower;
                 return true;
             }},
            {"margin",
             [](Resolved& r, StringView v) {
                 const auto box = ParseBox(v);
                 if (!box) {
                     return false;
                 }
                 r.margin = *box;
                 return true;
             }},
            {"padding",
             [](Resolved& r, StringView v) {
                 const auto box = ParseBox(v);
                 if (!box) {
                     return false;
                 }
                 r.padding = *box;
                 return true;
             }},
            {"gap",
             [](Resolved& r, StringView v) {
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
             [](Resolved& r, StringView v) {
                 const auto pixels = UIWebText::ParsePixels(v);
                 if (pixels) {
                     r.gap = *pixels;
                 }
                 return pixels.has_value();
             }},
            {"column-gap",
             [](Resolved& r, StringView v) {
                 const auto pixels = UIWebText::ParsePixels(v);
                 if (pixels) {
                     r.gap = *pixels;
                 }
                 return pixels.has_value();
             }},
            {"flex", [](Resolved& r, StringView v) { return ParseFlexGrow(v, r.flexGrow); }},
            {"flex-grow", [](Resolved& r, StringView v) { return ParseFlexGrow(v, r.flexGrow); }},
            {"flex-direction",
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(v);
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
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(v);
                 if (lower != "wrap" && lower != "nowrap") {
                     return false;
                 }
                 r.wrapChildren = lower == "wrap";
                 return true;
             }},
            {"align-items",
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(v);
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
             [](Resolved& r, StringView v) {
                 const auto color = UIWebText::ParseColor(v);
                 if (color) {
                     r.background = *color;
                 }
                 return color.has_value();
             }},
            {"background-image",
             [](Resolved& r, StringView v) {
                 // The image layers are replaced as a whole, as in CSS: a url, a linear-gradient or both (the sprite is drawn over the gradient), `none` for neither
                 Resolved parsed;
                 const auto words = UIWebText::SplitWords(v);
                 if (words.empty()) {
                     return false;
                 }
                 for (const StringView word : words) {
                     if (UIWebText::ToLower(word) != "none" && !ParseImageWord(word, parsed)) {
                         return false;
                     }
                 }
                 r.sprite = parsed.sprite;
                 r.gradient = parsed.gradient;
                 return true;
             }},
            {"background",
             [](Resolved& r, StringView v) {
                 // A color, a url and a linear-gradient, in any order; the repeat and position words that mean nothing here are skipped
                 Resolved parsed;
                 for (const StringView word : UIWebText::SplitWords(v)) {
                     const String lower = UIWebText::ToLower(word);
                     if (lower == "none" || lower == "no-repeat" || lower == "center") {
                         continue;
                     }
                     if (ParseImageWord(word, parsed)) {
                         continue;
                     }
                     if (const auto color = UIWebText::ParseColor(word)) {
                         parsed.background = *color;
                     } else {
                         return false;
                     }
                 }
                 // The shorthand resets what it does not name
                 r.background = parsed.background ? parsed.background : Optional<UIWebText::WebColor>{Color{0, 0, 0, 0}};
                 r.sprite = parsed.sprite;
                 r.gradient = parsed.gradient;
                 return true;
             }},
            {"border-radius",
             [](Resolved& r, StringView v) {
                 const auto corners = UIWebText::ParseCornerRadii(v);
                 if (corners) {
                     r.cornerRadius = *corners;
                 }
                 return corners.has_value();
             }},
            {"border-top-left-radius", [](Resolved& r, StringView v) { return SetCorner(r, v, &UICorners::topLeft); }},
            {"border-top-right-radius", [](Resolved& r, StringView v) { return SetCorner(r, v, &UICorners::topRight); }},
            {"border-bottom-right-radius", [](Resolved& r, StringView v) { return SetCorner(r, v, &UICorners::bottomRight); }},
            {"border-bottom-left-radius", [](Resolved& r, StringView v) { return SetCorner(r, v, &UICorners::bottomLeft); }},
            {"border",
             [](Resolved& r, StringView v) {
                 // The shorthand sets all three parts: `none` is no border
                 const auto border = UIWebText::ParseBorder(v);
                 if (!border) {
                     return false;
                 }
                 r.borderHidden = false;
                 r.borderWidth = border->width;
                 r.borderColor = border->none ? Optional<UIWebText::WebColor>{} : Optional<UIWebText::WebColor>{border->color};
                 return true;
             }},
            {"border-width",
             [](Resolved& r, StringView v) {
                 const auto width = ParseBorderWidth(v);
                 if (width) {
                     r.borderWidth = *width;
                 }
                 return width.has_value();
             }},
            {"border-color",
             [](Resolved& r, StringView v) {
                 const auto color = UIWebText::ParseColor(v);
                 if (color) {
                     r.borderColor = *color;
                 }
                 return color.has_value();
             }},
            {"border-style",
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(UIWebText::TrimSpace(v));
                 if (lower != "solid" && lower != "none") {
                     return false;
                 }
                 r.borderHidden = lower == "none";
                 return true;
             }},
            {"box-shadow",
             [](Resolved& r, StringView v) {
                 const auto shadows = UIWebText::ParseBoxShadows(v);
                 if (shadows) {
                     r.shadows = *shadows;
                 }
                 return shadows.has_value();
             }},
            {"border-image-slice",
             [](Resolved& r, StringView v) {
                 // Plain numbers in pixels of the sprite (CSS says "pixels" for a bare number); `fill` is the only keyword and is the behavior of a sliced panel anyway
                 String cleaned;
                 for (const StringView word : UIWebText::SplitWords(v)) {
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
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(v);
                 if (lower == "none") {
                     r.pointerEvents = false;
                 } else if (lower == "auto" || lower == "all") {
                     r.pointerEvents = true;
                 } else {
                     return false;
                 }
                 return true;
             }},
            {"opacity", [](Resolved& r, StringView v) { return ParseOpacity(v, r.opacity); }},
            {"transition", [](Resolved& r, StringView v) { return ParseTransition(r, v); }},
            {"-be-transition-scale",
             [](Resolved& r, StringView v) {
                 const auto scale = UIText::ParseFiniteFloat(UIWebText::TrimSpace(v));
                 if (!scale || *scale < 0.0f) {
                     return false;
                 }
                 r.transitionScale = *scale;
                 return true;
             }},
            {"-be-transition-slide",
             [](Resolved& r, StringView v) {
                 const auto words = UIWebText::SplitWords(v);
                 if (words.size() != 2) {
                     return false;
                 }
                 const auto x = UIWebText::ParsePixels(words[0]);
                 const auto y = UIWebText::ParsePixels(words[1]);
                 if (!x || !y) {
                     return false;
                 }
                 r.transitionSlide = std::make_pair(*x, *y);
                 return true;
             }},
            {"-be-transition-input",
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(UIWebText::TrimSpace(v));
                 if (lower != "allow" && lower != "block") {
                     return false;
                 }
                 r.transitionInput = lower == "allow";
                 return true;
             }},
            {"color",
             [](Resolved& r, StringView v) {
                 const auto color = UIWebText::ParseColor(v);
                 if (color) {
                     r.textColor = *color;
                 }
                 return color.has_value();
             }},
            {"font-size",
             [](Resolved& r, StringView v) {
                 const auto pixels = UIWebText::ParsePixels(v);
                 if (!pixels || *pixels <= 0.0f) {
                     return false;
                 }
                 r.fontSize = *pixels;
                 return true;
             }},
            {"font-family",
             [](Resolved& r, StringView v) {
                 // A path to a font file is `font`, a generic family is the default font, any other name is a family of the table of TextConfig.xml
                 const auto families = UIWebText::SplitTopLevel(v, ',');
                 StringView first = families.empty() ? StringView() : families[0];
                 if (first.size() >= 2 && (first.front() == '"' || first.front() == '\'') && first.back() == first.front()) {
                     first = first.substr(1, first.size() - 2);
                 }
                 if (first.empty()) {
                     return false;
                 }
                 r.fontPath.clear();
                 r.fontFamily.clear();
                 if (first.find('/') != StringView::npos || first.find('.') != StringView::npos) {
                     r.fontPath = String(first);
                 } else if (!IsOneOf(UIWebText::ToLower(first), {"sans-serif", "serif", "monospace", "system-ui", "cursive", "fantasy"})) {
                     r.fontFamily = String(first);
                 }
                 return true;
             }},
            {"font-weight",
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(UIWebText::TrimSpace(v));
                 if (lower == "normal") {
                     r.fontWeight = FontWeightNormal;
                 } else if (lower == "bold") {
                     r.fontWeight = FontWeightBold;
                 } else {
                     // `bolder` and `lighter` depend on the parent's weight, which a label has no way to say
                     const auto number = UIText::ParseFiniteFloat(lower);
                     if (!number || *number < FontWeightMin || *number > FontWeightMax || std::fmod(*number, FontWeightStep) != 0.0f) {
                         return false;
                     }
                     r.fontWeight = static_cast<int>(*number);
                 }
                 return true;
             }},
            {"text-shadow",
             [](Resolved& r, StringView v) {
                 if (UIWebText::ToLower(UIWebText::TrimSpace(v)) == "none") {
                     r.textShadow.reset();
                     return true;
                 }
                 // `<x> <y> [blur] <color>`, the colour first or last; the engine draws one hard shadow
                 const auto shadows = UIWebText::SplitTopLevel(v, ',');
                 if (shadows.empty()) {
                     return false;
                 }
                 float numbers[TextShadowMaxNumbers] = {};
                 size_t numberCount = 0;
                 Optional<UIWebText::WebColor> color;
                 for (const StringView word : UIWebText::SplitWords(shadows[0])) {
                     if (const auto pixels = UIWebText::ParsePixels(word)) {
                         if (numberCount == TextShadowMaxNumbers) {
                             return false;
                         }
                         numbers[numberCount++] = *pixels;
                     } else if (const auto parsed = UIWebText::ParseColor(word); parsed && !color) {
                         color = *parsed;
                     } else {
                         return false;
                     }
                 }
                 const bool hasBlur = numberCount == TextShadowMaxNumbers;
                 if (!color || numberCount < TextShadowMinNumbers || (hasBlur && numbers[TextShadowBlurIndex] < 0.0f)) {
                     return false;
                 }
                 r.textShadow = TextShadowValue{numbers[0], numbers[1], *color};
                 if (hasBlur && numbers[TextShadowBlurIndex] != 0.0f) {
                     r.note = "text-shadow blur is not drawn: the shadow is a hard copy of the text";
                 } else if (shadows.size() > 1) {
                     r.note = "only the first of several text shadows is used";
                 }
                 return true;
             }},
            {"-be-text-outline",
             [](Resolved& r, StringView v) {
                 if (UIWebText::ToLower(UIWebText::TrimSpace(v)) == "none") {
                     r.textOutline.reset();
                     return true;
                 }
                 // `<width> <color>`, the colour first or last
                 const auto words = UIWebText::SplitWords(v);
                 if (words.size() != 2) {
                     return false;
                 }
                 const bool widthFirst = UIWebText::ParsePixels(words[0]).has_value();
                 const auto width = UIWebText::ParsePixels(words[widthFirst ? 0 : 1]);
                 const auto color = UIWebText::ParseColor(words[widthFirst ? 1 : 0]);
                 if (!width || !color || *width < 0.0f) {
                     return false;
                 }
                 r.textOutline = TextOutlineValue{*width, *color};
                 return true;
             }},
            {"text-align",
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(v);
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
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(v);
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
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(v);
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
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(v);
                 if (lower != "shrink" && lower != "none") {
                     return false;
                 }
                 r.textFit = lower == "shrink";
                 return true;
             }},
            {"-be-min-font-size",
             [](Resolved& r, StringView v) {
                 const auto pixels = UIWebText::ParsePixels(v);
                 if (!pixels || *pixels <= 0.0f) {
                     return false;
                 }
                 r.minFontSize = *pixels;
                 return true;
             }},
            {"-be-highlight",
             [](Resolved& r, StringView v) {
                 const auto color = UIWebText::ParseColor(v);
                 if (color) {
                     r.highlight = *color;
                 }
                 return color.has_value();
             }},
            {"-be-shape",
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(UIWebText::TrimSpace(v));
                 if (lower != "square" && lower != "round") {
                     return false;
                 }
                 r.partShape = lower == "round" ? UIPartShape::Round : UIPartShape::Square;
                 return true;
             }},
            {"-be-track-color",
             [](Resolved& r, StringView v) {
                 const auto color = UIWebText::ParseColor(v);
                 if (color) {
                     r.trackColor = *color;
                 }
                 return color.has_value();
             }},
            {"-be-fill-color",
             [](Resolved& r, StringView v) {
                 const auto color = UIWebText::ParseColor(v);
                 if (color) {
                     r.fillColor = *color;
                 }
                 return color.has_value();
             }},
            {"-be-thumb-color",
             [](Resolved& r, StringView v) {
                 const auto color = UIWebText::ParseColor(v);
                 if (color) {
                     r.thumbColor = *color;
                 }
                 return color.has_value();
             }},
            {"-be-icon-color",
             [](Resolved& r, StringView v) {
                 const auto color = UIWebText::ParseColor(v);
                 if (color) {
                     r.iconColor = *color;
                 }
                 return color.has_value();
             }},
            {"white-space",
             [](Resolved& r, StringView v) {
                 const String lower = UIWebText::ToLower(v);
                 if (lower != "normal" && lower != "nowrap") {
                     return false;
                 }
                 r.wrapText = lower == "normal";
                 return true;
             }},
            {"line-height",
             [](Resolved& r, StringView v) {
                 if (UIWebText::ToLower(v) == "normal") {
                     r.lineHeightRatio = 1.0f;
                     r.lineHeightPixels.reset();
                     return true;
                 }
                 const StringView trimmed = UIWebText::TrimSpace(v);
                 if (trimmed.size() > 2 && trimmed.substr(trimmed.size() - 2) == "px") {
                     const auto pixels = UIWebText::ParsePixels(trimmed);
                     if (!pixels || *pixels <= 0.0f) {
                         return false;
                     }
                     r.lineHeightPixels = *pixels;
                     r.lineHeightRatio.reset();
                     return true;
                 }
                 const auto ratio = UIText::ParseFiniteFloat(trimmed);
                 if (!ratio || *ratio <= 0.0f) {
                     return false;
                 }
                 r.lineHeightRatio = *ratio;
                 r.lineHeightPixels.reset();
                 return true;
             }},
        };

        // Properties that are accepted and mean nothing to a UI that is not a browser, so they are not a defect
        bool IsIgnorable(StringView property, StringView value) {
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
            void Set(StringView key, String value) {
                for (auto& entry : _entries) {
                    if (entry.first == key) {
                        entry.second = std::move(value);
                        return;
                    }
                }
                _entries.emplace_back(String(key), std::move(value));
            }

            [[nodiscard]] const String* Find(StringView key) const {
                for (const auto& entry : _entries) {
                    if (entry.first == key) {
                        return &entry.second;
                    }
                }
                return nullptr;
            }

            [[nodiscard]] const Vector<std::pair<String, String>>& Entries() const {
                return _entries;
            }

        private:
            Vector<std::pair<String, String>> _entries;
        };

        String Number(float value) {
            return Format("{}", value + 0.0f);  // + 0 turns -0 into 0
        }

        // What a variant can change, and the value an absent attribute stands for
        struct OverridableKey {
            StringView name;
            StringView defaultValue;
        };
        constexpr OverridableKey OverridableKeys[] = {{"visible", "true"}, {"anchor", "TopLeft"},     {"x", "0"},       {"y", "0"}, {"width", "0"},
                                                      {"height", "0"},     {"direction", "Vertical"}, {"fontSize", "0"}};

        const OverridableKey* FindOverridable(StringView key) {
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
            String name;
            int parent = -1;
            Vector<int> children;
            String text;
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
        using DeclarationList = Vector<const UICssDeclaration*>;

        class Compiler {
        public:
            Compiler(StringView html, const UIWebCompiler::StylesheetLoader& loader, UIWebReport& report) : _loader(loader), _report(report) {
                _document = UIHtmlParser::Parse(html, report);
            }

            String Run() {
                CollectStyles();
                CheckDeclarations();

                const auto base = Cascade(-1, CascadeState::None);
                BuildTree(UIHtmlDocument::RootIndex, -1, base);
                _baseAttrs.reserve(_nodes.size());
                for (size_t i = 0; i < _nodes.size(); ++i) {
                    _baseAttrs.push_back(BuildAttrs(static_cast<int>(i), base, &_report));
                }

                Vector<VariantOutput> variants = BuildVariants();
                ApplyStates();
                return Write(variants);
            }

        private:
            struct Override {
                int node = -1;
                Vector<std::pair<String, String>> values;
            };

            struct VariantOutput {
                UIVariantCondition when;
                Vector<Override> overrides;
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
                    if (const String* style = node.FindAttribute("style")) {
                        _inlineStyles[i] = UICssParser::ParseDeclarations(*style, node.line, UICssDeclaration::HtmlSource, _report);
                    }
                }
            }

            void ReadLink(const UIHtmlNode& link) {
                const String* rel = link.FindAttribute("rel");
                const String* href = link.FindAttribute("href");
                if (rel == nullptr || href == nullptr || UIWebText::ToLower(*rel) != "stylesheet") {
                    return;
                }
                Optional<String> css = _loader ? _loader(*href) : std::nullopt;
                if (!css) {
                    _report.AddOnce(HtmlSource, link.line, Format("the style sheet '{}' cannot be read", href->c_str()));
                    return;
                }
                UICssParser::ParseInto(_sheet, *css, Format("css:{}", href->c_str()), 1, _report);
            }

            [[nodiscard]] StringView SourceOf(const UICssDeclaration& declaration) const {
                return declaration.sourceId == UICssDeclaration::HtmlSource ? HtmlSource : StringView(_sheet.sources[declaration.sourceId]);
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
                if (result == ApplyResult::Applied && !scratch.note.empty()) {
                    // Understood, with a caveat: the value counts and the declaration is still reported once
                    if (_report.AddOnce(SourceOf(declaration), declaration.line,
                                        Format("'{}: {}': {}", UIText::Excerpt(declaration.property).c_str(), UIText::Excerpt(declaration.value).c_str(), scratch.note.c_str()))) {
                        ++_report.declarationsUnsupported;
                    }
                }
                if (result == ApplyResult::Applied && declaration.property == "box-shadow") {
                    const auto shadows = UIWebText::ParseBoxShadows(declaration.value);
                    if (shadows && shadows->count > UIWebText::MaxShadowLayers &&
                        _report.AddOnce(SourceOf(declaration), declaration.line,
                                        Format("box-shadow holds at most {} layers (the shadow and the glow of a box): the rest is left out", UIWebText::MaxShadowLayers))) {
                        ++_report.declarationsUnsupported;
                    }
                }
                if (result == ApplyResult::Applied || result == ApplyResult::Ignored) {
                    return;
                }
                const bool hasVar = UIWebText::ToLower(declaration.value).find("var(") != String::npos;
                const String message = result == ApplyResult::UnknownProperty ? Format("property '{}' is not supported", UIText::Excerpt(declaration.property).c_str())
                                       : hasVar                               ? Format("'{}: {}': var() is read only as var(--name) of a theme token, with no fallback value, where a color is read",
                                                                                       UIText::Excerpt(declaration.property).c_str(), UIText::Excerpt(declaration.value).c_str())
                                                : Format("'{}: {}' is not understood or not supported", UIText::Excerpt(declaration.property).c_str(), UIText::Excerpt(declaration.value).c_str());
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
                    const String* id = element.FindAttribute("id");
                    if (id == nullptr || *id != compound.id) {
                        return false;
                    }
                }
                return std::all_of(compound.classes.begin(), compound.classes.end(), [&](const String& className) { return element.HasClass(className); });
            }

            /// Which (part of the selector, level above the element) pairs were tried and did not match. A selector is `a b c` and an ancestor chain is a path, so the same pair is
            /// asked again by every way down to it: without this memory `a a a a a a a b` over a chain of `a` costs the number of ways to choose its parts
            struct SelectorMemory {
                uint64_t failed[UICssParser::kMaxSelectorParts] = {};
            };
            static_assert(UIHtmlParser::kMaxDepth <= 64, "a level above the element is a bit of a 64-bit mask");

            [[nodiscard]] bool MatchesSelector(int node, const UICssSelector& selector) const {
                SelectorMemory memory;
                return MatchesPart(node, 0, selector, selector.compounds.size() - 1, memory);
            }

            /// Whether @p node, @p level steps above the element, matches part @p index of @p selector and the parts to the left of it match above it
            [[nodiscard]] bool MatchesPart(int node, size_t level, const UICssSelector& selector, size_t index, SelectorMemory& memory) const {
                if (!MatchesCompound(node, selector.compounds[index])) {
                    return false;
                }
                if (index == 0) {
                    return true;
                }
                const int parent = _document.nodes[node].parent;
                const auto matchesAbove = [&](int ancestor, size_t ancestorLevel) {
                    const uint64_t bit = uint64_t{1} << ancestorLevel;
                    if ((memory.failed[index - 1] & bit) != 0) {
                        return false;
                    }
                    if (MatchesPart(ancestor, ancestorLevel, selector, index - 1, memory)) {
                        return true;
                    }
                    memory.failed[index - 1] |= bit;
                    return false;
                };
                if (selector.compounds[index].combinator == UICssCombinator::Child) {
                    return parent != UIHtmlDocument::RootIndex && matchesAbove(parent, level + 1);
                }
                size_t ancestorLevel = level + 1;
                for (int ancestor = parent; ancestor != UIHtmlDocument::RootIndex; ancestor = _document.nodes[ancestor].parent, ++ancestorLevel) {
                    if (matchesAbove(ancestor, ancestorLevel)) {
                        return true;
                    }
                }
                return false;
            }

            /// Takes @p steps from the work the cascades of this compile may do (UIWebCompiler::kMaxCascadeWork). False, with one entry in the report, once it is spent
            [[nodiscard]] bool SpendCascadeWork(size_t steps) {
                _cascadeWork += steps;
                if (_cascadeWork <= UIWebCompiler::kMaxCascadeWork) {
                    return true;
                }
                _report.AddOnce(CssSource, 0,
                                Format("the style sheets are too large for this document (more than {} steps of matching): the rest of the styling is left out", UIWebCompiler::kMaxCascadeWork));
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
            [[nodiscard]] Vector<DeclarationList> Cascade(int media, CascadeState state) {
                Vector<DeclarationList> styles(_document.nodes.size());
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

                    Vector<Match> matches;
                    const size_t ruleCount = SpendCascadeWork(_sheet.rules.size() + list.size()) ? _sheet.rules.size() : 0;
                    for (size_t r = 0; r < ruleCount; ++r) {
                        const UICssRule& rule = _sheet.rules[r];
                        if (rule.media != -1 && rule.media != media) {
                            continue;
                        }
                        Optional<uint32_t> specificity;
                        for (const UICssSelector& selector : rule.selectors) {
                            const UICssPseudo pseudo = selector.compounds.back().pseudo;
                            if (pseudo != UICssPseudo::None && pseudo != ToPseudo(state)) {
                                continue;
                            }
                            if (MatchesSelector(static_cast<int>(i), selector)) {
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
                    if (!SpendCascadeWork(matches.size())) {
                        matches.clear();
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
            void GatherText(int node, String& out) const {
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

            [[nodiscard]] static String CollapseWhitespace(const String& raw) {
                String result;
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

            void BuildTree(int htmlParent, int parent, const Vector<DeclarationList>& base) {
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

            /// The kind a `data-widget` value names (not case sensitive); nothing for a name that is not a widget
            [[nodiscard]] static Optional<WebKind> WidgetKindOf(const String& widget) {
                const String lower = UIWebText::ToLower(widget);
                for (const WidgetName& entry : WidgetNames) {
                    if (lower == entry.name) {
                        return entry.kind;
                    }
                }
                return std::nullopt;
            }

            /// The widget an element is, when it says `data-widget` with a name we know and is not a button or an image (those keep their own kind and the attribute is reported)
            [[nodiscard]] static Optional<WebKind> WidgetOf(const UIHtmlNode& html) {
                const String* widget = html.FindAttribute(WidgetAttribute);
                if (widget == nullptr || html.tag == "button" || html.tag == "img") {
                    return std::nullopt;
                }
                return WidgetKindOf(*widget);
            }

            /// A number in an attribute: nothing when there is no attribute or it is not a number that is at least @p minimum
            [[nodiscard]] static Optional<float> ReadNumber(const UIHtmlNode& html, StringView attribute, float minimum = -FLT_MAX) {
                const String* text = html.FindAttribute(attribute);
                if (text == nullptr) {
                    return std::nullopt;
                }
                const auto number = UIText::ParseFiniteFloat(*text);
                return number && *number >= minimum ? number : std::nullopt;
            }

            /// The icon and the tooltip of a button: `data-icon`, `data-icon-position` and `title` anywhere else, or with a value that is not understood, are reported
            void CheckIconAttributes(const UIHtmlNode& html, WebKind kind) {
                const auto report = [&](String message) { _report.AddOnce(HtmlSource, html.line, std::move(message)); };
                const bool button = kind == WebKind::Button;
                if (!button) {
                    if (html.HasAttribute("data-icon")) {
                        report("data-icon only applies to a <button>");
                    }
                    if (html.HasAttribute("data-icon-position")) {
                        report("data-icon-position only applies to a <button>");
                    }
                    if (html.HasAttribute("title")) {
                        report("title only applies to a <button> (it becomes the tooltip): it is left out here");
                    }
                    return;
                }
                const String* icon = html.FindAttribute("data-icon");
                if (icon != nullptr && icon->empty()) {
                    report("data-icon needs the name of a sprite: no icon is drawn");
                }
                if (const String* position = html.FindAttribute("data-icon-position")) {
                    if (icon == nullptr || icon->empty()) {
                        report("data-icon-position has no effect without data-icon");
                    }
                    if (!IconPlacementOf(*position)) {
                        report(Format("data-icon-position=\"{}\" is not supported (\"left\" or \"above\"): the default (left) is kept", position->c_str()));
                    } else if (icon != nullptr && !icon->empty() && StringView(IconPlacementOf(*position)) == "Above") {
                        report("data-icon-position=\"above\" is not supported on a <button> yet: the icon is put left of the caption");
                    }
                }
            }

            /// The placement a `data-icon-position` names
            [[nodiscard]] static const char* IconPlacementOf(const String& position) {
                const String lower = UIWebText::ToLower(UIWebText::TrimSpace(position));
                if (lower == "left") {
                    return "Left";
                }
                return lower == "above" ? "Above" : nullptr;
            }

            /// `data-widget` and the `data-*` attributes of the widgets outside the places that understand them are reported, never dropped in silence
            void CheckWidgetAttributes(int htmlIndex, WebKind kind) {
                const UIHtmlNode& html = _document.nodes[htmlIndex];
                const auto report = [&](String message) { _report.AddOnce(HtmlSource, html.line, std::move(message)); };
                const bool widget = IsWidgetKind(kind);
                if (const String* name = html.FindAttribute(WidgetAttribute)) {
                    if (!widget) {
                        report(WidgetKindOf(*name) ? Format("data-widget=\"{}\" on a <{}> is left out: it belongs on a <div>", name->c_str(), html.tag.c_str())
                                                   : Format("data-widget=\"{}\" is not supported (the widgets are \"segmented\", \"toggle\", \"slider\" and \"progress\")", name->c_str()));
                    }
                }
                if (!widget && html.HasAttribute("data-bind")) {
                    report("data-bind only applies to an element with data-widget=\"segmented\", \"toggle\", \"slider\" or \"progress\"");
                }
                if (kind != WebKind::Button && html.HasAttribute("data-value")) {
                    report("data-value only applies to a <button>");
                }
                if (html.HasAttribute("data-action") && kind != WebKind::Button && kind != WebKind::Toggle && kind != WebKind::Slider) {
                    report("data-action only applies to a <button> or an element with data-widget=\"toggle\" or \"slider\"");
                }
                CheckIconAttributes(html, kind);
                if (kind != WebKind::Toggle) {
                    for (const char* attribute : {"data-on", "data-off"}) {
                        if (html.HasAttribute(attribute)) {
                            report(Format("{} only applies to an element with data-widget=\"toggle\"", attribute));
                        }
                    }
                }
                const bool ranged = kind == WebKind::Slider || kind == WebKind::Progress;
                for (const char* attribute : {"data-min", "data-max"}) {
                    if (!html.HasAttribute(attribute)) {
                        continue;
                    }
                    if (!ranged) {
                        report(Format("{} only applies to an element with data-widget=\"slider\" or \"progress\"", attribute));
                    } else if (!ReadNumber(html, attribute)) {
                        report(Format("{}=\"{}\" is not a number: the default is kept", attribute, html.FindAttribute(attribute)->c_str()));
                    }
                }
                if (html.HasAttribute("data-step")) {
                    if (kind != WebKind::Slider) {
                        report("data-step only applies to an element with data-widget=\"slider\"");
                    } else if (!ReadNumber(html, "data-step", 0.0f)) {
                        report(Format("data-step=\"{}\" is not a number of 0 or more: the default (a continuous slider) is kept", html.FindAttribute("data-step")->c_str()));
                    }
                }
                if (kind == WebKind::Progress && html.HasAttribute("disabled")) {
                    report("disabled does not apply to a progress bar: it takes no input");
                }
                if ((kind == WebKind::Slider || kind == WebKind::Progress) && !IsEmptyContent(htmlIndex)) {
                    report("a slider or progress bar shows no content: what is inside it is left out");
                }
            }

            void AddNode(int htmlIndex, int parent, const Vector<DeclarationList>& base) {
                const UIHtmlNode& html = _document.nodes[htmlIndex];
                WebNode node;
                node.html = htmlIndex;
                node.parent = parent;

                bool hasText = false;
                const bool onlyText = HoldsOnlyText(htmlIndex, hasText);
                const Optional<WebKind> widget = WidgetOf(html);
                if (widget) {
                    node.kind = *widget;
                    if (node.kind == WebKind::Segmented && !Resolve(base[htmlIndex]).flex) {
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
                CheckWidgetAttributes(htmlIndex, node.kind);
                if (IsTextKind(node.kind)) {
                    GatherText(htmlIndex, node.text);
                    node.text = CollapseWhitespace(node.text);
                }
                if (const String* id = html.FindAttribute("id")) {
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
                if (ReadsChildren(_nodes[index].kind)) {
                    BuildTree(htmlIndex, index, base);
                }
            }

            // ---- the attributes ----

            /// The attributes of node @p index when @p styles are in effect. @p report is null when the same node is built again for a variant or a state
            [[nodiscard]] Attrs BuildAttrs(int index, const Vector<DeclarationList>& styles, UIWebReport* report) const {
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
                // A gradient replaces the background color (CSS paints the image over the color); its start is the `color` of the element
                const bool hasFill = r.gradient ? r.gradient->start.IsVisible() : (r.background && r.background->IsVisible());
                if (r.gradient) {
                    attrs.Set("color", UIWebText::FormatWebColor(r.gradient->start));
                } else if (hasFill) {
                    attrs.Set("color", UIWebText::FormatWebColor(*r.background));
                }
                AddBox(attrs, r, node.kind, report);
                AddTransition(attrs, r, report);
                // A panel takes clicks by itself; any other element does when it is a visible plate, and `pointer-events` decides when it is said.
                // A button, a toggle and a slider are interactive already, which is hit-testable, and a progress bar takes no input at all, so they are left alone
                const bool defaultBlocks = node.kind == WebKind::Panel;
                const bool blocks = r.pointerEvents.value_or(defaultBlocks || hasFill);
                const bool ownsInput = node.kind == WebKind::Button || node.kind == WebKind::Toggle || node.kind == WebKind::Slider || node.kind == WebKind::Progress;
                if (!ownsInput && blocks != defaultBlocks) {
                    attrs.Set("blocksInput", blocks ? "true" : "false");
                }
                if (r.pointerEvents && (node.kind == WebKind::Toggle || node.kind == WebKind::Slider || node.kind == WebKind::Progress)) {
                    Note(report, "pointer-events does not apply to a toggle, slider or progress bar: a toggle and a slider take input and a progress bar never does");
                }

                switch (node.kind) {
                    case WebKind::Panel:
                        AddSprite(attrs, r, "sprite");
                        if (r.slice) {
                            attrs.Set("sliceBorder", UIText::FormatEdges(*r.slice));
                        }
                        break;
                    case WebKind::Image:
                        if (const String* src = html.FindAttribute("src")) {
                            attrs.Set("sprite", *src);
                        }
                        AddImageFit(attrs, r, report);
                        break;
                    case WebKind::Label:
                    case WebKind::Button:
                        AddText(attrs, r, node, html, report);
                        break;
                    case WebKind::Segmented:
                        AddBind(attrs, html);
                        AddDisabled(attrs, html);
                        break;
                    case WebKind::Toggle:
                        AddText(attrs, r, node, html, report);
                        AddBind(attrs, html);
                        AddAction(attrs, html);
                        if (const String* on = html.FindAttribute("data-on")) {
                            attrs.Set("onValue", *on);
                        }
                        if (const String* off = html.FindAttribute("data-off")) {
                            attrs.Set("offValue", *off);
                        }
                        AddDisabled(attrs, html);
                        break;
                    case WebKind::Slider:
                        AddBind(attrs, html);
                        AddAction(attrs, html);
                        AddNumber(attrs, html, "data-min", "minValue");
                        AddNumber(attrs, html, "data-max", "maxValue");
                        AddNumber(attrs, html, "data-step", "step", 0.0f);
                        AddDisabled(attrs, html);
                        break;
                    case WebKind::Progress:
                        AddBind(attrs, html);
                        AddNumber(attrs, html, "data-min", "minValue");
                        AddNumber(attrs, html, "data-max", "maxValue");
                        break;
                    case WebKind::Group:
                        break;
                }
                AddWidgetColors(attrs, r, node.kind, report);
                if (node.kind == WebKind::Toggle) {
                    AddDerivedSwitchColors(attrs, r, report);
                }
                return attrs;
            }

            /// `opacity` and `transition`: the opacity and the state duration of the element, and the fields of the `UITransitionComponent` the writers make of the transition keys
            static void AddTransition(Attrs& attrs, const Resolved& r, UIWebReport* report) {
                if (r.opacity && *r.opacity != 1.0f) {
                    attrs.Set("opacity", Number(*r.opacity));
                }
                const TransitionValue* transition = r.transition ? &*r.transition : nullptr;
                const bool slides = transition != nullptr && transition->transform;
                if (!slides && (r.transitionScale || r.transitionSlide)) {
                    Note(report, "-be-transition-scale and -be-transition-slide only apply with `transform` in transition");
                }
                if (transition == nullptr || (!transition->fade && !slides)) {
                    if (r.transitionInput) {
                        Note(report, "-be-transition-input only applies with `opacity` or `transform` in transition");
                    }
                }
                if (transition == nullptr) {
                    return;
                }
                if (transition->state && *transition->state > 0.0f) {
                    attrs.Set("stateDuration", Number(*transition->state));
                }
                if (slides && !r.transitionScale && !r.transitionSlide) {
                    Note(report, "a `transform` transition needs -be-transition-scale or -be-transition-slide to say where it starts");
                }
                const float fade = transition->fade.value_or(0.0f);
                const float transform = slides && (r.transitionScale || r.transitionSlide) ? *transition->transform : 0.0f;
                const float duration = fade > 0.0f && !(transform > 0.0f && transition->transformFirst) ? fade : transform;
                if (duration <= 0.0f) {
                    return;
                }
                attrs.Set("duration", Number(duration));
                if (fade <= 0.0f) {
                    attrs.Set("fade", "false");
                }
                if (transform > 0.0f) {
                    if (r.transitionScale) {
                        attrs.Set("scaleFrom", Number(*r.transitionScale));
                    }
                    if (r.transitionSlide) {
                        attrs.Set("slideX", Number(r.transitionSlide->first));
                        attrs.Set("slideY", Number(r.transitionSlide->second));
                    }
                }
                if (transition->ease != EaseType::QuadOut) {
                    attrs.Set("ease", Format("{}", transition->ease));
                }
                if (r.transitionInput.value_or(false)) {
                    attrs.Set("inputDuringTransition", "true");
                }
            }

            static void AddBind(Attrs& attrs, const UIHtmlNode& html) {
                if (const String* bind = html.FindAttribute("data-bind")) {
                    attrs.Set("bind", *bind);
                }
            }

            /// `data-action`, or `action` as on a button
            static void AddAction(Attrs& attrs, const UIHtmlNode& html) {
                const String* action = html.FindAttribute("data-action");
                if (action == nullptr) {
                    action = html.FindAttribute("action");
                }
                if (action != nullptr) {
                    attrs.Set("action", *action);
                }
            }

            static void AddDisabled(Attrs& attrs, const UIHtmlNode& html) {
                if (html.HasAttribute("disabled")) {
                    attrs.Set("disabled", "true");
                }
            }

            /// A number attribute written as a number (`1.0` and `1` both give `1`); one that is not a number (or is below @p minimum) is left out and was reported when the node was added
            static void AddNumber(Attrs& attrs, const UIHtmlNode& html, StringView attribute, StringView key, float minimum = -FLT_MAX) {
                if (const auto number = ReadNumber(html, attribute, minimum)) {
                    attrs.Set(key, Number(*number));
                }
            }

            /// The box attributes: corners, border, gradient, shadow, glow, highlight and the shape of the parts; each only when set
            static void AddBox(Attrs& attrs, const Resolved& r, WebKind kind, UIWebReport* report) {
                if (r.cornerRadius && !r.cornerRadius->IsZero()) {
                    attrs.Set("cornerRadius", UIText::FormatCorners(*r.cornerRadius));
                }
                const bool borderWanted = !r.borderHidden && r.borderWidth && *r.borderWidth > 0.0f;
                if (borderWanted && r.borderColor) {
                    attrs.Set("borderWidth", Number(*r.borderWidth));
                    attrs.Set("borderColor", UIWebText::FormatWebColor(*r.borderColor));
                } else if (borderWanted) {
                    Note(report, "a border needs a color: `border: 1px solid <color>` or `border-color`");
                }
                if (r.gradient) {
                    attrs.Set("gradientDirection", r.gradient->direction == UIGradientDirection::Horizontal ? "Horizontal" : "Vertical");
                    attrs.Set("gradientColor", UIWebText::FormatWebColor(r.gradient->end));
                }
                if (r.shadows && r.shadows->count > 0) {
                    // CSS paints the first layer on top, the engine paints the glow over the shadow
                    if (r.shadows->count == 1) {
                        attrs.Set("shadow", UIText::FormatShadow(r.shadows->layers[0], r.shadows->tokens[0]));
                    } else {
                        attrs.Set("glow", UIText::FormatShadow(r.shadows->layers[0], r.shadows->tokens[0]));
                        attrs.Set("shadow", UIText::FormatShadow(r.shadows->layers[1], r.shadows->tokens[1]));
                    }
                }
                if (r.highlight) {
                    attrs.Set("highlight", UIWebText::FormatWebColor(*r.highlight));
                }
                if (r.partShape) {
                    if (kind == WebKind::Toggle || kind == WebKind::Slider || kind == WebKind::Progress) {
                        if (*r.partShape == UIPartShape::Round) {
                            attrs.Set("partShape", "Round");
                        }
                    } else {
                        Note(report, "-be-shape applies to a toggle, slider or progress bar only");
                    }
                }
            }

            /// The three engine-only colors of the widgets; on any other element they are reported
            static void AddWidgetColors(Attrs& attrs, const Resolved& r, WebKind kind, UIWebReport* report) {
                const bool track = kind == WebKind::Toggle || kind == WebKind::Slider || kind == WebKind::Progress;
                const bool thumb = kind == WebKind::Toggle || kind == WebKind::Slider;
                if (r.trackColor) {
                    if (track) {
                        attrs.Set("trackColor", UIWebText::FormatWebColor(*r.trackColor));
                    } else {
                        Note(report, "-be-track-color applies to a toggle, slider or progress bar only");
                    }
                }
                if (r.fillColor) {
                    if (track) {
                        attrs.Set("fillColor", UIWebText::FormatWebColor(*r.fillColor));
                    } else {
                        Note(report, "-be-fill-color applies to a toggle, slider or progress bar only");
                    }
                }
                if (r.iconColor && kind != WebKind::Button) {
                    Note(report, "-be-icon-color applies to a button only");
                }
                if (r.thumbColor) {
                    if (thumb) {
                        attrs.Set("thumbColor", UIWebText::FormatWebColor(*r.thumbColor));
                    } else {
                        Note(report, "-be-thumb-color applies to a toggle or slider only (a progress bar has no thumb)");
                    }
                }
            }

            /// The off track and the thumb of a toggle followed the colour of its own text; the switch is a child that cannot see the caption, so the colours are written when the text
            /// is not white (`tools/migrate/steps/_ui_value.py` does the same). A colour the author gave stays; a theme token cannot be given an alpha, so it is the thumb only
            static void AddDerivedSwitchColors(Attrs& attrs, const Resolved& r, UIWebReport* report) {
                if (!r.textColor) {
                    return;
                }
                const auto given = [](const Optional<UIWebText::WebColor>& color) { return color && color->IsVisible(); };
                if (r.textColor->IsToken()) {
                    if (!given(r.trackColor)) {
                        Note(report, "a toggle whose text color is a theme token needs -be-track-color: its off track cannot be derived from a token");
                    }
                    if (!given(r.thumbColor)) {
                        attrs.Set("thumbColor", UIWebText::FormatWebColor(*r.textColor));
                    }
                    return;
                }
                if (ColorsEqual(*r.textColor, CaptionWhite)) {
                    return;
                }
                if (!given(r.trackColor)) {
                    attrs.Set("trackColor", UIText::FormatColor(WithAlpha(*r.textColor, UISwitchComponent::kOffTrackAlpha)));
                }
                if (!given(r.thumbColor)) {
                    attrs.Set("thumbColor", UIWebText::FormatWebColor(*r.textColor));
                }
            }

            static void AddSprite(Attrs& attrs, const Resolved& r, StringView key) {
                if (!r.sprite.empty()) {
                    attrs.Set(key, r.sprite);
                }
            }

            static void Note(UIWebReport* report, String message) {
                if (report != nullptr) {
                    report->AddOnce(CssSource, 0, std::move(message));
                }
            }

            static void AddPosition(Attrs& attrs, const Resolved& r, UIWebReport* report) {
                const bool allSides = r.left && r.top && r.right && r.bottom;
                String anchor = r.anchor;
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
                const String direction = r.flexDirection.empty() ? String("Horizontal") : r.flexDirection;
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

            static void AddText(Attrs& attrs, const Resolved& r, const WebNode& node, const UIHtmlNode& html, UIWebReport* report) {
                attrs.Set("text", node.text);
                if (!r.fontPath.empty()) {
                    attrs.Set("font", r.fontPath);
                } else if (!r.fontFamily.empty()) {
                    attrs.Set("fontFamily", r.fontFamily);
                }
                if (r.fontWeight && *r.fontWeight != FontWeightNormal) {
                    attrs.Set("fontWeight", Number(static_cast<float>(*r.fontWeight)));
                }
                if (r.textOutline && r.textOutline->width > 0.0f) {
                    attrs.Set("textOutline", UIText::FormatTextOutline(r.textOutline->width, r.textOutline->color, r.textOutline->color.token));
                }
                if (r.textShadow) {
                    attrs.Set("textShadow", UIText::FormatTextShadow(r.textShadow->offsetX, r.textShadow->offsetY, r.textShadow->color, r.textShadow->color.token));
                }
                if (r.textColor) {
                    attrs.Set("textColor", UIWebText::FormatWebColor(*r.textColor));
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
                    AddAction(attrs, html);
                    if (const String* value = html.FindAttribute("data-value")) {
                        attrs.Set("value", *value);
                    }
                    AddDisabled(attrs, html);
                    AddIcon(attrs, r, html, report);
                }
            }

            /// The icon, its placement and its colour, and the tooltip of a button; each only when set (a bad placement was reported when the node was added; a colour without an icon is reported
            /// here). A button has no placement: its icon is always left of the caption
            static void AddIcon(Attrs& attrs, const Resolved& r, const UIHtmlNode& html, UIWebReport* report) {
                const String* icon = html.FindAttribute("data-icon");
                if (icon != nullptr && !icon->empty()) {
                    attrs.Set("icon", *icon);
                    if (r.iconColor) {
                        attrs.Set("iconTint", UIWebText::FormatWebColor(*r.iconColor));
                    }
                } else if (r.iconColor) {
                    Note(report, "-be-icon-color has no effect on a button without data-icon");
                }
                if (const String* tooltip = html.FindAttribute("title"); tooltip != nullptr && !tooltip->empty()) {
                    attrs.Set("tooltip", *tooltip);
                }
            }

            // ---- variants and states ----

            [[nodiscard]] Vector<VariantOutput> BuildVariants() {
                Vector<VariantOutput> variants;
                for (size_t m = 0; m < _sheet.medias.size(); ++m) {
                    const UICssMedia& media = _sheet.medias[m];
                    const bool used = std::any_of(_sheet.rules.begin(), _sheet.rules.end(), [&](const UICssRule& rule) { return rule.media == static_cast<int>(m); });
                    if (!used) {
                        continue;
                    }
                    const auto styles = Cascade(static_cast<int>(m), CascadeState::None);
                    Vector<Override> overrides;
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
                auto consider = [&](StringView key) {
                    const OverridableKey* overridable = FindOverridable(key);
                    const String* before = base.Find(key);
                    const String* after = changed.Find(key);
                    const StringView beforeText = before ? StringView(*before) : (overridable ? overridable->defaultValue : StringView());
                    const StringView afterText = after ? StringView(*after) : (overridable ? overridable->defaultValue : StringView());
                    if (beforeText == afterText) {
                        return;
                    }
                    if (overridable == nullptr) {
                        _report.AddOnce(_sheet.sources[media.sourceId], media.line,
                                        Format("@media changes '{}': a variant can change only visibility, anchor, offset, size, direction and font size", String(key).c_str()));
                        return;
                    }
                    entry.values.emplace_back(String(key), String(afterText));
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
                            const String* before = base.Find(key);
                            if (before != nullptr && *before == value) {
                                continue;
                            }
                            const WebKind kind = _nodes[i].kind;
                            if (kind == WebKind::Button && key == "color") {
                                base.Set(colorKey, value);
                            } else if (kind == WebKind::Button && key == "gradientColor") {
                                _report.AddOnce(CssSource, 0, "the far end of a state gradient is derived from its start color, as the states of a <button> are: it is left out");
                            } else if (kind == WebKind::Toggle || kind == WebKind::Slider || kind == WebKind::Progress) {
                                _report.AddOnce(CssSource, 0,
                                                Format("a state rule does not apply to a toggle, slider or progress bar: its states are derived from its colors ('{}' of a <{}> is left out)",
                                                       key.c_str(), _document.nodes[_nodes[i].html].tag.c_str()));
                            } else {
                                _report.AddOnce(CssSource, 0,
                                                Format("a state rule can change only the background of a <button>, not '{}' of a <{}>", key.c_str(), _document.nodes[_nodes[i].html].tag.c_str()));
                            }
                        }
                    }
                }
            }

            // ---- the output ----

            [[nodiscard]] String UniqueName(const String& tag) {
                // The names tried before are taken (an id of the document may be `div-7`), so counting goes on where the last call stopped: one call per element must not
                // start at 1 again
                int& suffix = _nextNameSuffix[tag];
                while (true) {
                    String candidate = Format("{}-{}", tag.c_str(), ++suffix);
                    if (_usedNames.insert(candidate).second) {
                        return candidate;
                    }
                }
            }

            /// A button with an icon and a text: the icon is left of the caption, in a child each
            [[nodiscard]] bool IsIconLeft(int index) const {
                const Attrs& attrs = _baseAttrs[index];
                const String* text = attrs.Find("text");
                return attrs.Find("icon") != nullptr && text != nullptr && !text->empty();
            }

            static void AddComponentAttribute(pugi::xml_node component, const Attrs& attrs, StringView key) {
                if (const String* value = attrs.Find(key)) {
                    component.append_attribute(String(key).c_str()) = value->c_str();
                }
            }

            /// The `UITransitionComponent` of an element, from the transition keys of its attributes; none when it has no duration
            static void AddTransitionComponent(pugi::xml_node element, const Attrs& attrs) {
                if (attrs.Find("duration") == nullptr) {
                    return;
                }
                pugi::xml_node components = element.child("components");
                if (!components) {
                    components = element.append_child("components");
                }
                pugi::xml_node component = components.append_child("component");
                component.append_attribute("type") = "UITransitionComponent";
                for (const StringView key : TransitionKeys) {
                    AddComponentAttribute(component, attrs, key);
                }
            }

            /// The text of a control as a `UITextComponent`, with what a button or a toggle does by itself (@p defaults: a button centres its text, a toggle's caption centres it vertically and does
            /// not wrap);
            /// @p colorOnly writes the colour alone (the bar of a selected icon-only button takes it)
            template <size_t N>
            static void AddTextComponent(pugi::xml_node components, const Attrs& attrs, bool colorOnly, const ButtonTextDefault (&defaults)[N]) {
                if (colorOnly && attrs.Find("textColor") == nullptr) {
                    return;
                }
                pugi::xml_node component = components.append_child("component");
                component.append_attribute("type") = "UITextComponent";
                for (const StringView key : TextComponentKeys) {
                    if (colorOnly && key != "textColor") {
                        continue;
                    }
                    if (attrs.Find(key) != nullptr) {
                        AddComponentAttribute(component, attrs, key);
                        continue;
                    }
                    const auto* centring = std::find_if(std::begin(defaults), std::end(defaults), [&](const ButtonTextDefault& entry) { return entry.key == key; });
                    if (!colorOnly && centring != std::end(defaults)) {
                        component.append_attribute(String(key).c_str()) = String(centring->value).c_str();
                    }
                }
            }

            static void AddTextComponent(pugi::xml_node components, const Attrs& attrs, bool colorOnly) {
                AddTextComponent(components, attrs, colorOnly, ButtonTextDefaults);
            }

            /// One child element that holds one component; its name is the button's with @p suffix, or none when the button has none
            pugi::xml_node AppendPartChild(pugi::xml_node children, const WebNode& node, StringView suffix) {
                pugi::xml_node element = children.append_child("element");
                element.append_attribute("type") = "UIElement";
                if (!node.name.empty()) {
                    element.append_attribute("name") = Format("{}{}", node.name.c_str(), String(suffix).c_str()).c_str();
                }
                ++_report.elements;
                return element;
            }

            /// A button as an element with the components of a button: the text, always a clicker, a tooltip, and for an icon one child (the image) or two (the image and the caption).
            /// `ui-controls.md` ("Components") has the forms; `tools/migrate/steps/s0001_ui_button_to_components.py` writes the same ones
            void WriteButton(pugi::xml_node parent, int index) {
                const WebNode& node = _nodes[index];
                const Attrs& attrs = _baseAttrs[index];
                const bool hasIcon = attrs.Find("icon") != nullptr;
                const bool iconLeft = IsIconLeft(index);

                pugi::xml_node element = parent.append_child("element");
                element.append_attribute("type") = TypeName(WebKind::Group);
                if (!node.name.empty()) {
                    element.append_attribute("name") = node.name.c_str();
                }
                bool ownStack = false;
                for (const auto& [key, value] : attrs.Entries()) {
                    const bool moved =
                        IsKeyOf(TextComponentKeys, key) || IsKeyOf(ClickerKeys, key) || IsKeyOf(IconKeys, key) || IsKeyOf(TransitionKeys, key) || key == "tooltip" || (iconLeft && key == "fontSize");
                    // The children of an icon button are a stack of their own, so its own stack attributes would fight them
                    const bool stack = hasIcon && IsKeyOf(StackKeys, key);
                    ownStack |= stack;
                    if (!moved && !stack) {
                        element.append_attribute(key.c_str()) = value.c_str();
                    }
                }
                if (ownStack) {
                    _report.AddOnce(HtmlSource, _document.nodes[node.html].line,
                                    "a <button> with data-icon lays its icon and caption out itself: display: flex, gap, padding, flex-direction, flex-wrap and align-items on it are left out");
                }
                if (iconLeft) {
                    element.append_attribute("layout") = "Stack";
                    element.append_attribute("direction") = "Horizontal";
                    element.append_attribute("padding") = IconPadding;
                    element.append_attribute("spacing") = IconGap;
                    element.append_attribute("crossAlign") = "Center";
                }
                ++_report.elements;

                pugi::xml_node components = element.append_child("components");
                if (!hasIcon) {
                    AddTextComponent(components, attrs, false);
                } else if (!iconLeft) {
                    AddTextComponent(components, attrs, true);
                }
                pugi::xml_node clicker = components.append_child("component");
                clicker.append_attribute("type") = "UIInputClicker";
                for (const StringView key : ClickerKeys) {
                    AddComponentAttribute(clicker, attrs, key);
                }
                if (const String* tooltip = attrs.Find("tooltip")) {
                    pugi::xml_node component = components.append_child("component");
                    component.append_attribute("type") = "UITooltipComponent";
                    component.append_attribute("text") = tooltip->c_str();
                }
                AddTransitionComponent(element, attrs);
                if (!hasIcon) {
                    return;
                }

                // The tint of the icon is `-be-icon-color`, else the colour of the text
                const String* tint = attrs.Find("iconTint") != nullptr ? attrs.Find("iconTint") : attrs.Find("textColor");
                pugi::xml_node children = element.append_child("children");
                pugi::xml_node icon = AppendPartChild(children, node, IconSuffix);
                if (iconLeft) {
                    icon.append_attribute("height") = IconCaptionSide;
                } else {
                    icon.append_attribute("anchor") = "Center";
                    icon.append_attribute("width") = IconOnlySide;
                    icon.append_attribute("height") = IconOnlySide;
                }
                icon.append_attribute("aspectRatio") = "1";
                pugi::xml_node image = icon.append_child("components").append_child("component");
                image.append_attribute("type") = "UIImageComponent";
                image.append_attribute("sprite") = attrs.Find("icon")->c_str();
                if (tint != nullptr) {
                    image.append_attribute("tint") = tint->c_str();
                }
                if (iconLeft) {
                    pugi::xml_node caption = AppendPartChild(children, node, CaptionSuffix);
                    caption.append_attribute("flex") = "1";
                    caption.append_attribute("height") = "100%";
                    if (const String* fontSize = attrs.Find("fontSize")) {
                        caption.append_attribute("fontSize") = fontSize->c_str();
                    }
                    AddTextComponent(caption.append_child("components"), attrs, false);
                }
            }

            /// A toggle as a row: an element with a clicker (`disabled`) and the toggle component (`bind`, `action`, `onValue`, `offValue`), laid out as a horizontal stack of the caption and the
            /// switch, each a child. `ui-controls.md` ("Components") has the form; `tools/migrate/steps/s0003_ui_toggle_slider_to_components.py` writes the same one
            void WriteToggle(pugi::xml_node parent, int index) {
                const WebNode& node = _nodes[index];
                const Attrs& attrs = _baseAttrs[index];

                pugi::xml_node element = parent.append_child("element");
                element.append_attribute("type") = TypeName(WebKind::Group);
                if (!node.name.empty()) {
                    element.append_attribute("name") = node.name.c_str();
                }
                bool ownStack = false;
                for (const auto& [key, value] : attrs.Entries()) {
                    const bool moved =
                        IsKeyOf(TextComponentKeys, key) || IsKeyOf(ToggleKeys, key) || IsKeyOf(SwitchKeys, key) || IsKeyOf(TransitionKeys, key) || key == "disabled" || key == "fontSize";
                    const bool stack = IsKeyOf(StackKeys, key);  // the row is a stack of its own
                    ownStack |= stack;
                    if (!moved && !stack) {
                        element.append_attribute(key.c_str()) = value.c_str();
                    }
                }
                if (ownStack) {
                    _report.AddOnce(HtmlSource, _document.nodes[node.html].line,
                                    "a toggle lays its caption and switch out itself: display: flex, gap, padding, flex-direction, flex-wrap and align-items on it are left out");
                }
                element.append_attribute("layout") = "Stack";
                element.append_attribute("direction") = "Horizontal";
                element.append_attribute("spacing") = ToggleGap;
                element.append_attribute("crossAlign") = "Center";
                ++_report.elements;

                pugi::xml_node components = element.append_child("components");
                pugi::xml_node clicker = components.append_child("component");
                clicker.append_attribute("type") = "UIInputClicker";
                AddComponentAttribute(clicker, attrs, "disabled");
                pugi::xml_node toggle = components.append_child("component");
                toggle.append_attribute("type") = "UIToggleComponent";
                for (const StringView key : ToggleKeys) {
                    AddComponentAttribute(toggle, attrs, key);
                }
                AddTransitionComponent(element, attrs);

                pugi::xml_node children = element.append_child("children");
                pugi::xml_node caption = AppendPartChild(children, node, CaptionSuffix);
                caption.append_attribute("flex") = "1";
                caption.append_attribute("height") = "100%";
                if (const String* fontSize = attrs.Find("fontSize")) {
                    caption.append_attribute("fontSize") = fontSize->c_str();
                }
                pugi::xml_node captionComponents = caption.append_child("components");
                AddTextComponent(captionComponents, attrs, false, ToggleTextDefaults);
                captionComponents.last_child().append_attribute(FadeWhenDisabledKey) = "true";
                pugi::xml_node track = AppendPartChild(children, node, SwitchSuffix);
                track.append_attribute("height") = "100%";
                track.append_attribute("aspectRatio") = Number(UISwitchComponent::kAreaAspect).c_str();
                pugi::xml_node look = track.append_child("components").append_child("component");
                look.append_attribute("type") = "UISwitchComponent";
                for (const StringView key : SwitchKeys) {
                    AddComponentAttribute(look, attrs, key);
                }
            }

            /// A slider as one element with a clicker that leaves its plate still (`disabled`) and the slider component. The same form as the step
            void WriteSlider(pugi::xml_node parent, int index) {
                const WebNode& node = _nodes[index];
                const Attrs& attrs = _baseAttrs[index];

                pugi::xml_node element = parent.append_child("element");
                element.append_attribute("type") = TypeName(WebKind::Group);
                if (!node.name.empty()) {
                    element.append_attribute("name") = node.name.c_str();
                }
                for (const auto& [key, value] : attrs.Entries()) {
                    if (!IsKeyOf(SliderKeys, key) && !IsKeyOf(SwitchKeys, key) && !IsKeyOf(TransitionKeys, key) && key != "disabled") {
                        element.append_attribute(key.c_str()) = value.c_str();
                    }
                }
                ++_report.elements;

                pugi::xml_node components = element.append_child("components");
                pugi::xml_node clicker = components.append_child("component");
                clicker.append_attribute("type") = "UIInputClicker";
                clicker.append_attribute("stateLook") = "false";
                AddComponentAttribute(clicker, attrs, "disabled");
                pugi::xml_node slider = components.append_child("component");
                slider.append_attribute("type") = "UISliderComponent";
                for (const StringView key : SliderKeys) {
                    AddComponentAttribute(slider, attrs, key);
                }
                for (const StringView key : SwitchKeys) {
                    AddComponentAttribute(slider, attrs, key);
                }
                AddTransitionComponent(element, attrs);
            }

            void WriteNode(pugi::xml_node parent, int index) {
                const WebNode& node = _nodes[index];
                if (node.kind == WebKind::Button) {
                    WriteButton(parent, index);
                    return;
                }
                if (node.kind == WebKind::Toggle) {
                    WriteToggle(parent, index);
                    return;
                }
                if (node.kind == WebKind::Slider) {
                    WriteSlider(parent, index);
                    return;
                }
                pugi::xml_node element = parent.append_child("element");
                element.append_attribute("type") = TypeName(node.kind);
                if (!node.name.empty()) {
                    element.append_attribute("name") = node.name.c_str();
                }
                const Attrs& attrs = _baseAttrs[index];
                const bool isText = node.kind == WebKind::Label;
                const bool isImage = node.kind == WebKind::Image;
                for (const auto& [key, value] : attrs.Entries()) {
                    if (!(isText && IsKeyOf(TextComponentKeys, key)) && !(isImage && IsKeyOf(ImageKeys, key)) && !IsKeyOf(TransitionKeys, key)) {
                        element.append_attribute(key.c_str()) = value.c_str();
                    }
                }
                ++_report.elements;
                if (isText || isImage) {
                    pugi::xml_node component = element.append_child("components").append_child("component");
                    component.append_attribute("type") = isText ? "UITextComponent" : "UIImageComponent";
                    if (isText) {
                        for (const StringView key : TextComponentKeys) {
                            AddComponentAttribute(component, attrs, key);
                        }
                    } else {
                        for (const StringView key : ImageKeys) {
                            AddComponentAttribute(component, attrs, key);
                        }
                    }
                }
                AddTransitionComponent(element, attrs);
                if (!node.children.empty()) {
                    pugi::xml_node children = element.append_child("children");
                    for (const int child : node.children) {
                        WriteNode(children, child);
                    }
                }
            }

            [[nodiscard]] String Write(const Vector<VariantOutput>& variants) {
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
                        const String when = variant.when.ToText();
                        if (!when.empty()) {
                            variantNode.append_attribute("when") = when.c_str();
                        }
                        pugi::xml_node overrides = variantNode.append_child("overrides");
                        for (const Override& entry : variant.overrides) {
                            pugi::xml_node set = overrides.append_child("set");
                            set.append_attribute("element") = _nodes[entry.node].name.c_str();
                            // The font size of an icon-left button and of a toggle is the caption's
                            const bool captionHolds = IsIconLeft(entry.node) || _nodes[entry.node].kind == WebKind::Toggle;
                            pugi::xml_node captionSet;
                            for (const auto& [key, value] : entry.values) {
                                if (captionHolds && key == "fontSize") {
                                    if (!captionSet) {
                                        captionSet = overrides.append_child("set");
                                        captionSet.append_attribute("element") = Format("{}{}", _nodes[entry.node].name.c_str(), String(CaptionSuffix).c_str()).c_str();
                                    }
                                    captionSet.append_attribute(key.c_str()) = value.c_str();
                                } else {
                                    set.append_attribute(key.c_str()) = value.c_str();
                                }
                            }
                            if (captionSet && set.attributes_begin() != set.attributes_end() && ++set.attributes_begin() == set.attributes_end()) {
                                overrides.remove_child(set);
                            }
                        }
                    }
                }

                struct StringWriter : pugi::xml_writer {
                    String text;
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
            Vector<Vector<UICssDeclaration>> _inlineStyles;
            Vector<WebNode> _nodes;
            Vector<Attrs> _baseAttrs;
            eastl::hash_set<String> _usedNames;
            eastl::hash_map<String, int> _nextNameSuffix;  ///< per tag, where UniqueName goes on counting
            size_t _cascadeWork = 0;
        };

    }  // namespace

    String UIWebCompiler::Compile(StringView html, const StylesheetLoader& loadStylesheet, UIWebReport& report) {
        return Compiler(html, loadStylesheet, report).Run();
    }

}  // namespace BECore
