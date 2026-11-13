#pragma once

namespace BECore {

    class XmlNode;

    /**
     * @brief Settings of the in-game UI, read from `config/UIConfig.xml`.
     *
     * ```xml
     * <UIConfig userScale="1.0" minTouchSize="32" hotReloadInterval="0.5" stateTransition="0"/>
     * ```
     */
    struct UISettings {
        /// The accessibility scale of the UI: it multiplies the scale of every Screen canvas
        float userScale = 1.0f;

        /// The smallest side of an interactive element in window units (what the user sees, whatever the canvas scale): the layout validator flags a smaller one
        float minTouchSize = 32.0f;

        /// How often, in seconds of game time, a document that comes from a file looks at its files and loads itself again when one changed (the RmlUi host); 0 turns it off
        float hotReloadInterval = 0.5f;

        /// How long, in seconds of game time, the look of a control takes to ease from one state (rest, hover, pressed, disabled, selected) to the next: its box, the tint of its picture and the
        /// colour of its text. 0 is the one-frame change of old and the default; an element's own `stateDuration` overrides it. Kept in 0 to `kMaxStateTransition`
        float stateTransition = 0.0f;

        /// The longest state transition, a setting or an element's own
        static constexpr float kMaxStateTransition = 2.0f;

        /// @p seconds as a state transition: a NaN or a negative number is 0, a long one is cut
        [[nodiscard]] static float ClampStateTransition(float seconds);

        /// The user scale is kept inside this range, so a bad value cannot make the UI vanish or fill the screen
        static constexpr float kMinUserScale = 0.5f;
        static constexpr float kMaxUserScale = 3.0f;

        [[nodiscard]] static float ClampUserScale(float scale);

        /// Reads @p node over the defaults: a missing or malformed attribute keeps its default
        [[nodiscard]] static UISettings FromConfig(const XmlNode& node);
    };

}  // namespace BECore
