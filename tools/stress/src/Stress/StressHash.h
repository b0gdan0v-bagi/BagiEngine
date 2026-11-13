#pragma once

namespace BECore::Stress {

    /// Streams of the hash are told apart by a salt, so the properties of one object do not follow each other.
    /// An own integer mix, never `std::rand` or the `std::` distributions: the numbers must not differ between standard libraries.

    /// splitmix64: a fixed integer mix, the same on every machine
    inline uint64_t Mix(uint64_t value) {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    inline uint64_t Hash(uint32_t index, uint64_t salt) {
        constexpr uint64_t kSaltShift = 8;
        return Mix((static_cast<uint64_t>(index) << kSaltShift) ^ salt);
    }

    /// A value of the hash of (@p index, @p salt) in [0, 1)
    inline float Unit(uint32_t index, uint64_t salt) {
        constexpr uint64_t kBits = 24;
        constexpr uint64_t kWord = 64;
        return static_cast<float>(Hash(index, salt) >> (kWord - kBits)) / static_cast<float>(1ULL << kBits);
    }

    inline float Between(float low, float high, float unit) {
        return low + (high - low) * unit;
    }

}  // namespace BECore::Stress
