#pragma once

#include <Rooms/RoomTypes.h>

namespace BECore::Tests {

    /// A deterministic stream of well-mixed words (splitmix64): the same numbers on every platform
    inline RandomFn MakeSequence(uint64_t seed) {
        auto state = MakeShared<uint64_t>(seed);
        return [state]() -> uint64_t {
            uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        };
    }

}  // namespace BECore::Tests
