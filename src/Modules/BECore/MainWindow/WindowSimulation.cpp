#include "WindowSimulation.h"

#include <cmath>

namespace BECore {

    namespace {

        std::expected<void, String> CheckSize(const char* name, int value) {
            if (value < WindowLimits::kMinSize || value > WindowLimits::kMaxSize) {
                return std::unexpected(Format("{} must be an integer from {} to {} window units, got {}", name, WindowLimits::kMinSize, WindowLimits::kMaxSize, value));
            }
            return {};
        }

        std::expected<void, String> CheckInset(const char* name, float value) {
            if (!std::isfinite(value) || value < 0.0f) {
                return std::unexpected(Format("safeArea.{} must be a finite number of window units, not negative, got {}", name, value));
            }
            return {};
        }

        std::expected<void, String> CheckOutputSide(const char* name, int units, float density) {
            const int pixels = SimulatedOutputPixels(units, density);
            if (pixels > WindowLimits::kMaxOutputSide) {
                return std::unexpected(Format("the output {} is {} pixels (size times density), at most {} are allowed", name, pixels, WindowLimits::kMaxOutputSide));
            }
            return {};
        }

    }  // namespace

    std::expected<void, String> ValidateWindowSimulation(const WindowSimulation& simulation) {
        if (auto result = CheckSize("width", simulation.width); !result) {
            return result;
        }
        if (auto result = CheckSize("height", simulation.height); !result) {
            return result;
        }
        // Written so that a NaN fails the range test too
        if (!(simulation.density >= WindowLimits::kMinDensity && simulation.density <= WindowLimits::kMaxDensity)) {
            return std::unexpected(Format("density must be a number from {} to {}, got {}", WindowLimits::kMinDensity, WindowLimits::kMaxDensity, simulation.density));
        }
        if (auto result = CheckOutputSide("width", simulation.width, simulation.density); !result) {
            return result;
        }
        if (auto result = CheckOutputSide("height", simulation.height, simulation.density); !result) {
            return result;
        }

        const WindowInsets& insets = simulation.safeArea;
        if (auto result = CheckInset("top", insets.top); !result) {
            return result;
        }
        if (auto result = CheckInset("right", insets.right); !result) {
            return result;
        }
        if (auto result = CheckInset("bottom", insets.bottom); !result) {
            return result;
        }
        if (auto result = CheckInset("left", insets.left); !result) {
            return result;
        }
        if (insets.left + insets.right >= static_cast<float>(simulation.width)) {
            return std::unexpected(Format("safeArea.left + safeArea.right must be less than the width {}, got {}", simulation.width, insets.left + insets.right));
        }
        if (insets.top + insets.bottom >= static_cast<float>(simulation.height)) {
            return std::unexpected(Format("safeArea.top + safeArea.bottom must be less than the height {}, got {}", simulation.height, insets.top + insets.bottom));
        }
        return {};
    }

    int SimulatedOutputPixels(int units, float density) {
        return static_cast<int>(std::lround(static_cast<double>(units) * static_cast<double>(density)));
    }

    Rect SimulatedSafeRect(const WindowSimulation& simulation) {
        const WindowInsets& insets = simulation.safeArea;
        return Rect(insets.left, insets.top, static_cast<float>(simulation.width) - insets.left - insets.right, static_cast<float>(simulation.height) - insets.top - insets.bottom);
    }

}  // namespace BECore
