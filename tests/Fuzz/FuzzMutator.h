#pragma once

#include <span>

namespace BECore::Fuzz {

    /// The largest input the fuzzers feed to a target (the libFuzzer runs use -max_len with the same number)
    inline constexpr size_t kMaxFuzzInputSize = 64 * 1024;

    using FuzzBytes = Vector<uint8_t>;

    /**
     * @brief xorshift64*: the generator of the mutator, with an explicit seed
     *
     * The standard distributions and std::random_device are not portable, so the corpus test, which has to make the
     * same bytes on every platform and every run, draws from this one only.
     */
    class FuzzRandom {
    public:
        explicit FuzzRandom(uint64_t seed);

        uint64_t Next();

        /// A number in [0, bound); 0 when @p bound is 0
        uint32_t Below(uint32_t bound);

    private:
        uint64_t _state;
    };

    /// The kinds of change Mutate() makes
    CORE_ENUM(FuzzMutation, uint8_t, FlipBit, SetByte, SetWord, InsertBlock, DeleteBlock, DuplicateBlock, Truncate, Splice)

    /**
     * @brief Apply one mutation of the given kind
     *
     * A kind that needs bytes the input does not have (anything but an insert on an empty input, a word on 3 bytes,
     * a splice with no other seed) does the nearest kind that fits instead; the kind that was done is returned.
     * The result is never longer than kMaxFuzzInputSize.
     */
    FuzzMutation ApplyMutation(FuzzMutation kind, FuzzBytes& bytes, FuzzRandom& random, std::span<const FuzzBytes> otherSeeds);

    /// One mutation of a kind drawn from @p random
    FuzzMutation Mutate(FuzzBytes& bytes, FuzzRandom& random, std::span<const FuzzBytes> otherSeeds);

}  // namespace BECore::Fuzz
