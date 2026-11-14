#include "UIWebText.h"

#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UIValueText.h>

namespace BECore::UIWebText {

    namespace {

        constexpr int HexBase = 16;
        constexpr unsigned char OpaqueAlpha = 255;
        constexpr float ByteMax = 255.0f;
        constexpr float PercentScale = 0.01f;
        constexpr size_t ShortHexLength = 3;
        constexpr size_t ShortHexAlphaLength = 4;
        constexpr size_t LongHexLength = 6;
        constexpr size_t LongHexAlphaLength = 8;
        constexpr StringView RgbName = "rgb";
        constexpr StringView RgbaName = "rgba";
        constexpr StringView UrlName = "url";
        constexpr StringView GradientName = "linear-gradient";
        constexpr StringView TokenPrefix = "--";
        constexpr StringView NoneKeyword = "none";
        constexpr StringView SolidKeyword = "solid";
        constexpr StringView InsetKeyword = "inset";
        constexpr size_t MaxCornerValues = 4;
        constexpr size_t MinShadowLengths = 2;
        constexpr size_t MaxShadowLengths = 4;
        constexpr size_t ShadowBlurIndex = 2;
        constexpr size_t ShadowSpreadIndex = 3;
        constexpr float AngleTop = 0.0f;
        constexpr float AngleRight = 90.0f;
        constexpr float AngleBottom = 180.0f;
        constexpr float AngleLeft = 270.0f;

        struct NamedColor {
            StringView name;
            Color color;
        };

        // The names a screen is likely to use; the rest is written as hex
        constexpr NamedColor NamedColors[] = {
            {"transparent", {0, 0, 0, 0}},  {"black", {0, 0, 0, 255}},       {"white", {255, 255, 255, 255}},  {"red", {255, 0, 0, 255}},
            {"green", {0, 128, 0, 255}},    {"blue", {0, 0, 255, 255}},      {"yellow", {255, 255, 0, 255}},   {"orange", {255, 165, 0, 255}},
            {"gray", {128, 128, 128, 255}}, {"grey", {128, 128, 128, 255}},  {"silver", {192, 192, 192, 255}}, {"purple", {128, 0, 128, 255}},
            {"cyan", {0, 255, 255, 255}},   {"magenta", {255, 0, 255, 255}}, {"lime", {0, 255, 0, 255}},       {"navy", {0, 0, 128, 255}},
        };

        Optional<unsigned char> ParseHexDigits(StringView digits) {
            unsigned int value = 0;
            const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, HexBase);
            if (error != std::errc{} || end != digits.data() + digits.size()) {
                return std::nullopt;
            }
            return static_cast<unsigned char>(value);
        }

        Optional<Color> ParseHexColor(StringView digits) {
            if (digits.size() == ShortHexLength || digits.size() == ShortHexAlphaLength) {
                // #rgb and #rgba: each digit is doubled
                String expanded;
                for (const char c : digits) {
                    expanded.push_back(c);
                    expanded.push_back(c);
                }
                return ParseHexColor(expanded);
            }
            if (digits.size() != LongHexLength && digits.size() != LongHexAlphaLength) {
                return std::nullopt;
            }
            const auto r = ParseHexDigits(digits.substr(0, 2));
            const auto g = ParseHexDigits(digits.substr(2, 2));
            const auto b = ParseHexDigits(digits.substr(4, 2));
            const auto a = digits.size() == LongHexAlphaLength ? ParseHexDigits(digits.substr(6, 2)) : Optional<unsigned char>{OpaqueAlpha};
            if (!r || !g || !b || !a) {
                return std::nullopt;
            }
            return Color{*r, *g, *b, *a};
        }

        /// A channel as a number 0 to 255 or a percent
        Optional<unsigned char> ParseChannel(StringView text) {
            text = TrimSpace(text);
            bool percent = false;
            if (!text.empty() && text.back() == '%') {
                percent = true;
                text.remove_suffix(1);
            }
            const auto value = UIText::ParseFiniteFloat(text);
            if (!value) {
                return std::nullopt;
            }
            const float scaled = percent ? *value * PercentScale * ByteMax : *value;
            return static_cast<unsigned char>(std::clamp(scaled + 0.5f, 0.0f, ByteMax));
        }

