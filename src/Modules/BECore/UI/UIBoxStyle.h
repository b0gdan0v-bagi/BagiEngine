#pragma once

#include <BECore/Reflection/DataAccessor.h>
#include <BECore/UI/UIBoxMesh.h>
#include <BECore/UI/UIColor.h>

namespace BECore {

    /**
     * @brief How an element draws its box: rounded corners, a border, a gradient, a shadow, a glow and a highlight (UIBoxMesh draws it).
     *
     * Like UIElementStyle it is flat in a file: its properties are attributes of the element itself (`cornerRadius`, `borderWidth`, `borderColor`, `gradientColor`,
     * `gradientDirection`, `shadow`, `glow`, `highlight`). Only a value that differs from the default is written, and every default is "no effect": an element with the default
     * box style draws a plain `DrawFilledRect` of its `color`, exactly as before the box existed.
     *
     * - `cornerRadius` is one number or "topLeft,topRight,bottomRight,bottomLeft" in UI units; a radius that does not fit the rect shrinks (UICorners::Clamped).
     * - `borderWidth` in UI units and `borderColor`: a ring inside the rect, drawn over the fill. Nothing for a width of 0 or a colour with no alpha.
     * - `gradientDirection` (`None`, `Vertical` top to bottom, `Horizontal` left to right) and `gradientColor`: the element's `color` is the start, `gradientColor` the end.
     * - `shadow` and `glow` are "offsetX,offsetY,blur,spread,color" (UIShadowSpec; the colour as "#RRGGBBAA" or "r,g,b,a"). Both are paint of the same kind and drawn before the
     *   fill, the shadow first. They go beyond the rect and are neither layout nor validated. A colour with no alpha is no shadow.
     * - `highlight`: the colour of a one-unit line just inside the top edge, following the top corners (a raised look). No alpha, no line.
     *
     * The names must not clash with a field of a control (`UIControlsTest` round-trips every control with everything set).
     */
    struct UIBoxStyle {
        UICorners cornerRadius;
        float borderWidth = 0.0f;
        UIColor borderColor;
        UIColor gradientColor;
        UIGradientDirection gradientDirection = UIGradientDirection::None;
        UIShadowSpec shadow;
        UIShadowSpec glow;
        UIColor highlight;

        [[nodiscard]] bool operator==(const UIBoxStyle& other) const;

        /// The box draws more than a flat rect: a rounded corner, a visible border, a gradient or a visible shadow, glow or highlight. False for the default style and for one
        /// whose only settings cannot show (a border colour with no width)
        [[nodiscard]] bool HasEffect() const;

        /// The gradient the mesh builder takes: the direction with @p end as the far colour
        [[nodiscard]] UIGradientSpec MakeGradient(const Color& end) const {
            return {gradientDirection, end};
        }

        void Serialize(ISerializer& s) const;
        void Deserialize(IDeserializer& d);
        bool VisitProperties(IPropertyVisitor& v);
    };

    namespace Ser {

        /// The box style is flat in a file, like UIElementStyle: it reads and writes the attributes of the object the serializer is in
        template <>
        struct DataAccessor<UIBoxStyle> {
            static void Save(ISerializer& s, const UIBoxStyle& value, eastl::string_view /*key*/) {
                value.Serialize(s);
            }

            static bool Load(IDeserializer& d, UIBoxStyle& value, eastl::string_view /*key*/) {
                value.Deserialize(d);
                return true;
            }
        };

    }  // namespace Ser

}  // namespace BECore
