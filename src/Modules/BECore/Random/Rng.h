#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

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

        /// An integer of `[lo, hi]`, both inclusive (`lo <= hi` is the caller's contract), without modulo bias: `x % range` of a draw
        /// `x` that is not below `2^64 mod range`, the others rejected. The number of draws depends only on the seed and the
        /// range, so the stream stays reproducible; a range of one value draws once, so the stream does not depend on the width
        /// of a range. The full 64-bit range returns one draw as it is.
        template <typename T>
        [[nodiscard]] T UniformInt(T lo, T hi) {
            static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool> && sizeof(T) <= sizeof(uint64_t), "an integer type of at most 64 bits");
            ASSERT(lo <= hi, "UniformInt: lo must not exceed hi");
            const uint64_t range = static_cast<uint64_t>(hi) - static_cast<uint64_t>(lo) + 1;
            if (range == 0) {
                return static_cast<T>(Next64());
            }
            const uint64_t limit = (uint64_t{0} - range) % range;
            uint64_t x = Next64();
            while (x < limit) {
                x = Next64();
            }
            return static_cast<T>(static_cast<uint64_t>(lo) + x % range);
        }

        /// True with probability `p`: one draw, `Uniform01() < p`. `p <= 0` is false and `p >= 1` is true with no draw; NaN is false
        /// with no draw.
        [[nodiscard]] constexpr bool Chance(double p) {
            if (!(p > 0.0)) {
                return false;
            }
            if (p >= 1.0) {
                return true;
            }
            return Uniform01() < p;
        }

        /// The index picked with the probability `weights[i] / sum`, empty (and nothing drawn) when there are no weights or all are
        /// zero. A zero weight is never picked. The weights are integers so that the result does not depend on how a compiler
        /// rounds a float sum: the caller scales them.
        [[nodiscard]] Optional<size_t> WeightedPick(Span<const uint32_t> weights) {
            uint64_t total = 0;
            for (const uint32_t weight : weights) {
                total += weight;
            }
            if (total == 0) {
                return {};
            }
            const uint64_t r = UniformInt<uint64_t>(0, total - 1);
            uint64_t running = 0;
            for (size_t i = 0; i < weights.size(); ++i) {
                running += weights[i];
                if (running > r) {
                    return i;
                }
            }
            return {};  // unreachable: r < total
        }

        /// Fisher-Yates from the end: for `i = n - 1` down to 1, swaps `[i]` with `[j]`, `j = UniformInt<size_t>(0, i)`. A span of
        /// 0 or 1 elements draws nothing.
        template <typename T>
        void Shuffle(Span<T> items) {
            for (size_t i = items.size(); i > 1; --i) {
                const size_t j = UniformInt<size_t>(0, i - 1);
                if (j != i - 1) {
                    using std::swap;
                    swap(items[i - 1], items[j]);
                }
            }
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
