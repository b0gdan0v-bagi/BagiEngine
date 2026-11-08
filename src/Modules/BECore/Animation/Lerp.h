#pragma once

#include <BECore/Math/Vec2.h>
#include <cmath>
#include <concepts>

namespace BECore {

    /**
     * @brief Linear interpolation of a float: `from` at 0, `to` at 1, both exact (`std::lerp`).
     *
     * @p t is not clamped: a value outside 0..1 extrapolates, which an overshooting ease (Back, Elastic) relies on to reach its bulge.
     */
    [[nodiscard]] inline float Lerp(float from, float to, float t) {
        return std::lerp(from, to, t);
    }

    /// Per component, like the float overload (not clamped)
    [[nodiscard]] inline Vec2 Lerp(const Vec2& from, const Vec2& to, float t) {
        return {std::lerp(from.x, to.x, t), std::lerp(from.y, to.y, t)};
    }

    inline constexpr float kColorChannelMax = 255.0f;  ///< the top of a color byte: the clamp of the Color overload of `Lerp`

    /**
     * @brief Per channel, rounded to the nearest integer (halves away from zero: 0 to 255 at 0.5 is 128) and clamped to 0..255.
     *
     * The clamp keeps an overshooting @p t (Back) from wrapping a byte around; the endpoints are exact.
     */
    [[nodiscard]] inline Color Lerp(const Color& from, const Color& to, float t) {
        const auto channel = [t](unsigned char a, unsigned char b) {
            const float value = std::round(std::lerp(static_cast<float>(a), static_cast<float>(b), t));
            return static_cast<unsigned char>(std::clamp(value, 0.0f, kColorChannelMax));
        };
        return {channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b), channel(from.a, to.a)};
    }

    /**
     * @brief A type a tween can run over: `Lerp(from, to, t)` exists for it and gives the same type.
     *
     * The overloads above are the engine's; a type of a game adds its own `Lerp` (found by argument-dependent lookup).
     */
    template <typename T>
    concept Tweenable = requires(const T& from, const T& to, float t) {
        {Lerp(from, to, t)}->std::same_as<T>;
    };

}  // namespace BECore
