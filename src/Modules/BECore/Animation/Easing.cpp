#include "Easing.h"

#include <cmath>
#include <numbers>

namespace BECore {

    namespace {

        using namespace EasingConstants;

        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kHalf = 0.5f;

        // Every InOut curve is two Out/In halves of double speed, mirrored around (0.5, 0.5)

        float QuadIn(float t) {
            return t * t;
        }

        float QuadOut(float t) {
            const float inverse = 1.0f - t;
            return 1.0f - inverse * inverse;
        }

        float QuadInOut(float t) {
            if (t < kHalf) {
                return 2.0f * t * t;
            }
            const float mirrored = -2.0f * t + 2.0f;
            return 1.0f - mirrored * mirrored * kHalf;
        }

        float CubicIn(float t) {
            return t * t * t;
        }

        float CubicOut(float t) {
            const float inverse = 1.0f - t;
            return 1.0f - inverse * inverse * inverse;
        }

        float CubicInOut(float t) {
            if (t < kHalf) {
                return 4.0f * t * t * t;
            }
            const float mirrored = -2.0f * t + 2.0f;
            return 1.0f - mirrored * mirrored * mirrored * kHalf;
        }

        float SineIn(float t) {
            return 1.0f - std::cos(t * kPi * kHalf);
        }

        float SineOut(float t) {
            return std::sin(t * kPi * kHalf);
        }

        float SineInOut(float t) {
            return -(std::cos(kPi * t) - 1.0f) * kHalf;
        }

        float BackIn(float t) {
            const float c3 = kBackOvershoot + 1.0f;
            return c3 * t * t * t - kBackOvershoot * t * t;
        }

        float BackOut(float t) {
            const float c3 = kBackOvershoot + 1.0f;
            const float shifted = t - 1.0f;
            return 1.0f + c3 * shifted * shifted * shifted + kBackOvershoot * shifted * shifted;
        }

        float BackInOut(float t) {
            const float c2 = kBackOvershoot * kBackInOutScale;
            if (t < kHalf) {
                const float doubled = 2.0f * t;
                return doubled * doubled * ((c2 + 1.0f) * doubled - c2) * kHalf;
            }
            const float doubled = 2.0f * t - 2.0f;
            return (doubled * doubled * ((c2 + 1.0f) * doubled + c2) + 2.0f) * kHalf;
        }

        float BounceOut(float t) {
            // The four parabolas of easings.net: the ball falls, then bounces three times, each bounce lower
            constexpr float kSecondStart = 1.0f;
            constexpr float kSecondOffset = 1.5f;
            constexpr float kSecondFloor = 0.75f;
            constexpr float kThirdStart = 2.0f;
            constexpr float kThirdEnd = 2.5f;
            constexpr float kThirdOffset = 2.25f;
            constexpr float kThirdFloor = 0.9375f;
            constexpr float kFourthOffset = 2.625f;
            constexpr float kFourthFloor = 0.984375f;

            if (t < kSecondStart / kBounceDivisor) {
                return kBounceStrength * t * t;
            }
            if (t < kThirdStart / kBounceDivisor) {
                t -= kSecondOffset / kBounceDivisor;
                return kBounceStrength * t * t + kSecondFloor;
            }
            if (t < kThirdEnd / kBounceDivisor) {
                t -= kThirdOffset / kBounceDivisor;
                return kBounceStrength * t * t + kThirdFloor;
            }
            t -= kFourthOffset / kBounceDivisor;
            return kBounceStrength * t * t + kFourthFloor;
        }

        float ElasticOut(float t) {
            return std::exp2(-kElasticFrequency * t) * std::sin((kElasticFrequency * t - kElasticPhase) * kElasticPeriod) + 1.0f;
        }

    }  // namespace

    float Ease(EaseType type, float t) {
        // The endpoints are answered before any formula, so float error cannot leave 0.9999 or -1e-8; `!(t > 0)` also catches NaN
        if (!(t > 0.0f)) {
            return 0.0f;
        }
        if (t >= 1.0f) {
            return 1.0f;
        }

        switch (type) {
            case EaseType::Linear:
                return t;
            case EaseType::QuadIn:
                return QuadIn(t);
            case EaseType::QuadOut:
                return QuadOut(t);
            case EaseType::QuadInOut:
                return QuadInOut(t);
            case EaseType::CubicIn:
                return CubicIn(t);
            case EaseType::CubicOut:
                return CubicOut(t);
            case EaseType::CubicInOut:
                return CubicInOut(t);
            case EaseType::SineIn:
                return SineIn(t);
            case EaseType::SineOut:
                return SineOut(t);
            case EaseType::SineInOut:
                return SineInOut(t);
            case EaseType::BackIn:
                return BackIn(t);
            case EaseType::BackOut:
                return BackOut(t);
            case EaseType::BackInOut:
                return BackInOut(t);
            case EaseType::BounceOut:
                return BounceOut(t);
            case EaseType::ElasticOut:
                return ElasticOut(t);
        }
        return t;
    }

}  // namespace BECore
