#include "UIElementStyle.h"

#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <BECore/UI/UIAttributeIO.h>

namespace BECore {

    namespace {

        /// Writes a plain value (number, bool, enum) when it differs from the style's default. Not Ser::Save: that one also drops a value equal to T{} when the
        /// serializer skips defaults, and `visible` false is exactly that while the default of the style is true.
        template <typename T>
        void SaveIfChanged(ISerializer& s, const T& value, const T& defaultValue, StringView key) {
            if (!(value == defaultValue)) {
                Ser::DataAccessor<T>::Save(s, value, key);
            }
        }

        void SaveLengthIfChanged(ISerializer& s, const UILength& value, const UILength& defaultValue, StringView key) {
            if (!(value == defaultValue)) {
                UIAttr::WriteLength(s, key, value);
            }
        }

        void SaveEdgesIfChanged(ISerializer& s, const UIEdges& value, const UIEdges& defaultValue, StringView key) {
            if (!(value == defaultValue)) {
                UIAttr::WriteEdges(s, key, value);
            }
        }

        bool VisitLength(IPropertyVisitor& v, StringView key, UILength& length) {
            bool changed = false;
            if (v.BeginCompound(key)) {
                changed |= PropVisit::Visit(v, length.value, "value");
                changed |= PropVisit::Visit(v, length.unit, "unit");
                v.EndCompound();
            }
            return changed;
        }

        bool VisitVec2(IPropertyVisitor& v, StringView key, Vec2& value) {
            bool changed = false;
            if (v.BeginCompound(key)) {
                changed |= PropVisit::Visit(v, value.x, "x");
                changed |= PropVisit::Visit(v, value.y, "y");
                v.EndCompound();
            }
            return changed;
        }

        bool VisitOptionalVec2(IPropertyVisitor& v, StringView key, Optional<Vec2>& value) {
            return value && VisitVec2(v, key, *value);
        }

        bool VisitOptionalLength(IPropertyVisitor& v, StringView key, Optional<UILength>& length) {
            return length && VisitLength(v, key, *length);
        }

    }  // namespace

    void UIElementStyle::Serialize(ISerializer& s) const {
        const UIElementStyle defaults;
        SaveIfChanged(s, visible, defaults.visible, "visible");
        SaveIfChanged(s, rect.anchor, defaults.rect.anchor, "anchor");
        UIAttr::WriteOptionalVec2Fraction(s, "anchorMin", rect.anchorMin);
        UIAttr::WriteOptionalVec2Fraction(s, "anchorMax", rect.anchorMax);
        UIAttr::WriteOptionalVec2Fraction(s, "pivot", rect.pivot);
        SaveLengthIfChanged(s, rect.x, defaults.rect.x, "x");
        SaveLengthIfChanged(s, rect.y, defaults.rect.y, "y");
        SaveLengthIfChanged(s, rect.width, defaults.rect.width, "width");
        SaveLengthIfChanged(s, rect.height, defaults.rect.height, "height");
        UIAttr::WriteOptionalLength(s, "minWidth", rect.minWidth);
        UIAttr::WriteOptionalLength(s, "maxWidth", rect.maxWidth);
        UIAttr::WriteOptionalLength(s, "minHeight", rect.minHeight);
        UIAttr::WriteOptionalLength(s, "maxHeight", rect.maxHeight);
        SaveIfChanged(s, rect.aspectRatio, defaults.rect.aspectRatio, "aspectRatio");
        SaveIfChanged(s, rect.aspectMode, defaults.rect.aspectMode, "aspectMode");
        SaveEdgesIfChanged(s, rect.margin, defaults.rect.margin, "margin");
        SaveIfChanged(s, rect.flex, defaults.rect.flex, "flex");
        SaveIfChanged(s, rect.region, defaults.rect.region, "region");
        SaveIfChanged(s, stack.direction, defaults.stack.direction, "direction");
        SaveIfChanged(s, stack.spacing, defaults.stack.spacing, "spacing");
        SaveEdgesIfChanged(s, stack.padding, defaults.stack.padding, "padding");
        SaveIfChanged(s, stack.wrap, defaults.stack.wrap, "wrapChildren");
        SaveIfChanged(s, stack.crossAlign, defaults.stack.crossAlign, "crossAlign");
        SaveIfChanged(s, fontSize, defaults.fontSize, "fontSize");
    }

