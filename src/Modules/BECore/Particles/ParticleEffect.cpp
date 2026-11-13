#include "ParticleEffect.h"

#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <Generated/ParticleEffect.gen.hpp>
#include <cmath>

namespace BECore {

    namespace {

        /// @p value in @p low..@p high, or @p fallback when it is not a finite number
        float ClampFinite(float value, float low, float high, float fallback) {
            return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
        }

        /// @p value when it names one of the values of the enum, @p fallback otherwise
        template <typename TEnum>
        TEnum ValidEnum(TEnum value, TEnum fallback) {
            return EnumUtils<TEnum>::ToString(value).empty() ? fallback : value;
        }

        void SanitizeCurve(ParticleCurve& curve, const ParticleCurve& fallback, float limit) {
            curve.from = ClampFinite(curve.from, -limit, limit, fallback.from);
            curve.to = ClampFinite(curve.to, -limit, limit, fallback.to);
            curve.ease = ValidEnum(curve.ease, fallback.ease);
        }

        /// Both ends clamped, then put in order
        void SanitizeRange(float& low, float& high, float minimum, float maximum, float fallbackLow, float fallbackHigh) {
            low = ClampFinite(low, minimum, maximum, fallbackLow);
            high = ClampFinite(high, minimum, maximum, fallbackHigh);
            if (low > high) {
                std::swap(low, high);
            }
        }

    }  // namespace

    void ParticleEffect::Sanitize() {
        using namespace ParticleLimits;
        const ParticleEffect defaults;

        rate = ClampFinite(rate, 0.0f, kMaxRate, defaults.rate);
        maxParticles = std::clamp(maxParticles, kMinParticles, kMaxParticles);
        burstCount = std::clamp(burstCount, 0, maxParticles);

        SanitizeRange(lifetimeMin, lifetimeMax, kMinLifetime, kMaxLifetime, defaults.lifetimeMin, defaults.lifetimeMax);
        shape = ValidEnum(shape, defaults.shape);
        shapeRadius = ClampFinite(shapeRadius, 0.0f, kMaxRadius, defaults.shapeRadius);

        SanitizeRange(speedMin, speedMax, 0.0f, kMaxSpeed, defaults.speedMin, defaults.speedMax);
        direction = ClampFinite(direction, -kMaxAngle, kMaxAngle, defaults.direction);
        spread = ClampFinite(spread, 0.0f, kFullCircle, defaults.spread);
        gravityX = ClampFinite(gravityX, -kMaxGravity, kMaxGravity, defaults.gravityX);
        gravityY = ClampFinite(gravityY, -kMaxGravity, kMaxGravity, defaults.gravityY);

        SanitizeCurve(size, defaults.size, kMaxSize);
        size.from = std::max(size.from, 0.0f);
        size.to = std::max(size.to, 0.0f);
        sizeJitter = ClampFinite(sizeJitter, 0.0f, kMaxJitter, defaults.sizeJitter);
        SanitizeCurve(rotation, defaults.rotation, kMaxAngle);
        rotationJitter = ClampFinite(rotationJitter, 0.0f, kMaxRotationJitter, defaults.rotationJitter);
        color.ease = ValidEnum(color.ease, defaults.color.ease);

        blend = ValidEnum(blend, defaults.blend);
        frameClass = ValidEnum(frameClass, defaults.frameClass);
    }

    ParticleEffect ParticleEffect::Sanitized() const {
        ParticleEffect copy = *this;
        copy.Sanitize();
        return copy;
    }

}  // namespace BECore
