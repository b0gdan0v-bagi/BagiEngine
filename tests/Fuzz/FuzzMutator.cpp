#include "FuzzMutator.h"

namespace BECore::Fuzz {

    namespace {

        constexpr uint8_t kInterestingBytes[] = {0x00, 0x7F, 0x80, 0xFF};

        /// Values for the length and count fields of the formats: all ones, the largest signed, the smallest signed, zero
        constexpr uint32_t kInterestingWords[] = {0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0u};

        constexpr uint64_t kZeroSeedReplacement = 0x9E3779B97F4A7C15ull;
        constexpr uint64_t kMultiplier = 0x2545F4914F6CDD1Dull;
        constexpr uint32_t kMaxBlockSize = 32;
        constexpr uint32_t kBitsPerByte = 8;
        constexpr size_t kWordSize = sizeof(uint32_t);

        /// A block length in [1, min(available, kMaxBlockSize)]; @p available is at least 1
        size_t PickBlockSize(FuzzRandom& random, size_t available) {
            const size_t limit = std::min<size_t>(available, kMaxBlockSize);
            return 1 + random.Below(static_cast<uint32_t>(limit));
        }

        void InsertBytes(FuzzBytes& bytes, size_t position, const uint8_t* block, size_t count) {
            bytes.insert(bytes.begin() + static_cast<ptrdiff_t>(position), block, block + count);
        }

    }  // namespace

    FuzzRandom::FuzzRandom(uint64_t seed) : _state(seed != 0 ? seed : kZeroSeedReplacement) {}

    uint64_t FuzzRandom::Next() {
        _state ^= _state >> 12;
        _state ^= _state << 25;
        _state ^= _state >> 27;
        return _state * kMultiplier;
    }

    uint32_t FuzzRandom::Below(uint32_t bound) {
        if (bound == 0) {
            return 0;
        }
        return static_cast<uint32_t>((Next() >> 32) % bound);
    }

    FuzzMutation ApplyMutation(FuzzMutation kind, FuzzBytes& bytes, FuzzRandom& random, std::span<const FuzzBytes> otherSeeds) {
        const size_t size = bytes.size();

        // Everything but an insert needs bytes to work on
        if (size == 0 && kind != FuzzMutation::InsertBlock) {
            return ApplyMutation(FuzzMutation::InsertBlock, bytes, random, otherSeeds);
        }

        switch (kind) {
            case FuzzMutation::FlipBit: {
                const size_t position = random.Below(static_cast<uint32_t>(size));
                bytes[position] ^= static_cast<uint8_t>(1u << random.Below(kBitsPerByte));
                break;
            }
            case FuzzMutation::SetByte: {
                const size_t position = random.Below(static_cast<uint32_t>(size));
                bytes[position] = kInterestingBytes[random.Below(std::size(kInterestingBytes))];
                break;
            }
            case FuzzMutation::SetWord: {
                if (size < kWordSize) {
                    return ApplyMutation(FuzzMutation::SetByte, bytes, random, otherSeeds);
                }
                const size_t position = random.Below(static_cast<uint32_t>(size - kWordSize + 1));
                const uint32_t value = kInterestingWords[random.Below(std::size(kInterestingWords))];
                std::memcpy(bytes.data() + position, &value, kWordSize);
                break;
            }
            case FuzzMutation::InsertBlock: {
                const size_t position = random.Below(static_cast<uint32_t>(size + 1));
                const size_t count = PickBlockSize(random, kMaxBlockSize);
                uint8_t block[kMaxBlockSize];
                for (size_t i = 0; i < count; ++i) {
                    block[i] = static_cast<uint8_t>(random.Next() >> 56);
                }
                InsertBytes(bytes, position, block, count);
                break;
            }
            case FuzzMutation::DeleteBlock: {
                const size_t position = random.Below(static_cast<uint32_t>(size));
                const size_t count = PickBlockSize(random, size - position);
                bytes.erase(bytes.begin() + static_cast<ptrdiff_t>(position), bytes.begin() + static_cast<ptrdiff_t>(position + count));
                break;
            }
            case FuzzMutation::DuplicateBlock: {
                const size_t position = random.Below(static_cast<uint32_t>(size));
                const size_t count = PickBlockSize(random, size - position);
                const FuzzBytes block(bytes.begin() + static_cast<ptrdiff_t>(position), bytes.begin() + static_cast<ptrdiff_t>(position + count));
                InsertBytes(bytes, position + count, block.data(), count);
                break;
            }
            case FuzzMutation::Truncate: {
                bytes.resize(random.Below(static_cast<uint32_t>(size)));
                break;
            }
            case FuzzMutation::Splice: {
                if (otherSeeds.empty()) {
                    return ApplyMutation(FuzzMutation::DuplicateBlock, bytes, random, otherSeeds);
                }
                const FuzzBytes& other = otherSeeds[random.Below(static_cast<uint32_t>(otherSeeds.size()))];
                if (other.empty()) {
                    return ApplyMutation(FuzzMutation::DuplicateBlock, bytes, random, otherSeeds);
                }
                const size_t from = random.Below(static_cast<uint32_t>(other.size()));
                const size_t count = PickBlockSize(random, other.size() - from);
                const size_t position = random.Below(static_cast<uint32_t>(size + 1));
                InsertBytes(bytes, position, other.data() + from, count);
                break;
            }
        }

        if (bytes.size() > kMaxFuzzInputSize) {
            bytes.resize(kMaxFuzzInputSize);
        }
        return kind;
    }

    FuzzMutation Mutate(FuzzBytes& bytes, FuzzRandom& random, std::span<const FuzzBytes> otherSeeds) {
        const auto kind = static_cast<FuzzMutation>(random.Below(static_cast<uint32_t>(EnumUtils<FuzzMutation>::Count())));
        return ApplyMutation(kind, bytes, random, otherSeeds);
    }

}  // namespace BECore::Fuzz
