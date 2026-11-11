#include "UITextEffects.h"

#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIAttributeIO.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view OutlineKey = "textOutline";
        constexpr eastl::string_view ShadowKey = "textShadow";

    }  // namespace

    bool UITextEffects::operator==(const UITextEffects& other) const {
        return outlineWidth == other.outlineWidth && outlineColor == other.outlineColor && shadowOffsetX == other.shadowOffsetX && shadowOffsetY == other.shadowOffsetY &&
               shadowColor == other.shadowColor;
    }

    TextEffects UITextEffects::ToPixels(float scale, float alphaFactor) const {
        TextEffects effects;
        if (outlineWidth > 0.0f && outlineColor.a > 0) {
            effects.outlineWidth = static_cast<float>(FontAtlasLibrary::QuantizeOutline(outlineWidth * scale));
            effects.outlineColor = WithAlpha(outlineColor, alphaFactor);
        }
        if (shadowColor.a > 0) {
            effects.shadowOffsetX = std::round(shadowOffsetX * scale);
            effects.shadowOffsetY = std::round(shadowOffsetY * scale);
            effects.shadowColor = WithAlpha(shadowColor, alphaFactor);
        }
        return effects;
    }

    void UITextEffects::Serialize(ISerializer& s) const {
        const UITextEffects defaults;
        if (outlineWidth != defaults.outlineWidth || !(outlineColor == defaults.outlineColor)) {
            UIAttr::WriteTextOutline(s, OutlineKey, outlineWidth, outlineColor);
        }
        if (shadowOffsetX != defaults.shadowOffsetX || shadowOffsetY != defaults.shadowOffsetY || !(shadowColor == defaults.shadowColor)) {
            UIAttr::WriteTextShadow(s, ShadowKey, shadowOffsetX, shadowOffsetY, shadowColor);
        }
    }

    void UITextEffects::Deserialize(IDeserializer& d) {
        UIAttr::ReadTextOutline(d, OutlineKey, outlineWidth, outlineColor);
        UIAttr::ReadTextShadow(d, ShadowKey, shadowOffsetX, shadowOffsetY, shadowColor);
    }

    bool UITextEffects::VisitProperties(IPropertyVisitor& v) {
        bool changed = false;
        changed |= PropVisit::Visit(v, outlineWidth, "outlineWidth");
        changed |= PropVisit::Visit(v, outlineColor, "outlineColor");
        changed |= PropVisit::Visit(v, shadowOffsetX, "shadowOffsetX");
        changed |= PropVisit::Visit(v, shadowOffsetY, "shadowOffsetY");
        changed |= PropVisit::Visit(v, shadowColor, "shadowColor");
        return changed;
    }

}  // namespace BECore