        /// An alpha as a number 0 to 1 or a percent
        Optional<unsigned char> ParseAlpha(StringView text) {
            text = TrimSpace(text);
            bool percent = false;
            if (!text.empty() && text.back() == '%') {
                percent = true;
                text.remove_suffix(1);
            }
            const auto value = UIText::ParseFiniteFloat(text);
            if (!value) {
                return std::nullopt;
            }
            const float unit = percent ? *value * PercentScale : *value;
            return static_cast<unsigned char>(std::clamp(unit * ByteMax + 0.5f, 0.0f, ByteMax));
        }

        Optional<Color> ParseFunctionColor(StringView arguments) {
            // "r, g, b" "r, g, b, a" "r g b" "r g b / a"
            String normalized;
            for (const char c : arguments) {
                normalized.push_back(c == '/' ? ',' : c);
            }
            eastl::vector<StringView> parts = SplitTopLevel(normalized, ',');
            if (parts.size() == 1) {
                // Space separated: "r g b" or "r g b , a" after the slash became a comma (then it has two parts)
                parts = SplitWords(parts[0]);
            } else if (parts.size() == 2) {
                // "r g b / a"
                const auto rgb = SplitWords(parts[0]);
                if (rgb.size() == 3) {
                    eastl::vector<StringView> merged = rgb;
                    merged.push_back(parts[1]);
                    parts = merged;
                }
            }
            if (parts.size() != 3 && parts.size() != 4) {
                return std::nullopt;
            }
            const auto r = ParseChannel(parts[0]);
            const auto g = ParseChannel(parts[1]);
            const auto b = ParseChannel(parts[2]);
            const auto a = parts.size() == 4 ? ParseAlpha(parts[3]) : Optional<unsigned char>{OpaqueAlpha};
            if (!r || !g || !b || !a) {
                return std::nullopt;
            }
            return Color{*r, *g, *b, *a};
        }

        Optional<Color> ParseLiteralColor(StringView text) {
            text = TrimSpace(text);
            if (text.empty()) {
                return std::nullopt;
            }
            if (text.front() == '#') {
                return ParseHexColor(text.substr(1));
            }
            const size_t open = text.find('(');
            if (open != StringView::npos) {
                if (text.back() != ')') {
                    return std::nullopt;
                }
                const String name = ToLower(TrimSpace(text.substr(0, open)));
                if (name != RgbName && name != RgbaName) {
                    return std::nullopt;
                }
                return ParseFunctionColor(text.substr(open + 1, text.size() - open - 2));
            }
            const String lower = ToLower(text);
            for (const NamedColor& named : NamedColors) {
                if (named.name == lower) {
                    return named.color;
                }
            }
            return std::nullopt;
        }

        /// A px length that is a finite number
        Optional<float> ParseLength(StringView text) {
            const auto value = ParsePixels(text);
            if (!value || !std::isfinite(*value)) {
                return std::nullopt;
            }
            return value;
        }

        /// The direction word of a gradient, white space collapsed and lower case, as the direction and whether the two colors swap
        struct GradientDirectionWord {
            UIGradientDirection direction = UIGradientDirection::Vertical;
            bool swap = false;
        };

        Optional<GradientDirectionWord> ParseGradientDirection(StringView text) {
            String normalized;
            for (const StringView word : SplitWords(text)) {
                if (!normalized.empty()) {
                    normalized.push_back(' ');
                }
                normalized.append(ToLower(word));
            }
            if (normalized == "to bottom") {
                return GradientDirectionWord{UIGradientDirection::Vertical, false};
            }
            if (normalized == "to top") {
                return GradientDirectionWord{UIGradientDirection::Vertical, true};
            }
            if (normalized == "to right") {
                return GradientDirectionWord{UIGradientDirection::Horizontal, false};
            }
            if (normalized == "to left") {
                return GradientDirectionWord{UIGradientDirection::Horizontal, true};
            }
            constexpr StringView degrees = "deg";
            if (normalized.size() > degrees.size() && StringView(normalized).substr(normalized.size() - degrees.size()) == degrees) {
                const auto angle = UIText::ParseFloat(StringView(normalized).substr(0, normalized.size() - degrees.size()));
                if (angle && *angle == AngleBottom) {
                    return GradientDirectionWord{UIGradientDirection::Vertical, false};
                }
                if (angle && *angle == AngleTop) {
                    return GradientDirectionWord{UIGradientDirection::Vertical, true};
                }
                if (angle && *angle == AngleRight) {
                    return GradientDirectionWord{UIGradientDirection::Horizontal, false};
                }
                if (angle && *angle == AngleLeft) {
                    return GradientDirectionWord{UIGradientDirection::Horizontal, true};
                }
            }
            return std::nullopt;
        }

