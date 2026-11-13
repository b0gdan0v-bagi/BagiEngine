#pragma once

#include <BECore/Animation/Easing.h>
#include <BECore/Renderer/BlendMode.h>

namespace BECore {

    class IDeserializer;
    class ISerializer;

    /// The area a particle is born in: the center of the emitter, a disc around it, or the emitter's rect
    CORE_ENUM(ParticleShape, uint8_t, Point, Rect, Circle)

    /**
     * @brief How much the frame loop has to be awake for an effect (the contract of the render-on-demand entry).
     *
     * Interactive needs a frame all the time, FiniteEffect needs frames until the last particle died, DecorativeLoop is a looping
     * effect that only decorates (it may be drawn at a lower rate or not at all while the rest of the picture sleeps).
     */
    CORE_ENUM(ParticleFrameClass, uint8_t, Interactive, FiniteEffect, DecorativeLoop)

    namespace ParticleLimits {
        inline constexpr int32_t kMinParticles = 1;          ///< the smallest pool: an effect always has room for one
        inline constexpr int32_t kMaxParticles = 8192;       ///< the largest pool; a file or the inspector cannot ask for more
        inline constexpr float kMaxRate = 10000.0f;          ///< emitted per second, looping
        inline constexpr float kMinLifetime = 0.01f;         ///< seconds; a particle lives at least this long, so `age / lifetime` is finite
        inline constexpr float kMaxLifetime = 120.0f;        ///< seconds
        inline constexpr float kMaxSpeed = 100000.0f;        ///< scene units per second
        inline constexpr float kMaxGravity = 100000.0f;      ///< scene units per second squared, per axis (absolute value)
        inline constexpr float kMaxRadius = 100000.0f;       ///< scene units, the radius of `Circle`
        inline constexpr float kMaxSize = 100000.0f;         ///< scene units, a side of a particle
        inline constexpr float kMaxAngle = 360000.0f;        ///< degrees, an absolute value of the direction and of a rotation curve end
        inline constexpr float kFullCircle = 360.0f;         ///< degrees; the widest `spread`
        inline constexpr float kMaxJitter = 1.0f;            ///< `sizeJitter` is a fraction of the size
        inline constexpr float kMaxRotationJitter = 360.0f;  ///< degrees, either way
    }  // namespace ParticleLimits

    /// A float over the life of a particle: `from` at its birth, `to` at its end, the progress shaped by `ease`
    struct ParticleCurve {
        BE_CLASS(ParticleCurve)

        ParticleCurve() = default;
        ParticleCurve(float from_, float to_, EaseType ease_ = EaseType::Linear) : from(from_), to(to_), ease(ease_) {}

        BE_REFLECT_FIELD float from = 0.0f;
        BE_REFLECT_FIELD float to = 0.0f;
        BE_REFLECT_FIELD EaseType ease = EaseType::Linear;
    };

    /// A color over the life of a particle (straight alpha), like `ParticleCurve`
    struct ParticleColorCurve {
        BE_CLASS(ParticleColorCurve)

        BE_REFLECT_FIELD Color from{255, 255, 255, 255};
        BE_REFLECT_FIELD Color to{255, 255, 255, 0};
        BE_REFLECT_FIELD EaseType ease = EaseType::Linear;
    };

    /**
     * @brief A 2D particle effect as data: how many particles, where they are born, how they move and look.
     *
     * The defaults are a usable small burst: 24 white squares fly out in all directions from a point and fade out in half a second.
     * The data has no behavior of its own; `ParticleSimulation` runs it and `ParticleEmitterComponent` draws it. The fields are the
     * attributes and child elements of the file (`Particles/CLAUDE.md`). `Sanitize()` makes any values safe to run.
     */
    struct ParticleEffect {
        BE_CLASS(ParticleEffect)

        // Emission
        BE_REFLECT_FIELD bool looping = false;        ///< emit `rate` per second for as long as it plays; false fires `burstCount` once at Play
        BE_REFLECT_FIELD float rate = 30.0f;          ///< per second, when looping
        BE_REFLECT_FIELD int32_t burstCount = 24;     ///< at Play when not looping; the default count of a `Burst` call
        BE_REFLECT_FIELD int32_t maxParticles = 256;  ///< the size of the pool: a full pool drops new particles
        BE_REFLECT_FIELD uint32_t seed = 1;           ///< the same seed and the same game time give the same picture

        // Life and birth place
        BE_REFLECT_FIELD float lifetimeMin = 0.4f;  ///< seconds
        BE_REFLECT_FIELD float lifetimeMax = 0.8f;  ///< seconds
        BE_REFLECT_FIELD ParticleShape shape = ParticleShape::Point;
        BE_REFLECT_FIELD float shapeRadius = 0.0f;  ///< scene units, for `Circle`

        // Motion (a position is a function of age: origin + velocity * age + gravity * age^2 / 2)
        BE_REFLECT_FIELD float speedMin = 40.0f;   ///< scene units per second
        BE_REFLECT_FIELD float speedMax = 120.0f;  ///< scene units per second
        BE_REFLECT_FIELD float direction = 0.0f;   ///< degrees, 0 is right and positive is clockwise (like `TransformComponent::_rotation`)
        BE_REFLECT_FIELD float spread = 360.0f;    ///< degrees, the full width of the cone around `direction`; 360 is all around
        BE_REFLECT_FIELD float gravityX = 0.0f;    ///< scene units per second squared
        BE_REFLECT_FIELD float gravityY = 0.0f;    ///< scene units per second squared, positive is down

        // Look over the life
        BE_REFLECT_FIELD ParticleCurve size{8.0f, 0.0f, EaseType::Linear};      ///< a side of the square, scene units
        BE_REFLECT_FIELD float sizeJitter = 0.0f;                               ///< 0..1: the size of a particle is scaled by 1 +- this, drawn at birth
        BE_REFLECT_FIELD ParticleCurve rotation{0.0f, 0.0f, EaseType::Linear};  ///< degrees, clockwise
        BE_REFLECT_FIELD float rotationJitter = 0.0f;                           ///< degrees: the start angle of a particle is shifted by +- this, drawn at birth
        BE_REFLECT_FIELD ParticleColorCurve color;

        // Drawing
        BE_REFLECT_FIELD PoolString spriteId;     ///< a sprite of the TextureLibrary; empty and no `texturePath` draws plain squares
        BE_REFLECT_FIELD PoolString texturePath;  ///< a texture file, used when `spriteId` is empty (the whole texture)
        BE_REFLECT_FIELD BlendMode blend = BlendMode::Alpha;
        BE_REFLECT_FIELD ParticleFrameClass frameClass = ParticleFrameClass::FiniteEffect;

        /**
         * @brief Bring every field into its range, in place.
         *
         * Not finite floats become the field's default, the others are clamped to `ParticleLimits`; a min above its max swaps with it;
         * counts and rates are not negative, `maxParticles` is in `kMinParticles..kMaxParticles` and `burstCount` does not exceed it;
         * an enum value that is none of its names (a cast of a bad number) becomes the default. The simulation runs on a sanitized copy,
         * so a typo in a file or in the inspector never loops or allocates without bound; the effect itself keeps what was written.
         */
        void Sanitize();

        /// A sanitized copy of this effect
        [[nodiscard]] ParticleEffect Sanitized() const;
    };

}  // namespace BECore
