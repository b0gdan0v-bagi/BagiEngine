#include "UIVariantCondition.h"

#include <BECore/UI/UIValueText.h>

namespace BECore {

    namespace {

        constexpr char ConditionSeparator = ';';
        constexpr char ConditionValueSeparator = ':';
        constexpr eastl::string_view OrientationKey = "orientation";

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

        bool InRange(float value, float minimum, float maximum) {
            return (minimum <= 0.0f || value >= minimum) && (maximum <= 0.0f || value <= maximum);
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
        for (const eastl::string_view rawPart : StringUtils::Split(UIText::Trim(text), ConditionSeparator)) {
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

}  // namespace BECore
