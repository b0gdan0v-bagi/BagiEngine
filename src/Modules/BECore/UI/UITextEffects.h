#pragma once

#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Text/TextEffects.h>
#include <BECore/UI/UIColor.h>

namespace BECore {

    /**
     * @brief The outline and the hard drop shadow of a label's text, flat in a file like UIBoxStyle: two attributes of the element.
     *
     * - `textOutline="width,color"`: a ring of that width (UI units) round every glyph, in the colour (`#RRGGBBAA`, `r,g,b,a` or a theme token `@name`). No width or no alpha: no outline.
     * - `textShadow="offsetX,offsetY,color"` (UI units): the text again, moved by the offset, under the outline. No alpha or a zero offset: no shadow.
     *
     * Both colours may be theme tokens (UIColor::token, UIThemes). Both are paint, never layout: they do not enter the layout cache key and the validator measures the text without them. Only a value
     * that differs from the default is written. The names must not clash with a field of a control.
     */
    struct UITextEffects {
        float outlineWidth = 0.0f;  ///< UI units
        UIColor outlineColor;
        float shadowOffsetX = 0.0f;  ///< UI units, to the right
        float shadowOffsetY = 0.0f;  ///< UI units, down
        UIColor shadowColor;

        [[nodiscard]] bool operator==(const UITextEffects& other) const;

        /// The effects in output pixels for a canvas of @p scale (output pixels per UI unit): the outline width rounded to a whole pixel the outline atlas has
        /// (FontAtlasLibrary::QuantizeOutline), the offsets rounded. The alpha of both colours is multiplied by @p alphaFactor (a faded text fades its outline too)
        [[nodiscard]] TextEffects ToPixels(float scale, float alphaFactor = 1.0f) const;

        void Serialize(ISerializer& s) const;
        void Deserialize(IDeserializer& d);
        bool VisitProperties(IPropertyVisitor& v);
    };

    namespace Ser {

        /// Flat in a file, like UIBoxStyle: it reads and writes the attributes of the object the serializer is in
        template <>
        struct DataAccessor<UITextEffects> {
            static void Save(ISerializer& s, const UITextEffects& value, eastl::string_view /*key*/) {
                value.Serialize(s);
            }

            static bool Load(IDeserializer& d, UITextEffects& value, eastl::string_view /*key*/) {
                value.Deserialize(d);
                return true;
            }
        };

    }  // namespace Ser

}  // namespace BECore