        /// One layer of a `box-shadow`: the spec and the theme token of its colour
        struct ShadowLayer {
            UIShadowSpec spec;
            PoolString token;
        };

        Optional<ShadowLayer> ParseShadowLayer(StringView text) {
            eastl::vector<float> lengths;
            Optional<WebColor> color;
            for (const StringView word : SplitWords(text)) {
                if (ToLower(word) == InsetKeyword) {
                    return std::nullopt;
                }
                if (const auto length = ParseLength(word)) {
                    lengths.push_back(*length);
                } else if (const auto parsed = ParseColor(word); parsed && !color) {
                    color = parsed;
                } else {
                    return std::nullopt;
                }
            }
            if (!color || lengths.size() < MinShadowLengths || lengths.size() > MaxShadowLengths) {
                return std::nullopt;
            }
            ShadowLayer layer;
            UIShadowSpec& shadow = layer.spec;
            shadow.offsetX = lengths[0];
            shadow.offsetY = lengths[1];
            shadow.blur = lengths.size() > ShadowBlurIndex ? lengths[ShadowBlurIndex] : 0.0f;
            shadow.spread = lengths.size() > ShadowSpreadIndex ? lengths[ShadowSpreadIndex] : 0.0f;
            shadow.color = *color;
            layer.token = color->token;
            if (shadow.blur < 0.0f) {
                return std::nullopt;
            }
            return layer;
        }

    }  // namespace

    bool IsSpace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
    }

    StringView TrimSpace(StringView text) {
        while (!text.empty() && IsSpace(text.front())) {
            text.remove_prefix(1);
        }
        while (!text.empty() && IsSpace(text.back())) {
            text.remove_suffix(1);
        }
        return text;
    }

    String ToLower(StringView text) {
        String result;
        result.reserve(text.size());
        for (const char c : text) {
            result.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
        }
        return result;
    }

    eastl::vector<StringView> SplitTopLevel(StringView text, char separator) {
        eastl::vector<StringView> parts;
        size_t start = 0;
        int depth = 0;
        char quote = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (quote != 0) {
                if (c == quote) {
                    quote = 0;
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                depth = std::max(depth - 1, 0);
            } else if (c == separator && depth == 0) {
                parts.push_back(TrimSpace(text.substr(start, i - start)));
                start = i + 1;
            }
        }
        parts.push_back(TrimSpace(text.substr(start)));
        return parts;
    }

    eastl::vector<StringView> SplitWords(StringView text) {
        eastl::vector<StringView> words;
        size_t start = 0;
        bool inWord = false;
        int depth = 0;
        char quote = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (quote != 0) {
                if (c == quote) {
                    quote = 0;
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                depth = std::max(depth - 1, 0);
            }
            if (IsSpace(c) && depth == 0) {
                if (inWord) {
                    words.push_back(text.substr(start, i - start));
                    inWord = false;
                }
            } else if (!inWord) {
                start = i;
                inWord = true;
            }
        }
        if (inWord) {
            words.push_back(text.substr(start));
        }
        return words;
    }

    Optional<float> ParsePixels(StringView text) {
        text = TrimSpace(text);
        if (text.size() > 2 && text.substr(text.size() - 2) == "px") {
            text.remove_suffix(2);
        }
        return UIText::ParseFiniteFloat(text);
    }

    String FormatWebColor(const WebColor& color) {
        return UIText::FormatColorRef(color, color.token);
    }

    Optional<WebColor> ParseColor(StringView text) {
        text = TrimSpace(text);
        constexpr StringView varName = "var";
        const size_t open = text.find('(');
        if (open != StringView::npos && text.back() == ')' && ToLower(TrimSpace(text.substr(0, open))) == varName) {
            // `var(--name)`: a theme token. A fallback (a comma) is not read
            StringView name = TrimSpace(text.substr(open + 1, text.size() - open - 2));
            if (name.size() <= TokenPrefix.size() || name.substr(0, TokenPrefix.size()) != TokenPrefix) {
                return std::nullopt;
            }
            name.remove_prefix(TokenPrefix.size());
            if (!UIThemes::IsValidToken(name)) {
                return std::nullopt;
            }
            WebColor token;
            token.token = PoolString::Intern(name);
            return token;
        }
        const auto literal = ParseLiteralColor(text);
        if (!literal) {
            return std::nullopt;
        }
        return WebColor{*literal};
    }

    Optional<String> ParseUrl(StringView text) {
        text = TrimSpace(text);
        if (text.size() < UrlName.size() + 2 || ToLower(text.substr(0, UrlName.size())) != UrlName || text[UrlName.size()] != '(' || text.back() != ')') {
            return std::nullopt;
        }
        StringView inner = TrimSpace(text.substr(UrlName.size() + 1, text.size() - UrlName.size() - 2));
        if (inner.size() >= 2 && (inner.front() == '"' || inner.front() == '\'') && inner.back() == inner.front()) {
            inner = inner.substr(1, inner.size() - 2);
        }
        return String(inner);
    }

    Optional<UICorners> ParseCornerRadii(StringView text) {
        const auto words = SplitWords(text);
        if (words.empty() || words.size() > MaxCornerValues) {
            return std::nullopt;
        }
        float radii[MaxCornerValues] = {};
        for (size_t i = 0; i < words.size(); ++i) {
            const auto radius = ParseLength(words[i]);
            if (!radius || *radius < 0.0f) {
                return std::nullopt;
            }
            radii[i] = *radius;
        }
        // The CSS order: one value for all, two for the diagonals, three with the bottom-left taken from the top-right
        switch (words.size()) {
            case 1:
                return UICorners::Uniform(radii[0]);
            case 2:
                return UICorners{radii[0], radii[1], radii[0], radii[1]};
            case 3:
                return UICorners{radii[0], radii[1], radii[2], radii[1]};
            default:
                return UICorners{radii[0], radii[1], radii[2], radii[3]};
        }
    }

    Optional<BorderSpec> ParseBorder(StringView text) {
        Optional<float> width;
        Optional<WebColor> color;
        Optional<bool> solid;
        for (const StringView word : SplitWords(text)) {
            const String lower = ToLower(word);
            if (lower == SolidKeyword || lower == NoneKeyword) {
                if (solid) {
                    return std::nullopt;
                }
                solid = lower == SolidKeyword;
            } else if (const auto length = ParseLength(word)) {
                if (width || *length < 0.0f) {
                    return std::nullopt;
                }
                width = length;
            } else if (const auto parsed = ParseColor(word); parsed && !color) {
                color = parsed;
            } else {
                return std::nullopt;
            }
        }
        BorderSpec border;
        if ((solid && !*solid) || (width && *width == 0.0f)) {
            border.none = true;
            return border;
        }
        if (!width || !color) {
            return std::nullopt;
        }
        border.width = *width;
        border.color = *color;
        return border;
    }

    Optional<GradientSpec> ParseLinearGradient(StringView text) {
        text = TrimSpace(text);
        if (text.size() <= GradientName.size() + 2 || ToLower(text.substr(0, GradientName.size())) != GradientName || text[GradientName.size()] != '(' || text.back() != ')') {
            return std::nullopt;
        }
        const auto parts = SplitTopLevel(text.substr(GradientName.size() + 1, text.size() - GradientName.size() - 2), ',');
        constexpr size_t stopsOnly = 2;
        constexpr size_t withDirection = 3;
        if (parts.size() != stopsOnly && parts.size() != withDirection) {
            return std::nullopt;
        }
        GradientDirectionWord direction;
        if (parts.size() == withDirection) {
            const auto parsed = ParseGradientDirection(parts[0]);
            if (!parsed) {
                return std::nullopt;
            }
            direction = *parsed;
        }
        const auto first = ParseColor(parts[parts.size() - stopsOnly]);
        const auto second = ParseColor(parts[parts.size() - 1]);
        if (!first || !second) {
            return std::nullopt;
        }
        GradientSpec gradient;
        gradient.direction = direction.direction;
        gradient.start = direction.swap ? *second : *first;
        gradient.end = direction.swap ? *first : *second;
        return gradient;
    }

    Optional<BoxShadows> ParseBoxShadows(StringView text) {
        text = TrimSpace(text);
        BoxShadows shadows;
        if (ToLower(text) == NoneKeyword) {
            return shadows;
        }
        for (const StringView layer : SplitTopLevel(text, ',')) {
            const auto parsed = ParseShadowLayer(layer);
            if (!parsed) {
                return std::nullopt;
            }
            if (shadows.count < MaxShadowLayers) {
                shadows.layers[shadows.count] = parsed->spec;
                shadows.tokens[shadows.count] = parsed->token;
            }
            ++shadows.count;
        }
        return shadows;
    }

}  // namespace BECore::UIWebText
