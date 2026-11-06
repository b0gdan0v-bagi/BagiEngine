#pragma once

#include <numbers>

namespace BECore {

    /**
     * @brief The easing curves of `Ease`: the well-known set of https://easings.net (the same names and formulas).
     *
     * In slows down the start, Out slows down the end, InOut both. Back leaves 0..1 on purpose (it pulls back before the start, or
     * overshoots the end, and comes home), Elastic overshoots and rings, Bounce hits the end and bounces back from it.
     */
    CORE_ENUM(EaseType, uint8_t, Linear, QuadIn, QuadOut, QuadInOut, CubicIn, CubicOut, CubicInOut, SineIn, SineOut, SineInOut, BackIn, BackOut, BackInOut, BounceOut, ElasticOut)

    namespace EasingConstants {
        inline constexpr float kBackOvershoot = 1.70158f;                                 ///< `c1` of easings.net: about 10 % overshoot
        inline constexpr float kBackInOutScale = 1.525f;                                  ///< `c2 = c1 * 1.525` of easings.net
        inline constexpr float kBounceStrength = 7.5625f;                                 ///< `n1` of easings.net: the parabola of one bounce
        inline constexpr float kBounceDivisor = 2.75f;                                    ///< `d1` of easings.net: the length of the first bounce
        inline constexpr float kElasticPeriod = 2.0f * std::numbers::pi_v<float> / 3.0f;  ///< `c4 = 2 * pi / 3` of easings.net
        inline constexpr float kElasticFrequency = 10.0f;                                 ///< `10` of easings.net: the speed of the oscillation, and so how fast the ring dies out
        inline constexpr float kElasticPhase = 0.75f;                                     ///< the phase that makes the curve start at 0
    }  // namespace EasingConstants

    /**
     * @brief The eased progress of a normalized time.
     *
     * @p t is clamped to 0..1 first (NaN gives 0). The endpoints are exact for every type: `Ease(type, 0) == 0` and `Ease(type, 1) == 1`,
     * so a value interpolated with the result ends on the target and not 0.9999 away from it. Between them Back and Elastic leave 0..1,
     * every other type stays inside it. Pure maths, no services, the same on every platform (to float precision).
     */
    [[nodiscard]] float Ease(EaseType type, float t);

}  // namespace BECore
