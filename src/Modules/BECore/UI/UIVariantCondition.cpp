#include "UIVariantCondition.h"

#include <BECore/UI/UIValueText.h>

namespace BECore {

    namespace {

        constexpr char ConditionSeparator = ';';
        constexpr char ConditionValueSeparator = ':';
        constexpr StringView OrientationKey = "orientation";

        /// A range limit of a condition by its name in the text
        struct ConditionLimit {
            StringView key;
            float UIVariantCondition::* member;
        };

        constexpr ConditionLimit ConditionLimits[] = {
            {"minAspect", &UIVariantCondition::minAspect}, {"maxAspect", &UIVariantCondition::maxAspect}, {"minWidth", &UIVariantCondition::minWidth},
            {"maxWidth", &UIVariantCondition::maxWidth},   {"minHeight", &UIVariantCondition::minHeight}, {"maxHeight", &UIVariantCondition::maxHeight},
        };

        bool EqualsIgnoreCase(StringView a, StringView b) {
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

        Optional<UIOrientation> ParseOrientation(StringView text) {
            for (const UIOrientation orientation : EnumUtils<UIOrientation>::Values()) {
                if (EqualsIgnoreCase(text, EnumUtils<UIOrientation>::ToString(orientation))) {
                    return orientation;
                }
            }
            return std::nullopt;
        }

        String Lowercase(StringView text) {
            String result(text.data(), text.size());
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

    String UIVariantCondition::ToText() const {
        String text;
        const auto append = [&text](const String& part) { text += text.empty() ? part : Format("{}{}", ConditionSeparator, part); };
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

    std::expected<UIVariantCondition, String> UIVariantCondition::FromText(StringView text) {
        UIVariantCondition condition;
        for (const StringView rawPart : StringUtils::Split(UIText::Trim(text), ConditionSeparator)) {
            const StringView part = UIText::Trim(rawPart);
            if (part.empty()) {
                continue;
            }
            const size_t colon = part.find(ConditionValueSeparator);
            if (colon == StringView::npos) {
                return std::unexpected(Format("Condition part '{}' has no value", part));
            }
            const StringView key = UIText::Trim(part.substr(0, colon));
            const StringView value = UIText::Trim(part.substr(colon + 1));

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
