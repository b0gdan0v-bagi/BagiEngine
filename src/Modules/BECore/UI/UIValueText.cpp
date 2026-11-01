#include "UIValueText.h"


namespace BECore::UIText {

    namespace {

        constexpr char PercentSign = '%';
        constexpr eastl::string_view PixelsSuffix = "px";
        constexpr size_t RgbaPartCount = 4;
        constexpr size_t HexRgbLength = 6;
        constexpr size_t HexRgbaLength = 8;
        constexpr int HexBase = 16;
        constexpr unsigned char OpaqueAlpha = 255;

        std::optional<unsigned char> ParseByte(eastl::string_view text) {
            text = Trim(text);
            unsigned int value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size() || value > OpaqueAlpha) {
                return std::nullopt;
            }
            return static_cast<unsigned char>(value);
        }

        std::optional<unsigned char> ParseHexByte(eastl::string_view text) {
            unsigned int value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, HexBase);
            if (error != std::errc{} || end != text.data() + text.size()) {
                return std::nullopt;
            }
            return static_cast<unsigned char>(value);
        }

    }  // namespace

    eastl::string_view Trim(eastl::string_view text) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
            text.remove_prefix(1);
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
            text.remove_suffix(1);
        }
        return text;
    }

    std::optional<float> ParseFloat(eastl::string_view text) {
        text = Trim(text);
        float value = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
            return std::nullopt;
        }
        return value;
    }

    eastl::string FormatLength(const UILength& length) {
        return length.unit == UIUnit::Percent ? Format("{}%", length.value) : Format("{}", length.value);
    }

    std::optional<UILength> ParseLength(eastl::string_view text) {
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

    eastl::string FormatEdges(const UIEdges& edges) {
        if (edges.left == edges.top && edges.top == edges.right && edges.right == edges.bottom) {
            return Format("{}", edges.left);
        }
        return Format("{},{},{},{}", edges.left, edges.top, edges.right, edges.bottom);
    }

    std::optional<UIEdges> ParseEdges(eastl::string_view text) {
        const auto parts = String::Split(Trim(text), ',');
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

    eastl::string FormatColor(const Color& color) {
        return Format("{},{},{},{}", static_cast<int>(color.r), static_cast<int>(color.g), static_cast<int>(color.b), static_cast<int>(color.a));
    }

    std::optional<Color> ParseColor(eastl::string_view text) {
        text = Trim(text);
        if (!text.empty() && text.front() == '#') {
            text.remove_prefix(1);
            if (text.size() != HexRgbLength && text.size() != HexRgbaLength) {
                return std::nullopt;
            }
            const auto r = ParseHexByte(text.substr(0, 2));
            const auto g = ParseHexByte(text.substr(2, 2));
            const auto b = ParseHexByte(text.substr(4, 2));
            const auto a = text.size() == HexRgbaLength ? ParseHexByte(text.substr(6, 2)) : std::optional<unsigned char>{OpaqueAlpha};
            if (!r || !g || !b || !a) {
                return std::nullopt;
            }
            return Color{*r, *g, *b, *a};
        }
        const auto parts = String::Split(text, ',');
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

}  // namespace BECore::UIText
