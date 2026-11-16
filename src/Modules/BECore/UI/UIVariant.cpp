#include "UIVariant.h"

#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/UI/UIAttributeIO.h>
#include <cctype>
#include <limits>

namespace BECore {

    namespace {

        constexpr StringView OverridesKey = "overrides";
        constexpr StringView OverrideItem = "set";
        constexpr StringView VariantItem = "variant";

        /// Writes an optional property as an attribute even when it equals T{} (an override "x is 0" must survive); Ser::Save would drop it under skip-defaults
        template <typename T>
        void SaveOptional(ISerializer& s, StringView key, const Optional<T>& value) {
            if (value) {
                Ser::DataAccessor<T>::Save(s, *value, key);
            }
        }

        template <typename T>
        void LoadOptional(IDeserializer& d, StringView key, Optional<T>& out) {
            T value{};
            if (Ser::DataAccessor<T>::Load(d, value, key)) {
                out = value;
            }
        }

        template <typename T>
        void SaveItems(ISerializer& s, const Vector<T>& items, StringView key, StringView itemName) {
            size_t count = items.size();
            if (s.BeginArray(key, itemName, count)) {
                for (const T& item : items) {
                    if (s.BeginArrayElement()) {
                        item.Serialize(s);
                        s.EndArrayElement();
                    }
                }
                s.EndArray();
            }
        }

        template <typename T>
        bool LoadItems(IDeserializer& d, Vector<T>& items, StringView key, StringView itemName) {
            size_t count = 0;
            if (!d.BeginArray(key, itemName, count)) {
                return false;
            }
            items.clear();
            for (size_t i = 0; i < count; ++i) {
                if (d.BeginArrayElement()) {
                    T item;
                    item.Deserialize(d);
                    items.push_back(eastl::move(item));
                    d.EndArrayElement();
                }
            }
            d.EndArray();
            return true;
        }

        void ResetActive(UIElement& element) {
            element.active = element.style;
            for (const auto& child : element.children) {
                ResetActive(*child);
            }
        }

        void ApplyOverride(UIElement& element, const UIElementOverride& override) {
            if (element.name == override.element) {
                override.ApplyTo(element.active);
            }
            for (const auto& child : element.children) {
                ApplyOverride(*child, override);
            }
        }

    }  // namespace

    void UIElementOverride::ApplyTo(UIElementStyle& style) const {
        if (visible) {
            style.visible = *visible;
        }
        if (anchor) {
            style.rect.anchor = *anchor;
        }
        if (anchorMin) {
            style.rect.anchorMin = *anchorMin;
        }
        if (anchorMax) {
            style.rect.anchorMax = *anchorMax;
        }
        if (pivot) {
            style.rect.pivot = *pivot;
        }
        if (x) {
            style.rect.x = *x;
        }
        if (y) {
            style.rect.y = *y;
        }
        if (width) {
            style.rect.width = *width;
        }
        if (height) {
            style.rect.height = *height;
        }
        if (margin) {
            style.rect.margin = *margin;
        }
        if (region) {
            style.rect.region = *region;
        }
        if (direction) {
            style.stack.direction = *direction;
        }
        if (fontSize) {
            style.fontSize = *fontSize;
        }
    }

    UIOverrideMask UIElementOverride::Properties() const {
        UIOverrideMask mask = 0;
        const auto add = [&mask](bool isSet, UIOverrideProperty property) {
            if (isSet) {
                mask |= OverrideBit(property);
            }
        };
        add(visible.has_value(), UIOverrideProperty::Visible);
        add(anchor.has_value(), UIOverrideProperty::Anchor);
        add(anchorMin.has_value(), UIOverrideProperty::AnchorMin);
        add(anchorMax.has_value(), UIOverrideProperty::AnchorMax);
        add(pivot.has_value(), UIOverrideProperty::Pivot);
        add(x.has_value(), UIOverrideProperty::X);
        add(y.has_value(), UIOverrideProperty::Y);
        add(width.has_value(), UIOverrideProperty::Width);
        add(height.has_value(), UIOverrideProperty::Height);
        add(margin.has_value(), UIOverrideProperty::Margin);
        add(region.has_value(), UIOverrideProperty::Region);
        add(direction.has_value(), UIOverrideProperty::Direction);
        add(fontSize.has_value(), UIOverrideProperty::FontSize);
        return mask;
    }