    void UIElementStyle::Deserialize(IDeserializer& d) {
        Ser::Load(d, visible, "visible");
        Ser::Load(d, rect.anchor, "anchor");
        UIAttr::ReadOptionalVec2Fraction(d, "anchorMin", rect.anchorMin);
        UIAttr::ReadOptionalVec2Fraction(d, "anchorMax", rect.anchorMax);
        UIAttr::ReadOptionalVec2Fraction(d, "pivot", rect.pivot);
        UIAttr::ReadLength(d, "x", rect.x);
        UIAttr::ReadLength(d, "y", rect.y);
        UIAttr::ReadLength(d, "width", rect.width);
        UIAttr::ReadLength(d, "height", rect.height);
        UIAttr::ReadOptionalLength(d, "minWidth", rect.minWidth);
        UIAttr::ReadOptionalLength(d, "maxWidth", rect.maxWidth);
        UIAttr::ReadOptionalLength(d, "minHeight", rect.minHeight);
        UIAttr::ReadOptionalLength(d, "maxHeight", rect.maxHeight);
        Ser::Load(d, rect.aspectRatio, "aspectRatio");
        Ser::Load(d, rect.aspectMode, "aspectMode");
        UIAttr::ReadEdges(d, "margin", rect.margin);
        Ser::Load(d, rect.flex, "flex");
        Ser::Load(d, rect.region, "region");
        Ser::Load(d, stack.direction, "direction");
        Ser::Load(d, stack.spacing, "spacing");
        UIAttr::ReadEdges(d, "padding", stack.padding);
        Ser::Load(d, stack.wrap, "wrapChildren");
        Ser::Load(d, stack.crossAlign, "crossAlign");
        Ser::Load(d, fontSize, "fontSize");
    }

    bool UIElementStyle::VisitProperties(IPropertyVisitor& v) {
        bool changed = false;
        changed |= PropVisit::Visit(v, visible, "visible");
        changed |= PropVisit::Visit(v, rect.anchor, "anchor");
        changed |= VisitOptionalVec2(v, "anchorMin", rect.anchorMin);
        changed |= VisitOptionalVec2(v, "anchorMax", rect.anchorMax);
        changed |= VisitOptionalVec2(v, "pivot", rect.pivot);
        changed |= VisitLength(v, "x", rect.x);
        changed |= VisitLength(v, "y", rect.y);
        changed |= VisitLength(v, "width", rect.width);
        changed |= VisitLength(v, "height", rect.height);
        changed |= VisitOptionalLength(v, "minWidth", rect.minWidth);
        changed |= VisitOptionalLength(v, "maxWidth", rect.maxWidth);
        changed |= VisitOptionalLength(v, "minHeight", rect.minHeight);
        changed |= VisitOptionalLength(v, "maxHeight", rect.maxHeight);
        changed |= PropVisit::Visit(v, rect.aspectRatio, "aspectRatio");
        changed |= PropVisit::Visit(v, rect.aspectMode, "aspectMode");
        changed |= PropVisit::Visit(v, rect.flex, "flex");
        changed |= PropVisit::Visit(v, rect.region, "region");
        changed |= PropVisit::Visit(v, stack.direction, "direction");
        changed |= PropVisit::Visit(v, stack.spacing, "spacing");
        changed |= PropVisit::Visit(v, stack.wrap, "wrapChildren");
        changed |= PropVisit::Visit(v, stack.crossAlign, "crossAlign");
        changed |= PropVisit::Visit(v, fontSize, "fontSize");
        return changed;
    }

}  // namespace BECore
