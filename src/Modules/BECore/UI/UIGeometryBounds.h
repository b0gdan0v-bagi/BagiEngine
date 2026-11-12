#pragma once

namespace BECore::UIBounds {

    /// The largest magnitude of a position, a size, an offset or a margin the pure UI geometry works with, in UI units. A screen is some thousands of units wide, so this is
    /// thousands of times more; a number past it is a mistake or an attack of a content file, and is cut here so that a sum or a product of a few of them stays far from the float
    /// range (an `inf` or a `NaN` born from `inf - inf` would reach a vertex buffer).
    inline constexpr float kMaxExtent = 1.0e7f;

    /// The range of the output pixels per UI unit of a canvas and of a drawn box: a scale outside is a window or a setting that is not real, and a division by it would leave
    /// the range of a float
    inline constexpr float kMinScale = 1.0e-3f;
    inline constexpr float kMaxScale = 1.0e3f;

    /// Whether @p value is a number the geometry takes as it is: finite and within kMaxExtent (false for NaN)
    [[nodiscard]] constexpr bool IsBounded(float value) {
        return value >= -kMaxExtent && value <= kMaxExtent;
    }

    /// @p value cut to [-kMaxExtent, kMaxExtent]; NaN is 0. A number of a real screen comes back unchanged
    [[nodiscard]] constexpr float Bound(float value) {
        if (!(value == value)) {
            return 0.0f;
        }
        return value < -kMaxExtent ? -kMaxExtent : (value > kMaxExtent ? kMaxExtent : value);
    }

    /// @p rect with every number bounded
    [[nodiscard]] constexpr Rect Bound(const Rect& rect) {
        return {Bound(rect.x), Bound(rect.y), Bound(rect.w), Bound(rect.h)};
    }

}  // namespace BECore::UIBounds
