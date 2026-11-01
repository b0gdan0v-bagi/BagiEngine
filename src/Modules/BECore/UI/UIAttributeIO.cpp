#include "UIAttributeIO.h"

#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/UI/UIValueText.h>

namespace BECore::UIAttr {

    namespace {

        void WriteText(ISerializer& s, eastl::string_view key, const eastl::string& text) {
            s.WriteAttribute(key, eastl::string_view(text.data(), text.size()));
        }

        /// The text of an attribute, or false when there is none
        bool ReadText(IDeserializer& d, eastl::string_view key, eastl::string& text) {
            return d.ReadAttribute(key, text);
        }

        void ReportMalformed(IDeserializer& d, eastl::string_view key, const eastl::string& text) {
            d.ReportError(key, eastl::string_view(Format("Invalid value: '{}'", text).c_str()));
        }

    }  // namespace

    void WriteLength(ISerializer& s, eastl::string_view key, const UILength& length) {
        WriteText(s, key, UIText::FormatLength(length));
    }

    bool ReadLength(IDeserializer& d, eastl::string_view key, UILength& out) {
        eastl::string text;
        if (!ReadText(d, key, text)) {
            return false;
        }
        const auto length = UIText::ParseLength(eastl::string_view(text.data(), text.size()));
        if (!length) {
            ReportMalformed(d, key, text);
            return false;
        }
        out = *length;
        return true;
    }

    void WriteEdges(ISerializer& s, eastl::string_view key, const UIEdges& edges) {
        WriteText(s, key, UIText::FormatEdges(edges));
    }

    bool ReadEdges(IDeserializer& d, eastl::string_view key, UIEdges& out) {
        eastl::string text;
        if (!ReadText(d, key, text)) {
            return false;
        }
        const auto edges = UIText::ParseEdges(eastl::string_view(text.data(), text.size()));
        if (!edges) {
            ReportMalformed(d, key, text);
            return false;
        }
        out = *edges;
        return true;
    }

    void WriteOptionalLength(ISerializer& s, eastl::string_view key, const std::optional<UILength>& length) {
        if (length) {
            WriteLength(s, key, *length);
        }
    }

    void ReadOptionalLength(IDeserializer& d, eastl::string_view key, std::optional<UILength>& out) {
        UILength length;
        if (ReadLength(d, key, length)) {
            out = length;
        }
    }

}  // namespace BECore::UIAttr
