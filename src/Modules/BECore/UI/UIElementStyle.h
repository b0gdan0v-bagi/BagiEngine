#pragma once

#include <BECore/Reflection/DataAccessor.h>
#include <BECore/UI/UIAttributeIO.h>
#include <BECore/UI/UIRectSpec.h>

namespace BECore {

    /**
     * @brief Everything a variant can change about an element.
     *
     * In a file the style is not a child element: its properties are attributes of the element itself (`anchor`, `x`, `y`, `width`, `height`, `minWidth`,
     * `maxWidth`, `minHeight`, `maxHeight`, `aspectRatio`, `aspectMode`, `margin`, `flex`, `direction`, `spacing`, `padding`, `wrapChildren`, `crossAlign`,
     * `fontSize`, `visible`). Only a value that differs from the default is written. A length is "220" or "90%", `margin` and `padding` are one number or
     * "left,top,right,bottom".
     *
     * These names share the element's attributes with the fields of every control (a label has its own `wrap`, the text one), so a new style attribute must not
     * reuse the name of a control field: `UIControlsTest` round-trips every control with everything set and fails on a clash.
     */
    struct UIElementStyle {
        bool visible = true;
        UIRectSpec rect;
        UIStackSpec stack;      ///< used when the element's layout is Stack
        float fontSize = 0.0f;  ///< in UI units; 0 leaves the element's own size. Elements without text ignore it

        [[nodiscard]] bool operator==(const UIElementStyle&) const = default;

        void Serialize(ISerializer& s) const;
        void Deserialize(IDeserializer& d);
        bool VisitProperties(IPropertyVisitor& v);
    };

    namespace Ser {

        /// A field of edges is one attribute in a file: "8" or "left,top,right,bottom"
        template <>
        struct DataAccessor<UIEdges> {
            static void Save(ISerializer& s, const UIEdges& value, eastl::string_view key) {
                UIAttr::WriteEdges(s, key, value);
            }

            static bool Load(IDeserializer& d, UIEdges& value, eastl::string_view key) {
                return UIAttr::ReadEdges(d, key, value);
            }
        };

        /// The style is flat in a file: it reads and writes the attributes of the object the serializer is in, so there is no @p key element
        template <>
        struct DataAccessor<UIElementStyle> {
            static void Save(ISerializer& s, const UIElementStyle& value, eastl::string_view /*key*/) {
                value.Serialize(s);
            }

            static bool Load(IDeserializer& d, UIElementStyle& value, eastl::string_view /*key*/) {
                value.Deserialize(d);
                return true;
            }
        };

    }  // namespace Ser

}  // namespace BECore
