#include "UIValueText.h"

#include <BECore/UI/UIThemes.h>
#include <cmath>

namespace BECore::UIText {

    namespace {

        constexpr char PercentSign = '%';
        constexpr StringView PixelsSuffix = "px";
        constexpr size_t FractionPartCount = 2;
        constexpr float PercentDivisor = 100.0f;
        constexpr size_t RgbaPartCount = 4;
        constexpr size_t CornerCount = 4;
        constexpr size_t ShadowNumberCount = 4;  // offsetX, offsetY, blur, spread: the colour follows
        constexpr size_t ShadowBlurIndex = 2;
        constexpr size_t TextShadowNumberCount = 2;  // offsetX, offsetY: the colour follows
        constexpr size_t HexRgbLength = 6;
        constexpr size_t HexRgbaLength = 8;
        constexpr int HexBase = 16;
        constexpr unsigned char OpaqueAlpha = 255;
        constexpr char TokenSign = '@';

        /// A colour text of a composite attribute: a literal, or a token when @p token is given. Without @p token a "@name" is refused
        bool ParseColorPart(StringView text, Color& color, PoolString* token) {
            const auto parsed = ParseColorRef(text);
            if (!parsed || (!parsed->token.Empty() && !token)) {
                return false;
            }
            color = parsed->color;
            if (token) {
                *token = parsed->token;
            }
            return true;
        }

        Optional<unsigned char> ParseByte(StringView text) {
            text = Trim(text);
            unsigned int value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size() || value > OpaqueAlpha) {
                return std::nullopt;
            }
            return static_cast<unsigned char>(value);
        }

