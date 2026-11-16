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

        void SanitizeRange(float& low, float& high, float minimum, float maximum, float fallbackLow, float fallbackHigh) {
            low = ClampFinite(low, minimum, maximum, fallbackLow);
            high = ClampFinite(high, minimum, maximum, fallbackHigh);
            if (low > high) {
                std::swap(low, high);
            }
        }

        void SanitizeFlipbook(ParticleFlipbook& flipbook, const ParticleFlipbook& fallback) {
            using namespace ParticleLimits;
            flipbook.columns = std::clamp(flipbook.columns, 1, kMaxFlipbookAxis);
            flipbook.rows = std::clamp(flipbook.rows, 1, kMaxFlipbookAxis);
            flipbook.frames = std::clamp(flipbook.frames, 0, flipbook.columns * flipbook.rows);
            flipbook.mode = ValidEnum(flipbook.mode, fallback.mode);
            flipbook.fps = ClampFinite(flipbook.fps, 0.0f, kMaxFlipbookFps, fallback.fps);
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

        SanitizeFlipbook(flipbook, defaults.flipbook);

        blend = ValidEnum(blend, defaults.blend);
        frameClass = ValidEnum(frameClass, defaults.frameClass);
    }

    ParticleEffect ParticleEffect::Sanitized() const {
        ParticleEffect copy = *this;
        copy.Sanitize();
        return copy;
    }

}  // namespace BECore