    void UIElementOverride::Serialize(ISerializer& s) const {
        Ser::DataAccessor<PoolString>::Save(s, element, "element");
        SaveOptional(s, "visible", visible);
        SaveOptional(s, "anchor", anchor);
        UIAttr::WriteOptionalVec2Fraction(s, "anchorMin", anchorMin);
        UIAttr::WriteOptionalVec2Fraction(s, "anchorMax", anchorMax);
        UIAttr::WriteOptionalVec2Fraction(s, "pivot", pivot);
        if (x) {
            UIAttr::WriteLength(s, "x", *x);
        }
        if (y) {
            UIAttr::WriteLength(s, "y", *y);
        }
        if (width) {
            UIAttr::WriteLength(s, "width", *width);
        }
        if (height) {
            UIAttr::WriteLength(s, "height", *height);
        }
        UIAttr::WriteOptionalEdges(s, "margin", margin);
        SaveOptional(s, "region", region);
        SaveOptional(s, "direction", direction);
        SaveOptional(s, "fontSize", fontSize);
    }

    void UIElementOverride::Deserialize(IDeserializer& d) {
        Ser::Load(d, element, "element");
        LoadOptional(d, "visible", visible);
        LoadOptional(d, "anchor", anchor);
        UIAttr::ReadOptionalVec2Fraction(d, "anchorMin", anchorMin);
        UIAttr::ReadOptionalVec2Fraction(d, "anchorMax", anchorMax);
        UIAttr::ReadOptionalVec2Fraction(d, "pivot", pivot);
        UIAttr::ReadOptionalLength(d, "x", x);
        UIAttr::ReadOptionalLength(d, "y", y);
        UIAttr::ReadOptionalLength(d, "width", width);
        UIAttr::ReadOptionalLength(d, "height", height);
        UIAttr::ReadOptionalEdges(d, "margin", margin);
        LoadOptional(d, "region", region);
        LoadOptional(d, "direction", direction);
        LoadOptional(d, "fontSize", fontSize);
    }

    void UIVariant::Serialize(ISerializer& s) const {
        const String text = when.ToText();
        s.WriteAttribute("when", StringView(text));
        SaveItems(s, overrides, OverridesKey, OverrideItem);
    }

    void UIVariant::Deserialize(IDeserializer& d) {
        String text;
        if (d.ReadAttribute("when", text)) {
            const auto condition = UIVariantCondition::FromText(StringView(text));
            if (condition) {
                when = *condition;
            } else {
                d.ReportError("when", StringView(condition.error().c_str()));
            }
        }
        LoadItems(d, overrides, OverridesKey, OverrideItem);
    }

    void Ser::DataAccessor<Vector<UIVariant>>::Save(ISerializer& s, const Vector<UIVariant>& variants, StringView key) {
        SaveItems(s, variants, key, VariantItem);
    }

    bool Ser::DataAccessor<Vector<UIVariant>>::Load(IDeserializer& d, Vector<UIVariant>& variants, StringView key) {
        return LoadItems(d, variants, key, VariantItem);
    }

    size_t UIVariants::Apply(UIElement& root, Span<const UIVariant> variants, const Rect& bounds) {
        ResetActive(root);
        size_t matched = 0;
        for (const UIVariant& variant : variants) {
            if (!variant.when.Matches(bounds)) {
                continue;
            }
            ++matched;
            for (const UIElementOverride& override : variant.overrides) {
                ApplyOverride(root, override);
            }
        }
        return matched;
    }

    void UIVariants::Sources(Span<const UIVariant> variants, const Rect& bounds, PoolString element, Vector<UIVariantSource>& sources) {
        sources.clear();
        for (size_t index = 0; index < variants.size(); ++index) {
            const UIVariant& variant = variants[index];
            if (!variant.when.Matches(bounds)) {
                continue;
            }
            UIOverrideMask properties = 0;
            for (const UIElementOverride& override : variant.overrides) {
                if (override.element == element) {
                    properties |= override.Properties();
                }
            }
            if (properties != 0) {
                sources.push_back({index, properties});
            }
        }
    }

    void UIVariants::DescribeSources(Span<const UIVariantSource> sources, String& text) {
        static_assert(EnumUtils<UIOverrideProperty>::Count() <= std::numeric_limits<UIOverrideMask>::digits, "UIOverrideMask has a bit per property");
        text.clear();
        if (sources.empty()) {
            text += "authored";
            return;
        }
        bool firstSource = true;
        for (const UIVariantSource& source : sources) {
            text += firstSource ? "variant " : "; variant ";
            firstSource = false;
            FormatTo(eastl::back_inserter(text), "{}", source.index);
            bool firstProperty = true;
            for (const UIOverrideProperty property : EnumUtils<UIOverrideProperty>::Values()) {
                if ((source.properties & OverrideBit(property)) == 0) {
                    continue;
                }
                text += firstProperty ? ": " : ", ";
                firstProperty = false;
                const StringView name = EnumUtils<UIOverrideProperty>::ToString(property);
                // The files spell a property with a small first letter (anchorMin)
                text += static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
                text.append(name.data() + 1, name.size() - 1);
            }
        }
    }

}  // namespace BECore