        Optional<unsigned char> ParseHexByte(StringView text) {
            unsigned int value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, HexBase);
            if (error != std::errc{} || end != text.data() + text.size()) {
                return std::nullopt;
            }
            return static_cast<unsigned char>(value);
        }

    }  // namespace

    StringView Trim(StringView text) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
            text.remove_prefix(1);
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
            text.remove_suffix(1);
        }
        return text;
    }

    Optional<float> ParseFloat(StringView text) {
        text = Trim(text);
        float value = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
            return std::nullopt;
        }
        return value;
    }

    Optional<float> ParseFiniteFloat(StringView text) {
        const auto value = ParseFloat(text);
        if (!value || !std::isfinite(*value)) {
            return std::nullopt;
        }
        return value;
    }

    float FractionInRange(float value, float minValue, float maxValue) {
        if (!std::isfinite(value) || !std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) {
            return 0.0f;
        }
        // In double: the span of two large floats overflows a float
        const double fraction = (static_cast<double>(value) - minValue) / (static_cast<double>(maxValue) - minValue);
        return static_cast<float>(std::clamp(fraction, 0.0, 1.0));
    }

    float ValueInRange(StringView text, float minValue, float maxValue) {
        const auto value = ParseFiniteFloat(text);
        if (!value || !std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) {
            return minValue;
        }
        return std::clamp(*value, minValue, maxValue);
    }

    eastl::string FormatLength(const UILength& length) {
        return length.unit == UIUnit::Percent ? Format("{}%", length.value) : Format("{}", length.value);
    }

    Optional<UILength> ParseLength(StringView text) {
        text = Trim(text);
        UIUnit unit = UIUnit::Pixels;
        if (!text.empty() && text.back() == PercentSign) {
            unit = UIUnit::Percent;
            text.remove_suffix(1);
        } else if (text.size() > PixelsSuffix.size() && text.substr(text.size() - PixelsSuffix.size()) == PixelsSuffix) {
            text.remove_suffix(PixelsSuffix.size());
        }
        const auto value = ParseFloat(text);
        if (!value) {
            return std::nullopt;
        }
        return UILength{*value, unit};
    }

    eastl::string FormatFraction2(const Vec2& value) {
        return Format("{},{}", value.x, value.y);
    }

    Optional<Vec2> ParseFraction2(StringView text) {
        const auto parts = StringUtils::Split(Trim(text), ',');
        if (parts.size() != FractionPartCount) {
            return std::nullopt;
        }
        float fractions[FractionPartCount] = {};
        for (size_t i = 0; i < FractionPartCount; ++i) {
            StringView part = Trim(parts[i]);
            float divisor = 1.0f;
            if (!part.empty() && part.back() == PercentSign) {
                part.remove_suffix(1);
                divisor = PercentDivisor;
            }
            const auto number = ParseFiniteFloat(part);
            if (!number) {
                return std::nullopt;
            }
            fractions[i] = *number / divisor;
        }
        return Vec2{fractions[0], fractions[1]};
    }

    eastl::string FormatEdges(const UIEdges& edges) {
        if (edges.left == edges.top && edges.top == edges.right && edges.right == edges.bottom) {
            return Format("{}", edges.left);
        }
        return Format("{},{},{},{}", edges.left, edges.top, edges.right, edges.bottom);
    }

    Optional<UIEdges> ParseEdges(StringView text) {
        const auto parts = StringUtils::Split(Trim(text), ',');
        if (parts.size() == 1) {
            const auto all = ParseFloat(parts[0]);
            if (!all) {
                return std::nullopt;
            }
            return UIEdges{*all, *all, *all, *all};
        }
        if (parts.size() != 4) {
            return std::nullopt;
        }
        const auto left = ParseFloat(parts[0]);
        const auto top = ParseFloat(parts[1]);
        const auto right = ParseFloat(parts[2]);
        const auto bottom = ParseFloat(parts[3]);
        if (!left || !top || !right || !bottom) {
            return std::nullopt;
        }
        return UIEdges{*left, *top, *right, *bottom};
    }

    eastl::string FormatCorners(const UICorners& corners) {
        if (corners.topLeft == corners.topRight && corners.topRight == corners.bottomRight && corners.bottomRight == corners.bottomLeft) {
            return Format("{}", corners.topLeft);
        }
        return Format("{},{},{},{}", corners.topLeft, corners.topRight, corners.bottomRight, corners.bottomLeft);
    }

    Optional<UICorners> ParseCorners(StringView text) {
        const auto parts = StringUtils::Split(Trim(text), ',');
        if (parts.size() != 1 && parts.size() != CornerCount) {
            return std::nullopt;
        }
        float radii[CornerCount] = {};
        for (size_t i = 0; i < parts.size(); ++i) {
            const auto radius = ParseFiniteFloat(parts[i]);
            if (!radius) {
                return std::nullopt;
            }
            radii[i] = *radius;
        }
        return parts.size() == 1 ? UICorners::Uniform(radii[0]) : UICorners{radii[0], radii[1], radii[2], radii[3]};
    }

    eastl::string FormatShadow(const UIShadowSpec& shadow, PoolString colorToken) {
        if (!colorToken.Empty()) {
            return Format("{},{},{},{},@{}", shadow.offsetX, shadow.offsetY, shadow.blur, shadow.spread, colorToken.ToStringView());
        }
        return Format("{},{},{},{},#{:02X}{:02X}{:02X}{:02X}", shadow.offsetX, shadow.offsetY, shadow.blur, shadow.spread, static_cast<int>(shadow.color.r), static_cast<int>(shadow.color.g),
                      static_cast<int>(shadow.color.b), static_cast<int>(shadow.color.a));
    }

    Optional<UIShadowSpec> ParseShadow(StringView text, PoolString* colorToken) {
        text = Trim(text);
        float numbers[ShadowNumberCount] = {};
        for (float& number : numbers) {
            const size_t comma = text.find(',');
            if (comma == StringView::npos) {
                return std::nullopt;
            }
            const auto parsed = ParseFiniteFloat(text.substr(0, comma));
            if (!parsed) {
                return std::nullopt;
            }
            number = *parsed;
            text.remove_prefix(comma + 1);
        }
        Color color;
        if (numbers[ShadowBlurIndex] < 0.0f || !ParseColorPart(text, color, colorToken)) {
            return std::nullopt;
        }
        return UIShadowSpec{numbers[0], numbers[1], numbers[ShadowBlurIndex], numbers[3], color};
    }

    eastl::string FormatTextOutline(float width, const Color& color, PoolString token) {
        if (!token.Empty()) {
            return Format("{},@{}", width, token.ToStringView());
        }
        return Format("{},#{:02X}{:02X}{:02X}{:02X}", width, static_cast<int>(color.r), static_cast<int>(color.g), static_cast<int>(color.b), static_cast<int>(color.a));
    }

    bool ParseTextOutline(StringView text, float& width, Color& color, PoolString* token) {
        text = Trim(text);
        const size_t comma = text.find(',');
        if (comma == StringView::npos) {
            return false;
        }
        const auto parsedWidth = ParseFiniteFloat(text.substr(0, comma));
        Color parsedColor;
        if (!parsedWidth || *parsedWidth < 0.0f || !ParseColorPart(text.substr(comma + 1), parsedColor, token)) {
            return false;
        }
        width = *parsedWidth;
        color = parsedColor;
        return true;
    }

    eastl::string FormatTextShadow(float offsetX, float offsetY, const Color& color, PoolString token) {
        if (!token.Empty()) {
            return Format("{},{},@{}", offsetX, offsetY, token.ToStringView());
        }
        return Format("{},{},#{:02X}{:02X}{:02X}{:02X}", offsetX, offsetY, static_cast<int>(color.r), static_cast<int>(color.g), static_cast<int>(color.b), static_cast<int>(color.a));
    }

    bool ParseTextShadow(StringView text, float& offsetX, float& offsetY, Color& color, PoolString* token) {
        text = Trim(text);
        float numbers[TextShadowNumberCount] = {};
        for (float& number : numbers) {
            const size_t comma = text.find(',');
            if (comma == StringView::npos) {
                return false;
            }
            const auto parsed = ParseFiniteFloat(text.substr(0, comma));
            if (!parsed) {
                return false;
            }
            number = *parsed;
            text.remove_prefix(comma + 1);
        }
        Color parsedColor;
        if (!ParseColorPart(text, parsedColor, token)) {
            return false;
        }
        offsetX = numbers[0];
        offsetY = numbers[1];
        color = parsedColor;
        return true;
    }

    eastl::string FormatColor(const Color& color) {
        return Format("{},{},{},{}", static_cast<int>(color.r), static_cast<int>(color.g), static_cast<int>(color.b), static_cast<int>(color.a));
    }

    Optional<Color> ParseColor(StringView text) {
        text = Trim(text);
        if (!text.empty() && text.front() == '#') {
            text.remove_prefix(1);
            if (text.size() != HexRgbLength && text.size() != HexRgbaLength) {
                return std::nullopt;
            }
            const auto r = ParseHexByte(text.substr(0, 2));
            const auto g = ParseHexByte(text.substr(2, 2));
            const auto b = ParseHexByte(text.substr(4, 2));
            const auto a = text.size() == HexRgbaLength ? ParseHexByte(text.substr(6, 2)) : Optional<unsigned char>{OpaqueAlpha};
            if (!r || !g || !b || !a) {
                return std::nullopt;
            }
            return Color{*r, *g, *b, *a};
        }
        const auto parts = StringUtils::Split(text, ',');
        if (parts.size() != RgbaPartCount) {
            return std::nullopt;
        }
        const auto r = ParseByte(parts[0]);
        const auto g = ParseByte(parts[1]);
        const auto b = ParseByte(parts[2]);
        const auto a = ParseByte(parts[3]);
        if (!r || !g || !b || !a) {
            return std::nullopt;
        }
        return Color{*r, *g, *b, *a};
    }

    eastl::string FormatColorRef(const Color& color, PoolString token) {
        if (!token.Empty()) {
            return Format("@{}", token.ToStringView());
        }
        return FormatColor(color);
    }

    Optional<ColorRef> ParseColorRef(StringView text) {
        text = Trim(text);
        if (!text.empty() && text.front() == TokenSign) {
            const StringView name = text.substr(1);
            if (!UIThemes::IsValidToken(name)) {
                return std::nullopt;
            }
            return ColorRef{Color{}, PoolString::Intern(name)};
        }
        const auto color = ParseColor(text);
        if (!color) {
            return std::nullopt;
        }
        return ColorRef{*color, PoolString{}};
    }

}  // namespace BECore::UIText
