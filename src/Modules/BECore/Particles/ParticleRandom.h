#pragma once

namespace BECore {

    /// The independent streams of numbers one particle draws at birth: the same particle asks each stream for its own number
    CORE_ENUM(ParticleStream, uint8_t, Lifetime, Angle, Speed, SizeJitter, RotationJitter, ShapeA, ShapeB)

    /**
     * @brief A random number that is a pure function of (seed, serial, stream): no state, no order, the same on every platform.
     *
     * `std::uniform_*` and `rand` differ between standard libraries and depend on how many numbers were drawn before, so a particle
     * would look different after a pause, a lower frame rate or on another platform. Here particle number n always gets the same values
     * for a seed, whatever else happened. The mixer is the finalizer of splitmix64 (Vigna).
     */
    namespace ParticleRandom {

        inline constexpr uint64_t kGamma = 0x9E3779B97F4A7C15ull;  ///< splitmix64: the odd constant that steps the state
        inline constexpr uint64_t kMixA = 0xBF58476D1CE4E5B9ull;
        inline constexpr uint64_t kMixB = 0x94D049BB133111EBull;
        inline constexpr int kMixShiftA = 30;
        inline constexpr int kMixShiftB = 27;
        inline constexpr int kMixShiftC = 31;
        inline constexpr int kUnitBits = 24;                           ///< a float holds 24 bits of fraction exactly
        inline constexpr int kUnitShift = 64 - kUnitBits;              ///< the top bits of the hash are the best mixed
        inline constexpr float kUnitScale = 1.0f / (1u << kUnitBits);  ///< 2^-24: the top bits times this are in [0, 1)

        /// One step of splitmix64 from @p x (for x = 0 the first output of the generator seeded with 0: 0xE220A8397B1DCDAF)
        [[nodiscard]] constexpr uint64_t SplitMix64(uint64_t x) {
            uint64_t z = x + kGamma;
            z = (z ^ (z >> kMixShiftA)) * kMixA;
            z = (z ^ (z >> kMixShiftB)) * kMixB;
            return z ^ (z >> kMixShiftC);
        }

        /// The 64 bits of particle @p serial of @p seed for @p stream
        [[nodiscard]] constexpr uint64_t Hash(uint32_t seed, uint64_t serial, ParticleStream stream) {
            const uint64_t streamKey = (static_cast<uint64_t>(stream) + 1u) * kGamma;
            return SplitMix64(SplitMix64(SplitMix64(seed) ^ serial) ^ streamKey);
        }

        /// A float in [0, 1) from the same inputs
        [[nodiscard]] constexpr float Unit(uint32_t seed, uint64_t serial, ParticleStream stream) {
            return static_cast<float>(Hash(seed, serial, stream) >> kUnitShift) * kUnitScale;
        }

    }  // namespace ParticleRandom

}  // namespace BECore
