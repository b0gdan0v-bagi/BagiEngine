#include "UIAttributeIO.h"

#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/UI/UIValueText.h>

namespace BECore::UIAttr {

    namespace {

        void WriteText(ISerializer& s, StringView key, const eastl::string& text) {
            s.WriteAttribute(key, StringView(text));
        }

        /// The text of an attribute, or false when there is none
        bool ReadText(IDeserializer& d, StringView key, eastl::string& text) {
            return d.ReadAttribute(key, text);
        }

        void ReportMalformed(IDeserializer& d, StringView key, const eastl::string& text) {
            d.ReportError(key, StringView(Format("Invalid value: '{}'", text).c_str()));
        }

    }  // namespace

    void WriteLength(ISerializer& s, StringView key, const UILength& length) {
        WriteText(s, key, UIText::FormatLength(length));
    }

    bool ReadLength(IDeserializer& d, StringView key, UILength& out) {
        eastl::string text;
        if (!ReadText(d, key, text)) {
            return false;
        }
        const auto length = UIText::ParseLength(StringView(text));
        if (!length) {
            ReportMalformed(d, key, text);
            return false;
        }
        out = *length;
        return true;
    }

    void WriteEdges(ISerializer& s, StringView key, const UIEdges& edges) {
        WriteText(s, key, UIText::FormatEdges(edges));
    }

    bool ReadEdges(IDeserializer& d, StringView key, UIEdges& out) {
        eastl::string text;
        if (!ReadText(d, key, text)) {
            return false;
        }
        const auto edges = UIText::ParseEdges(StringView(text));
        if (!edges) {
            ReportMalformed(d, key, text);
            return false;
        }
        out = *edges;
        return true;
    }

    void WriteVec2Fraction(ISerializer& s, StringView key, const Vec2& value) {
        WriteText(s, key, UIText::FormatFraction2(value));
    }

    void WriteOptionalVec2Fraction(ISerializer& s, StringView key, const Optional<Vec2>& value) {
        if (value) {
            WriteVec2Fraction(s, key, *value);
        }
    }

    void ReadOptionalVec2Fraction(IDeserializer& d, StringView key, Optional<Vec2>& out) {
        eastl::string text;
        if (!ReadText(d, key, text)) {
            return;
        }
        const auto value = UIText::ParseFraction2(StringView(text));
        if (!value) {
            ReportMalformed(d, key, text);
            return;
        }
        out = *value;
    }

    void WriteOptionalEdges(ISerializer& s, StringView key, const Optional<UIEdges>& edges) {
        if (edges) {
            WriteEdges(s, key, *edges);
        }
    }

    void ReadOptionalEdges(IDeserializer& d, StringView key, Optional<UIEdges>& out) {
        UIEdges edges;
        if (ReadEdges(d, key, edges)) {
            out = edges;
        }
    }

    void WriteCorners(ISerializer& s, StringView key, const UICorners& corners) {
        WriteText(s, key, UIText::FormatCorners(corners));
    }

    bool ReadCorners(IDeserializer& d, StringView key, UICorners& out) {
        eastl::string text;
        if (!ReadText(d, key, text)) {
            return false;
        }
        const auto corners = UIText::ParseCorners(StringView(text));
        if (!corners) {
            ReportMalformed(d, key, text);
            return false;
        }
        out = *corners;
        return true;
    }

    void WriteShadow(ISerializer& s, StringView key, const UIShadowSpec& shadow, PoolString colorToken) {
        WriteText(s, key, UIText::FormatShadow(shadow, colorToken));
    }

    bool ReadShadow(IDeserializer& d, StringView key, UIShadowSpec& out, PoolString& colorToken) {
        eastl::string text;
        if (!ReadText(d, key, text)) {
            return false;
        }
        PoolString token;
        const auto shadow = UIText::ParseShadow(StringView(text), &token);
        if (!shadow) {
            ReportMalformed(d, key, text);
            return false;
        }
        out = *shadow;
        colorToken = token;
        return true;
    }

    void WriteTextOutline(ISerializer& s, StringView key, float width, const UIColor& color) {
        WriteText(s, key, UIText::FormatTextOutline(width, color, color.token));
    }

    bool ReadTextOutline(IDeserializer& d, StringView key, float& width, UIColor& color) {
        eastl::string text;
        if (!ReadText(d, key, text)) {
            return false;
        }
        float parsedWidth = 0.0f;
        Color parsedColor;
        PoolString token;
        if (!UIText::ParseTextOutline(StringView(text), parsedWidth, parsedColor, &token)) {
            ReportMalformed(d, key, text);
            return false;
        }
        width = parsedWidth;
        color = token.Empty() ? UIColor(parsedColor) : UIColor::FromToken(token);
        return true;
    }

    void WriteTextShadow(ISerializer& s, StringView key, float offsetX, float offsetY, const UIColor& color) {
        WriteText(s, key, UIText::FormatTextShadow(offsetX, offsetY, color, color.token));
    }

    bool ReadTextShadow(IDeserializer& d, StringView key, float& offsetX, float& offsetY, UIColor& color) {
        eastl::string text;
        if (!ReadText(d, key, text)) {
            return false;
        }
        float parsedX = 0.0f;
        float parsedY = 0.0f;
        Color parsedColor;
        PoolString token;
        if (!UIText::ParseTextShadow(StringView(text), parsedX, parsedY, parsedColor, &token)) {
            ReportMalformed(d, key, text);
            return false;
        }
        offsetX = parsedX;
        offsetY = parsedY;
        color = token.Empty() ? UIColor(parsedColor) : UIColor::FromToken(token);
        return true;
    }

    void WriteOptionalLength(ISerializer& s, StringView key, const Optional<UILength>& length) {
        if (length) {
            WriteLength(s, key, *length);
        }
    }

    void ReadOptionalLength(IDeserializer& d, StringView key, Optional<UILength>& out) {
        UILength length;
        if (ReadLength(d, key, length)) {
            out = length;
        }
    }

}  // namespace BECore::UIAttr
