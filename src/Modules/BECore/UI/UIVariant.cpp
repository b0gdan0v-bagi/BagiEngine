#include "UIVariant.h"

#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/UI/UIAttributeIO.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view OverridesKey = "overrides";
        constexpr eastl::string_view OverrideItem = "set";
        constexpr eastl::string_view VariantItem = "variant";

        /// Writes an optional property as an attribute even when it equals T{} (an override "x is 0" must survive); Ser::Save would drop it under skip-defaults
        template <typename T>
        void SaveOptional(ISerializer& s, eastl::string_view key, const std::optional<T>& value) {
            if (value) {
                Ser::DataAccessor<T>::Save(s, *value, key);
            }
        }

        template <typename T>
        void LoadOptional(IDeserializer& d, eastl::string_view key, std::optional<T>& out) {
            T value{};
            if (Ser::DataAccessor<T>::Load(d, value, key)) {
                out = value;
            }
        }

        template <typename T>
        void SaveItems(ISerializer& s, const eastl::vector<T>& items, eastl::string_view key, eastl::string_view itemName) {
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
        bool LoadItems(IDeserializer& d, eastl::vector<T>& items, eastl::string_view key, eastl::string_view itemName) {
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

    void UIElementOverride::Serialize(ISerializer& s) const {
        Ser::DataAccessor<PoolString>::Save(s, element, "element");
        SaveOptional(s, "visible", visible);
        SaveOptional(s, "anchor", anchor);
        SaveOptional(s, "x", x);
        SaveOptional(s, "y", y);
        if (width) {
            UIAttr::WriteLength(s, "width", *width);
        }
        if (height) {
            UIAttr::WriteLength(s, "height", *height);
        }
        SaveOptional(s, "region", region);
        SaveOptional(s, "direction", direction);
        SaveOptional(s, "fontSize", fontSize);
    }

    void UIElementOverride::Deserialize(IDeserializer& d) {
        Ser::Load(d, element, "element");
        LoadOptional(d, "visible", visible);
        LoadOptional(d, "anchor", anchor);
        LoadOptional(d, "x", x);
        LoadOptional(d, "y", y);
        UIAttr::ReadOptionalLength(d, "width", width);
        UIAttr::ReadOptionalLength(d, "height", height);
        LoadOptional(d, "region", region);
        LoadOptional(d, "direction", direction);
        LoadOptional(d, "fontSize", fontSize);
    }

    void UIVariant::Serialize(ISerializer& s) const {
        const eastl::string text = when.ToText();
        s.WriteAttribute("when", eastl::string_view(text.data(), text.size()));
        SaveItems(s, overrides, OverridesKey, OverrideItem);
    }

    void UIVariant::Deserialize(IDeserializer& d) {
        eastl::string text;
        if (d.ReadAttribute("when", text)) {
            const auto condition = UIVariantCondition::FromText(eastl::string_view(text.data(), text.size()));
            if (condition) {
                when = *condition;
            } else {
                d.ReportError("when", eastl::string_view(condition.error().c_str()));
            }
        }
        LoadItems(d, overrides, OverridesKey, OverrideItem);
    }

    void Ser::DataAccessor<eastl::vector<UIVariant>>::Save(ISerializer& s, const eastl::vector<UIVariant>& variants, eastl::string_view key) {
        SaveItems(s, variants, key, VariantItem);
    }

    bool Ser::DataAccessor<eastl::vector<UIVariant>>::Load(IDeserializer& d, eastl::vector<UIVariant>& variants, eastl::string_view key) {
        return LoadItems(d, variants, key, VariantItem);
    }

    size_t UIVariants::Apply(UIElement& root, eastl::span<const UIVariant> variants, const Rect& bounds) {
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

}  // namespace BECore
