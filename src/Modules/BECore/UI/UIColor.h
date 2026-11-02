#pragma once

#include <BECore/Reflection/DataAccessor.h>
#include <BECore/UI/UIValueText.h>

namespace BECore {

    /**
     * @brief A Color that a UI file writes in one attribute (`color="0,0,0,160"` or `color="#000000A0"`) instead of the child element a plain Color uses.
     *
     * It is a Color for everything else: it converts from one and is passed where one is expected.
     */
    struct UIColor : Color {
        constexpr UIColor() = default;
        constexpr UIColor(unsigned char r_, unsigned char g_, unsigned char b_, unsigned char a_) : Color(r_, g_, b_, a_) {}
        constexpr UIColor(const Color& color) : Color(color) {}  // NOLINT(google-explicit-constructor): a UI field takes a plain Color
    };

    namespace Ser {

        template <>
        struct DataAccessor<UIColor> {
            static void Save(ISerializer& s, const UIColor& value, eastl::string_view key) {
                const eastl::string text = UIText::FormatColor(value);
                s.WriteAttribute(key, eastl::string_view(text.data(), text.size()));
            }

            static bool Load(IDeserializer& d, UIColor& value, eastl::string_view key) {
                eastl::string text;
                if (!d.ReadAttribute(key, text)) {
                    return false;
                }
                const auto color = UIText::ParseColor(eastl::string_view(text.data(), text.size()));
                if (!color) {
                    d.ReportError(key, eastl::string_view(Format("Invalid color: '{}'", text).c_str()));
                    return false;
                }
                value = *color;
                return true;
            }
        };

    }  // namespace Ser

}  // namespace BECore
