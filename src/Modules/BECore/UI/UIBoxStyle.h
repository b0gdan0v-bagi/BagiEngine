#pragma once

#include <BECore/Reflection/DataAccessor.h>
#include <BECore/UI/UIBoxMesh.h>
#include <BECore/UI/UIColor.h>

namespace BECore {

    class UIThemes;

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
     * - `shadow` and `glow` are "offsetX,offsetY,blur,spread,color" (UIShadowSpec; the colour as "#RRGGBBAA", "r,g,b,a" or a theme token "@name"). Both are paint of the same kind and drawn before the
     *   fill, the shadow first. They go beyond the rect and are neither layout nor validated. A colour with no alpha is no shadow.
     * - `highlight`: the colour of a one-unit line just inside the top edge, following the top corners (a raised look). No alpha, no line.
     *
     * Every colour attribute may be a theme token (`@name`, UIThemes): `borderColor`, `gradientColor` and `highlight` are UIColor fields, the colours of `shadow` and `glow` keep
     * theirs in `shadowColorToken` / `glowColorToken`. A token whose theme colour is invisible is still "set": it is saved and compared.
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
        /// The theme tokens (`@name`) of the colours of `shadow` and `glow`, empty for a literal. UIShadowSpec is a mesh spec and holds a plain Color, so the reference sits
        /// beside it; UIElement::ApplyTheme() resolves it into the spec's colour
        PoolString shadowColorToken;
        PoolString glowColorToken;

        [[nodiscard]] bool operator==(const UIBoxStyle& other) const;

        /// The box draws more than a flat rect: a rounded corner, a visible border, a gradient or a visible shadow, glow or highlight. False for the default style and for one
        /// whose only settings cannot show (a border colour with no width)
        [[nodiscard]] bool HasEffect() const;

        /// The gradient the mesh builder takes: the direction with @p end as the far colour
        [[nodiscard]] UIGradientSpec MakeGradient(const Color& end) const {
            return {gradientDirection, end};
        }

        /// Writes the current theme's colours into every colour that is a token, the shadow's and the glow's included (UIElement::ApplyTheme)
        void ResolveTheme(UIThemes& themes);

        void Serialize(ISerializer& s) const;
        void Deserialize(IDeserializer& d);
        bool VisitProperties(IPropertyVisitor& v);
    };

    namespace Ser {

        /// The box style is flat in a file, like UIElementStyle: it reads and writes the attributes of the object the serializer is in
        template <>
        struct DataAccessor<UIBoxStyle> {
            static void Save(ISerializer& s, const UIBoxStyle& value, StringView /*key*/) {
                value.Serialize(s);
            }

            static bool Load(IDeserializer& d, UIBoxStyle& value, StringView /*key*/) {
                value.Deserialize(d);
                return true;
            }
        };

    }  // namespace Ser

}  // namespace BECore
