#pragma once

namespace BECore {

    /// The shapes the editor viewport can be forced to, to look at a layout in several of them without resizing the window. Free follows the panel.
    CORE_ENUM(ViewportAspect, uint8_t, Free, Wide16x9, Standard4x3, UltraWide21x9, Portrait9x16)

    /// A safe area the editor viewport pretends the output has: a notch at the top, or the curved edges and the home bar of a phone held sideways,
    /// or the cut-out and the home bar of the chosen PreviewDevice (Device)
    CORE_ENUM(SafeAreaSimulation, uint8_t, None, TopNotch, SideInsets, Device)

    /// A device the viewport can show the scene at: its logical size in points, in portrait, and the insets of its cut-out and home bar
    struct PreviewDevice {
        eastl::string_view name;  ///< the label of the picker and the key under which the choice is stored, unique
        uint32_t width = 0;       ///< logical points, portrait (width < height)
        uint32_t height = 0;
        float cutout = 0.0f;   ///< the top inset in portrait, the inset of both sides in landscape (0 for a screen with no cut-out)
        float homeBar = 0.0f;  ///< the bottom inset, portrait and landscape
    };

    /// The smallest and the largest side of the viewport image, in pixels (a device or custom size is clamped to them)
    inline constexpr uint32_t kMinPreviewSide = 64;
    inline constexpr uint32_t kMaxPreviewSide = 4096;

    /// What shape the viewport image has. Which source wins: `custom`, else `device >= 0`, else `aspect`.
    struct PreviewShape {
        ViewportAspect aspect = ViewportAspect::Free;
        int32_t device = -1;  ///< the index in UIPreview::GetDevices(), -1 for none
        bool custom = false;  ///< a size of its own (customWidth x customHeight)
        uint32_t customWidth = 390;
        uint32_t customHeight = 844;
        bool rotated = false;  ///< swaps width and height of a device or custom size, turns the ratio of an aspect preset (ignored for Free)

        bool operator==(const PreviewShape&) const = default;
    };

    /// The size of the viewport image (the render target) and how much of it is shown: the image is drawn at width * zoom x height * zoom
    struct PreviewTarget {
        uint32_t width = 0;
        uint32_t height = 0;
        float zoom = 1.0f;
    };

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

        /// The devices of the picker, in portrait, in a fixed order (the index is PreviewShape::device); names are unique
        [[nodiscard]] static eastl::span<const PreviewDevice> GetDevices();

        /**
         * @brief The target for a shape in an available area (in ImGui units).
         *
         * Free and an aspect preset: FitSize at zoom 1; `rotated` turns the ratio of a preset (16:9 gives 9:16, 4:3 gives 3:4), Free ignores it.
         * A device or a custom size: exactly that size (each side clamped to kMinPreviewSide..kMaxPreviewSide, width and height swapped when `rotated`),
         * so canvases lay out for the real size, shown at `zoom = min(1, availableWidth / width, availableHeight / height)`: never above 100%.
         * An unknown device index gives Free. An available area under 1 pixel gives {0, 0, 0}.
         */
        [[nodiscard]] static PreviewTarget TargetFor(const PreviewShape& shape, float availableWidth, float availableHeight);

        /// The safe area of an output of this size, in output pixels, origin at the top left of the output (the whole output for None)
        [[nodiscard]] static Rect SimulatedSafeArea(SafeAreaSimulation simulation, float width, float height);

        /**
         * @brief The same with a device for SafeAreaSimulation::Device; the other simulations give what the 3-argument overload gives.
         *
         * Device in portrait (width < height): `{0, cutout, w, h - cutout - homeBar}`; in landscape: `{cutout, 0, w - 2 cutout, h - homeBar}`.
         * Device with no device (null) is the whole output. The insets are clamped so the rect is never empty.
         */
        [[nodiscard]] static Rect SimulatedSafeArea(SafeAreaSimulation simulation, const PreviewDevice* device, float width, float height);

    private:
        // FitSize for a ratio (width / height) instead of a preset; 0 is Free
        [[nodiscard]] static std::pair<uint32_t, uint32_t> FitRatio(float ratio, float availableWidth, float availableHeight);
    };

}  // namespace BECore
