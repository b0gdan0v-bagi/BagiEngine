#include "UIBoxStyle.h"

#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <BECore/UI/UIAttributeIO.h>

namespace BECore {

    namespace {

        bool IsVisible(const Color& color) {
            return color.a > 0;
        }

        void SaveColorIfSet(ISerializer& s, const UIColor& color, eastl::string_view key) {
            if (!ColorsEqual(color, UIColor{})) {
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

        bool VisitShadow(IPropertyVisitor& v, eastl::string_view key, UIShadowSpec& shadow) {
            bool changed = false;
            if (v.BeginCompound(key)) {
                changed |= PropVisit::Visit(v, shadow.offsetX, "offsetX");
                changed |= PropVisit::Visit(v, shadow.offsetY, "offsetY");
                changed |= PropVisit::Visit(v, shadow.blur, "blur");
                changed |= PropVisit::Visit(v, shadow.spread, "spread");
                changed |= PropVisit::Visit(v, shadow.color, "color");
                v.EndCompound();
            }
            return changed;
        }

    }  // namespace

    bool UIBoxStyle::operator==(const UIBoxStyle& other) const {
        return cornerRadius == other.cornerRadius && borderWidth == other.borderWidth && ColorsEqual(borderColor, other.borderColor) && ColorsEqual(gradientColor, other.gradientColor) &&
               gradientDirection == other.gradientDirection && shadow == other.shadow && glow == other.glow && ColorsEqual(highlight, other.highlight);
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
        if (!(shadow == defaults.shadow)) {
            UIAttr::WriteShadow(s, "shadow", shadow);
        }
        if (!(glow == defaults.glow)) {
            UIAttr::WriteShadow(s, "glow", glow);
        }
        SaveColorIfSet(s, highlight, "highlight");
    }

    void UIBoxStyle::Deserialize(IDeserializer& d) {
        UIAttr::ReadCorners(d, "cornerRadius", cornerRadius);
        Ser::Load(d, borderWidth, "borderWidth");
        Ser::Load(d, borderColor, "borderColor");
        Ser::Load(d, gradientColor, "gradientColor");
        Ser::Load(d, gradientDirection, "gradientDirection");
        UIAttr::ReadShadow(d, "shadow", shadow);
        UIAttr::ReadShadow(d, "glow", glow);
        Ser::Load(d, highlight, "highlight");
    }

    bool UIBoxStyle::VisitProperties(IPropertyVisitor& v) {
        bool changed = false;
        changed |= VisitCorners(v, "cornerRadius", cornerRadius);
        changed |= PropVisit::Visit(v, borderWidth, "borderWidth");
        changed |= PropVisit::Visit(v, borderColor, "borderColor");
        changed |= PropVisit::Visit(v, gradientColor, "gradientColor");
        changed |= PropVisit::Visit(v, gradientDirection, "gradientDirection");
        changed |= VisitShadow(v, "shadow", shadow);
        changed |= VisitShadow(v, "glow", glow);
        changed |= PropVisit::Visit(v, highlight, "highlight");
        return changed;
    }

}  // namespace BECore
