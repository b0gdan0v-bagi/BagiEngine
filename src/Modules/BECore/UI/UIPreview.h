#pragma once

namespace BECore {

    /// The shapes the editor viewport can be forced to, to look at a layout in several of them without resizing the window. Free follows the panel.
    CORE_ENUM(ViewportAspect, uint8_t, Free, Wide16x9, Standard4x3, UltraWide21x9, Portrait9x16)

    /// A safe area the editor viewport pretends the output has: a notch at the top, or the curved edges and the home bar of a phone held sideways
    CORE_ENUM(SafeAreaSimulation, uint8_t, None, TopNotch, SideInsets)

    /**
     * @brief The pure part of the editor viewport's UI preview: sizes and safe areas. The widget draws, this decides the numbers, so they are tested.
     *
     * The insets are a share of the short side of the output, the same shapes that the layout matrix of the tests uses (`tests/Common/UILayoutMatrix.h`).
     */
    class UIPreview {
    public:
        static constexpr float kNotchShare = 0.07f;        ///< the top inset of TopNotch
        static constexpr float kSideInsetShare = 0.09f;    ///< the left and right inset of SideInsets
        static constexpr float kBottomInsetShare = 0.04f;  ///< the bottom inset of SideInsets (the home bar)

        /// width / height of a preset; 0 for Free
        [[nodiscard]] static float AspectOf(ViewportAspect aspect);

        /**
         * @brief The size in whole pixels of the viewport image: the largest rect of the preset's aspect that fits the available area (the available area itself for Free).
         *
         * At least 1 x 1 pixel, and 0 x 0 for an available area with no size.
         */
        [[nodiscard]] static std::pair<uint32_t, uint32_t> FitSize(ViewportAspect aspect, float availableWidth, float availableHeight);

        /// The safe area of an output of this size, in output pixels, origin at the top left of the output (the whole output for None)
        [[nodiscard]] static Rect SimulatedSafeArea(SafeAreaSimulation simulation, float width, float height);
    };

}  // namespace BECore
