#pragma once

namespace BECore {

    class XmlNode;

    /**
     * @brief Settings of the in-game UI, read from `config/UIConfig.xml`.
     *
     * ```xml
     * <UIConfig userScale="1.0" minTouchSize="32"/>
     * ```
     */
    struct UISettings {
        /// The accessibility scale of the UI: it multiplies the scale of every Screen canvas
        float userScale = 1.0f;

        /// The smallest side of an interactive element in window units (what the user sees, whatever the canvas scale): the layout validator flags a smaller one
        float minTouchSize = 32.0f;

        /// The user scale is kept inside this range, so a bad value cannot make the UI vanish or fill the screen
        static constexpr float kMinUserScale = 0.5f;
        static constexpr float kMaxUserScale = 3.0f;

        [[nodiscard]] static float ClampUserScale(float scale);

        /// Reads @p node over the defaults: a missing or malformed attribute keeps its default
        [[nodiscard]] static UISettings FromConfig(const XmlNode& node);
    };

}  // namespace BECore
