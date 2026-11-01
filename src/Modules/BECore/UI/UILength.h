#pragma once

namespace BECore {

    CORE_ENUM(UIUnit, uint8_t, Pixels, Percent)

    /**
     * @brief A size: a number of UI units, or a percent of the parent's extent on the same axis (width against the parent's
     *        width, height against its height).
     */
    struct UILength {
        float value = 0.0f;
        UIUnit unit = UIUnit::Pixels;

        [[nodiscard]] static constexpr UILength Px(float value) {
            return {value, UIUnit::Pixels};
        }

        [[nodiscard]] static constexpr UILength Percent(float value) {
            return {value, UIUnit::Percent};
        }

        /// @param parentExtent the parent's width or height, whichever axis this length is on
        [[nodiscard]] constexpr float Resolve(float parentExtent) const {
            return unit == UIUnit::Percent ? parentExtent * value * 0.01f : value;
        }

        [[nodiscard]] constexpr bool operator==(const UILength&) const = default;
    };

}  // namespace BECore
