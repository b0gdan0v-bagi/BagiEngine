#include "UIWebText.h"

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
        constexpr eastl::string_view RgbName = "rgb";
        constexpr eastl::string_view RgbaName = "rgba";
        constexpr eastl::string_view UrlName = "url";

        struct NamedColor {
            eastl::string_view name;
            Color color;
        };

        // The names a screen is likely to use; the rest is written as hex
        constexpr NamedColor NamedColors[] = {
            {"transparent", {0, 0, 0, 0}},  {"black", {0, 0, 0, 255}},       {"white", {255, 255, 255, 255}},  {"red", {255, 0, 0, 255}},
            {"green", {0, 128, 0, 255}},    {"blue", {0, 0, 255, 255}},      {"yellow", {255, 255, 0, 255}},   {"orange", {255, 165, 0, 255}},
            {"gray", {128, 128, 128, 255}}, {"grey", {128, 128, 128, 255}},  {"silver", {192, 192, 192, 255}}, {"purple", {128, 0, 128, 255}},
            {"cyan", {0, 255, 255, 255}},   {"magenta", {255, 0, 255, 255}}, {"lime", {0, 255, 0, 255}},       {"navy", {0, 0, 128, 255}},
        };

        std::optional<unsigned char> ParseHexDigits(eastl::string_view digits) {
            unsigned int value = 0;
            const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, HexBase);
            if (error != std::errc{} || end != digits.data() + digits.size()) {
                return std::nullopt;
            }
            return static_cast<unsigned char>(value);
        }

        std::optional<Color> ParseHexColor(eastl::string_view digits) {
            if (digits.size() == ShortHexLength || digits.size() == ShortHexAlphaLength) {
                // #rgb and #rgba: each digit is doubled
                eastl::string expanded;
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
            const auto a = digits.size() == LongHexAlphaLength ? ParseHexDigits(digits.substr(6, 2)) : std::optional<unsigned char>{OpaqueAlpha};
            if (!r || !g || !b || !a) {
                return std::nullopt;
            }
            return Color{*r, *g, *b, *a};
        }

        /// A channel as a number 0 to 255 or a percent
        std::optional<unsigned char> ParseChannel(eastl::string_view text) {
            text = TrimSpace(text);
            bool percent = false;
            if (!text.empty() && text.back() == '%') {
                percent = true;
                text.remove_suffix(1);
            }
            const auto value = UIText::ParseFloat(text);
            if (!value) {
                return std::nullopt;
            }
            const float scaled = percent ? *value * PercentScale * ByteMax : *value;
            return static_cast<unsigned char>(std::clamp(scaled + 0.5f, 0.0f, ByteMax));
        }

        /// An alpha as a number 0 to 1 or a percent
        std::optional<unsigned char> ParseAlpha(eastl::string_view text) {
            text = TrimSpace(text);
            bool percent = false;
            if (!text.empty() && text.back() == '%') {
                percent = true;
                text.remove_suffix(1);
            }
            const auto value = UIText::ParseFloat(text);
            if (!value) {
                return std::nullopt;
            }
            const float unit = percent ? *value * PercentScale : *value;
            return static_cast<unsigned char>(std::clamp(unit * ByteMax + 0.5f, 0.0f, ByteMax));
        }

        std::optional<Color> ParseFunctionColor(eastl::string_view arguments) {
            // "r, g, b" "r, g, b, a" "r g b" "r g b / a"
            eastl::string normalized;
            for (const char c : arguments) {
                normalized.push_back(c == '/' ? ',' : c);
            }
            eastl::vector<eastl::string_view> parts = SplitTopLevel(normalized, ',');
            if (parts.size() == 1) {
                // Space separated: "r g b" or "r g b , a" after the slash became a comma (then it has two parts)
                parts = SplitWords(parts[0]);
            } else if (parts.size() == 2) {
                // "r g b / a"
                const auto rgb = SplitWords(parts[0]);
                if (rgb.size() == 3) {
                    eastl::vector<eastl::string_view> merged = rgb;
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
            const auto a = parts.size() == 4 ? ParseAlpha(parts[3]) : std::optional<unsigned char>{OpaqueAlpha};
            if (!r || !g || !b || !a) {
                return std::nullopt;
            }
            return Color{*r, *g, *b, *a};
        }

    }  // namespace

    bool IsSpace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
    }

    eastl::string_view TrimSpace(eastl::string_view text) {
        while (!text.empty() && IsSpace(text.front())) {
            text.remove_prefix(1);
        }
        while (!text.empty() && IsSpace(text.back())) {
            text.remove_suffix(1);
        }
        return text;
    }

    eastl::string ToLower(eastl::string_view text) {
        eastl::string result;
        result.reserve(text.size());
        for (const char c : text) {
            result.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
        }
        return result;
    }

    eastl::vector<eastl::string_view> SplitTopLevel(eastl::string_view text, char separator) {
        eastl::vector<eastl::string_view> parts;
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

    eastl::vector<eastl::string_view> SplitWords(eastl::string_view text) {
        eastl::vector<eastl::string_view> words;
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

    std::optional<float> ParsePixels(eastl::string_view text) {
        text = TrimSpace(text);
        if (text.size() > 2 && text.substr(text.size() - 2) == "px") {
            text.remove_suffix(2);
        }
        return UIText::ParseFloat(text);
    }

    std::optional<Color> ParseColor(eastl::string_view text) {
        text = TrimSpace(text);
        if (text.empty()) {
            return std::nullopt;
        }
        if (text.front() == '#') {
            return ParseHexColor(text.substr(1));
        }
        const size_t open = text.find('(');
        if (open != eastl::string_view::npos) {
            if (text.back() != ')') {
                return std::nullopt;
            }
            const eastl::string name = ToLower(TrimSpace(text.substr(0, open)));
            if (name != RgbName && name != RgbaName) {
                return std::nullopt;
            }
            return ParseFunctionColor(text.substr(open + 1, text.size() - open - 2));
        }
        const eastl::string lower = ToLower(text);
        for (const NamedColor& named : NamedColors) {
            if (named.name == lower) {
                return named.color;
            }
        }
        return std::nullopt;
    }

    std::optional<eastl::string> ParseUrl(eastl::string_view text) {
        text = TrimSpace(text);
        if (text.size() < UrlName.size() + 2 || ToLower(text.substr(0, UrlName.size())) != UrlName || text[UrlName.size()] != '(' || text.back() != ')') {
            return std::nullopt;
        }
        eastl::string_view inner = TrimSpace(text.substr(UrlName.size() + 1, text.size() - UrlName.size() - 2));
        if (inner.size() >= 2 && (inner.front() == '"' || inner.front() == '\'') && inner.back() == inner.front()) {
            inner = inner.substr(1, inner.size() - 2);
        }
        return eastl::string(inner);
    }

}  // namespace BECore::UIWebText
