#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    /// The bounds of a simulated window: client data of the control API is checked against them, so they are limits of the API, not of SDL
    struct WindowLimits {
        /// Window units per side
        static constexpr int kMinSize = 64;
        static constexpr int kMaxSize = 4096;
        static constexpr float kMinDensity = 0.5f;
        static constexpr float kMaxDensity = 4.0f;
        /// Output pixels per side (the size times the density); a software renderer and a capture need memory in proportion
        static constexpr int kMaxOutputSide = 8192;
    };

    /// A window size in window units; the optional start size of a headless run (`--window=WxH`)
    struct WindowSize {
        int width = 0;
        int height = 0;
    };

    /// How far the notch, the rounded corners and the system bars cover each edge, in window units
    struct WindowInsets {
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        float left = 0.0f;

        [[nodiscard]] bool operator==(const WindowInsets&) const = default;
    };

    /// What a headless window pretends to be: a size in window units, the output pixels per unit and the insets of the safe area
    struct WindowSimulation {
        int width = 0;
        int height = 0;
        float density = 1.0f;
        WindowInsets safeArea;

        [[nodiscard]] bool operator==(const WindowSimulation&) const = default;
    };

    /// Names the first field that is out of range and says the range; never asserts, the values come from a client
    [[nodiscard]] std::expected<void, String> ValidateWindowSimulation(const WindowSimulation& simulation);

    /// @p units window units at @p density as output pixels, rounded to the nearest
    [[nodiscard]] int SimulatedOutputPixels(int units, float density);

    /// The safe area in window units. Expects a simulation that passed ValidateWindowSimulation
    [[nodiscard]] Rect SimulatedSafeRect(const WindowSimulation& simulation);

}  // namespace BECore
