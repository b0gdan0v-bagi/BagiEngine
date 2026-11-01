#include "UIElementStyle.h"

#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <BECore/UI/UIAttributeIO.h>

namespace BECore {

    namespace {

        /// Writes a plain value (number, bool, enum) when it differs from the style's default. Not Ser::Save: that one also drops a value equal to T{} when the
        /// serializer skips defaults, and `visible` false is exactly that while the default of the style is true.
        template <typename T>
        void SaveIfChanged(ISerializer& s, const T& value, const T& defaultValue, eastl::string_view key) {
            if (!(value == defaultValue)) {
                Ser::DataAccessor<T>::Save(s, value, key);
            }
        }

        void SaveLengthIfChanged(ISerializer& s, const UILength& value, const UILength& defaultValue, eastl::string_view key) {
            if (!(value == defaultValue)) {
                UIAttr::WriteLength(s, key, value);
            }
        }

        void SaveEdgesIfChanged(ISerializer& s, const UIEdges& value, const UIEdges& defaultValue, eastl::string_view key) {
            if (!(value == defaultValue)) {
                UIAttr::WriteEdges(s, key, value);
            }
        }

        bool VisitLength(IPropertyVisitor& v, eastl::string_view key, UILength& length) {
            bool changed = false;
            if (v.BeginCompound(key)) {
                changed |= PropVisit::Visit(v, length.value, "value");
                changed |= PropVisit::Visit(v, length.unit, "unit");
                v.EndCompound();
            }
            return changed;
        }

        bool VisitOptionalLength(IPropertyVisitor& v, eastl::string_view key, std::optional<UILength>& length) {
            return length && VisitLength(v, key, *length);
        }

    }  // namespace

    void UIElementStyle::Serialize(ISerializer& s) const {
        const UIElementStyle defaults;
        SaveIfChanged(s, visible, defaults.visible, "visible");
        SaveIfChanged(s, rect.anchor, defaults.rect.anchor, "anchor");
        SaveIfChanged(s, rect.x, defaults.rect.x, "x");
        SaveIfChanged(s, rect.y, defaults.rect.y, "y");
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
        SaveIfChanged(s, stack.direction, defaults.stack.direction, "direction");
        SaveIfChanged(s, stack.spacing, defaults.stack.spacing, "spacing");
        SaveEdgesIfChanged(s, stack.padding, defaults.stack.padding, "padding");
        SaveIfChanged(s, stack.wrap, defaults.stack.wrap, "wrap");
        SaveIfChanged(s, stack.crossAlign, defaults.stack.crossAlign, "crossAlign");
        SaveIfChanged(s, fontSize, defaults.fontSize, "fontSize");
    }

    void UIElementStyle::Deserialize(IDeserializer& d) {
        Ser::Load(d, visible, "visible");
        Ser::Load(d, rect.anchor, "anchor");
        Ser::Load(d, rect.x, "x");
        Ser::Load(d, rect.y, "y");
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
        Ser::Load(d, stack.direction, "direction");
        Ser::Load(d, stack.spacing, "spacing");
        UIAttr::ReadEdges(d, "padding", stack.padding);
        Ser::Load(d, stack.wrap, "wrap");
        Ser::Load(d, stack.crossAlign, "crossAlign");
        Ser::Load(d, fontSize, "fontSize");
    }

    bool UIElementStyle::VisitProperties(IPropertyVisitor& v) {
        bool changed = false;
        changed |= PropVisit::Visit(v, visible, "visible");
        changed |= PropVisit::Visit(v, rect.anchor, "anchor");
        changed |= PropVisit::Visit(v, rect.x, "x");
        changed |= PropVisit::Visit(v, rect.y, "y");
        changed |= VisitLength(v, "width", rect.width);
        changed |= VisitLength(v, "height", rect.height);
        changed |= VisitOptionalLength(v, "minWidth", rect.minWidth);
        changed |= VisitOptionalLength(v, "maxWidth", rect.maxWidth);
        changed |= VisitOptionalLength(v, "minHeight", rect.minHeight);
        changed |= VisitOptionalLength(v, "maxHeight", rect.maxHeight);
        changed |= PropVisit::Visit(v, rect.aspectRatio, "aspectRatio");
        changed |= PropVisit::Visit(v, rect.aspectMode, "aspectMode");
        changed |= PropVisit::Visit(v, rect.flex, "flex");
        changed |= PropVisit::Visit(v, stack.direction, "direction");
        changed |= PropVisit::Visit(v, stack.spacing, "spacing");
        changed |= PropVisit::Visit(v, stack.wrap, "wrap");
        changed |= PropVisit::Visit(v, stack.crossAlign, "crossAlign");
        changed |= PropVisit::Visit(v, fontSize, "fontSize");
        return changed;
    }

}  // namespace BECore
