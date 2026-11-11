#include "UIBoxStyle.h"

#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <BECore/UI/UIAttributeIO.h>
#include <BECore/UI/UIThemes.h>

namespace BECore {

    namespace {

        bool IsVisible(const Color& color) {
            return color.a > 0;
        }

        void SaveColorIfSet(ISerializer& s, const UIColor& color, eastl::string_view key) {
            if (!(color == UIColor{})) {  // a token with transparent channels (not resolved yet, or an invisible theme colour) is set
                Ser::DataAccessor<UIColor>::Save(s, color, key);
            }
        }

        bool VisitCorners(IPropertyVisitor& v, eastl::string_view key, UICorners& corners) {
            bool changed = false;
            if (v.BeginCompound(key)) {
                changed |= PropVisit::Visit(v, corners.topLeft, "topLeft");
                changed |= PropVisit::Visit(v, corners.topRight, "topRight");
                changed |= PropVisit::Visit(v, corners.bottomRight, "bottomRight");
                changed |= PropVisit::Visit(v, corners.bottomLeft, "bottomLeft");
                v.EndCompound();
            }
            return changed;
        }

        /// The colour of a shadow or glow spec from its token (a spec holds a plain Color); no token, no change
        void ResolveSpecColor(UIThemes& themes, PoolString token, Color& color) {
            if (token.Empty()) {
                return;
            }
            UIColor reference = UIColor::FromToken(token);
            themes.Resolve(reference);
            color = reference;
        }

        /// An edit of the colour in the inspector leaves the token, as for a UIColor field (PropVisit::Visit of UIColor)
        bool VisitShadow(IPropertyVisitor& v, eastl::string_view key, UIShadowSpec& shadow, PoolString& token) {
            bool changed = false;
            if (v.BeginCompound(key)) {
                changed |= PropVisit::Visit(v, shadow.offsetX, "offsetX");
                changed |= PropVisit::Visit(v, shadow.offsetY, "offsetY");
                changed |= PropVisit::Visit(v, shadow.blur, "blur");
                changed |= PropVisit::Visit(v, shadow.spread, "spread");
                if (PropVisit::Visit(v, shadow.color, "color")) {
                    token = PoolString{};
                    changed = true;
                }
                v.EndCompound();
            }
            return changed;
        }

    }  // namespace

    bool UIBoxStyle::operator==(const UIBoxStyle& other) const {
        return cornerRadius == other.cornerRadius && borderWidth == other.borderWidth && borderColor == other.borderColor && gradientColor == other.gradientColor &&
               gradientDirection == other.gradientDirection && shadow == other.shadow && glow == other.glow && highlight == other.highlight && shadowColorToken == other.shadowColorToken &&
               glowColorToken == other.glowColorToken;
    }

    bool UIBoxStyle::HasEffect() const {
        return !cornerRadius.IsZero() || (borderWidth > 0.0f && IsVisible(borderColor)) || gradientDirection != UIGradientDirection::None || IsVisible(shadow.color) || IsVisible(glow.color) ||
               IsVisible(highlight);
    }

    void UIBoxStyle::Serialize(ISerializer& s) const {
        const UIBoxStyle defaults;
        if (!(cornerRadius == defaults.cornerRadius)) {
            UIAttr::WriteCorners(s, "cornerRadius", cornerRadius);
        }
        if (borderWidth != defaults.borderWidth) {
            Ser::DataAccessor<float>::Save(s, borderWidth, "borderWidth");
        }
        SaveColorIfSet(s, borderColor, "borderColor");
        SaveColorIfSet(s, gradientColor, "gradientColor");
        if (gradientDirection != defaults.gradientDirection) {
            Ser::DataAccessor<UIGradientDirection>::Save(s, gradientDirection, "gradientDirection");
        }
        if (!(shadow == defaults.shadow) || !shadowColorToken.Empty()) {
            UIAttr::WriteShadow(s, "shadow", shadow, shadowColorToken);
        }
        if (!(glow == defaults.glow) || !glowColorToken.Empty()) {
            UIAttr::WriteShadow(s, "glow", glow, glowColorToken);
        }
        SaveColorIfSet(s, highlight, "highlight");
    }

    void UIBoxStyle::Deserialize(IDeserializer& d) {
        UIAttr::ReadCorners(d, "cornerRadius", cornerRadius);
        Ser::Load(d, borderWidth, "borderWidth");
        Ser::Load(d, borderColor, "borderColor");
        Ser::Load(d, gradientColor, "gradientColor");
        Ser::Load(d, gradientDirection, "gradientDirection");
        UIAttr::ReadShadow(d, "shadow", shadow, shadowColorToken);
        UIAttr::ReadShadow(d, "glow", glow, glowColorToken);
        Ser::Load(d, highlight, "highlight");
    }

    void UIBoxStyle::ResolveTheme(UIThemes& themes) {
        themes.Resolve(borderColor);
        themes.Resolve(gradientColor);
        themes.Resolve(highlight);
        ResolveSpecColor(themes, shadowColorToken, shadow.color);
        ResolveSpecColor(themes, glowColorToken, glow.color);
    }

    bool UIBoxStyle::VisitProperties(IPropertyVisitor& v) {
        bool changed = false;
        changed |= VisitCorners(v, "cornerRadius", cornerRadius);
        changed |= PropVisit::Visit(v, borderWidth, "borderWidth");
        changed |= PropVisit::Visit(v, borderColor, "borderColor");
        changed |= PropVisit::Visit(v, gradientColor, "gradientColor");
        changed |= PropVisit::Visit(v, gradientDirection, "gradientDirection");
        changed |= VisitShadow(v, "shadow", shadow, shadowColorToken);
        changed |= VisitShadow(v, "glow", glow, glowColorToken);
        changed |= PropVisit::Visit(v, highlight, "highlight");
        return changed;
    }

}  // namespace BECore
