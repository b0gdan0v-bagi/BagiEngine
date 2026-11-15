#pragma once

#include <cstdint>

namespace BECore {

    /// A deterministic counter-based generator (SplitMix64): draw number `n` is a pure function of `seed` and `n`, the same on every
    /// platform and compiler. The state is two integers with no padding, so `memcpy` of an `Rng` is its save format.
    class Rng {
    public:
        /// The seed is used as it is: two seeds give overlapping streams only when they differ by a multiple of the odd step
        /// constant, which no small integer seed does.
        constexpr explicit Rng(uint64_t seed = 0) : _seed(seed) {}

        /// The next 64 random bits; the counter moves by one.
        [[nodiscard]] constexpr uint64_t Next64() {
            ++_counter;
            return At(_counter);
        }

        /// The high 32 bits of one `Next64()`.
        [[nodiscard]] constexpr uint32_t Next32() {
            return static_cast<uint32_t>(Next64() >> kHighHalfShift);
        }

        /// Draw number `n` (1-based) without moving the counter: `Rng(seed).At(n)` is the `n`-th `Next64()` of that seed.
        [[nodiscard]] constexpr uint64_t At(uint64_t n) const {
            return Mix(_seed + n * kStep);
        }

        /// Skips `n` draws.
        constexpr void Skip(uint64_t n) {
            _counter += n;
        }

        /// A `double` in [0, 1) with 53 random bits; one draw. The scaling is exact on every IEEE-754 platform.
        [[nodiscard]] constexpr double Uniform01() {
            return static_cast<double>(Next64() >> kDoubleDroppedBits) * kDoubleUnit;
        }

        [[nodiscard]] constexpr uint64_t Seed() const {
            return _seed;
        }

        /// The number of draws made so far.
        [[nodiscard]] constexpr uint64_t Counter() const {
            return _counter;
        }

        [[nodiscard]] constexpr bool operator==(const Rng&) const = default;

    private:
        static constexpr uint64_t kStep = 0x9E3779B97F4A7C15ULL;
        static constexpr uint64_t kMixMul1 = 0xBF58476D1CE4E5B9ULL;
        static constexpr uint64_t kMixMul2 = 0x94D049BB133111EBULL;
        static constexpr int kMixShift1 = 30;
        static constexpr int kMixShift2 = 27;
        static constexpr int kMixShift3 = 31;
        static constexpr int kHighHalfShift = 32;
        static constexpr int kDoubleDroppedBits = 11;
        static constexpr double kDoubleUnit = 0x1p-53;

        [[nodiscard]] static constexpr uint64_t Mix(uint64_t z) {
            z = (z ^ (z >> kMixShift1)) * kMixMul1;
            z = (z ^ (z >> kMixShift2)) * kMixMul2;
            return z ^ (z >> kMixShift3);
        }

        uint64_t _seed = 0;
        uint64_t _counter = 0;
    };

}  // namespace BECore
