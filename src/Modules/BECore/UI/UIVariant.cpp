#include "UIVariant.h"

#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/UI/UIAttributeIO.h>
#include <BECore/UI/UIValueText.h>

namespace BECore {

    namespace {

        constexpr char ConditionSeparator = ';';
        constexpr char ConditionValueSeparator = ':';
        constexpr eastl::string_view OrientationKey = "orientation";
        constexpr eastl::string_view OverridesKey = "overrides";
        constexpr eastl::string_view OverrideItem = "set";
        constexpr eastl::string_view VariantItem = "variant";

        /// A range limit of a condition by its name in the text
        struct ConditionLimit {
            eastl::string_view key;
            float UIVariantCondition::* member;
        };

        constexpr ConditionLimit ConditionLimits[] = {
            {"minAspect", &UIVariantCondition::minAspect}, {"maxAspect", &UIVariantCondition::maxAspect}, {"minWidth", &UIVariantCondition::minWidth},
            {"maxWidth", &UIVariantCondition::maxWidth},   {"minHeight", &UIVariantCondition::minHeight}, {"maxHeight", &UIVariantCondition::maxHeight},
        };

        bool EqualsIgnoreCase(eastl::string_view a, eastl::string_view b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        }

        std::optional<UIOrientation> ParseOrientation(eastl::string_view text) {
            for (const UIOrientation orientation : EnumUtils<UIOrientation>::Values()) {
                if (EqualsIgnoreCase(text, EnumUtils<UIOrientation>::ToString(orientation))) {
                    return orientation;
                }
            }
            return std::nullopt;
        }

        eastl::string Lowercase(eastl::string_view text) {
            eastl::string result(text.data(), text.size());
            for (char& c : result) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return result;
        }

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

        bool InRange(float value, float minimum, float maximum) {
            return (minimum <= 0.0f || value >= minimum) && (maximum <= 0.0f || value <= maximum);
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

    bool UIVariantCondition::Matches(const Rect& bounds) const {
        const bool portrait = bounds.h >= bounds.w;
        if ((orientation == UIOrientation::Portrait && !portrait) || (orientation == UIOrientation::Landscape && portrait)) {
            return false;
        }
        if ((minAspect > 0.0f || maxAspect > 0.0f) && bounds.h <= 0.0f) {
            return false;
        }
        const float aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 0.0f;
        return InRange(aspect, minAspect, maxAspect) && InRange(bounds.w, minWidth, maxWidth) && InRange(bounds.h, minHeight, maxHeight);
    }

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
        if (direction) {
            style.stack.direction = *direction;
        }
        if (fontSize) {
            style.fontSize = *fontSize;
        }
    }

    eastl::string UIVariantCondition::ToText() const {
        eastl::string text;
        const auto append = [&text](const eastl::string& part) { text += text.empty() ? part : Format("{}{}", ConditionSeparator, part); };
        if (orientation != UIOrientation::Any) {
            append(Format("{}{}{}", OrientationKey, ConditionValueSeparator, Lowercase(EnumUtils<UIOrientation>::ToString(orientation))));
        }
        for (const ConditionLimit& limit : ConditionLimits) {
            if (this->*limit.member != 0.0f) {
                append(Format("{}{}{}", limit.key, ConditionValueSeparator, this->*limit.member));
            }
        }
        return text;
    }

    std::expected<UIVariantCondition, eastl::string> UIVariantCondition::FromText(eastl::string_view text) {
        UIVariantCondition condition;
        for (const eastl::string_view rawPart : String::Split(UIText::Trim(text), ConditionSeparator)) {
            const eastl::string_view part = UIText::Trim(rawPart);
            if (part.empty()) {
                continue;
            }
            const size_t colon = part.find(ConditionValueSeparator);
            if (colon == eastl::string_view::npos) {
                return std::unexpected(Format("Condition part '{}' has no value", part));
            }
            const eastl::string_view key = UIText::Trim(part.substr(0, colon));
            const eastl::string_view value = UIText::Trim(part.substr(colon + 1));

            if (EqualsIgnoreCase(key, OrientationKey)) {
                const auto parsed = ParseOrientation(value);
                if (!parsed) {
                    return std::unexpected(Format("Unknown orientation '{}'", value));
                }
                condition.orientation = *parsed;
                continue;
            }
            const auto limit = std::find_if(std::begin(ConditionLimits), std::end(ConditionLimits), [key](const ConditionLimit& l) { return EqualsIgnoreCase(key, l.key); });
            if (limit == std::end(ConditionLimits)) {
                return std::unexpected(Format("Unknown condition '{}'", key));
            }
            const auto number = UIText::ParseFloat(value);
            if (!number) {
                return std::unexpected(Format("Condition '{}' needs a number, got '{}'", key, value));
            }
            condition.*(limit->member) = *number;
        }
        return condition;
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
