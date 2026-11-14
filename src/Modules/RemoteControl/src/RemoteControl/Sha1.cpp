#include "Sha1.h"

namespace BECore {

    namespace {

        constexpr size_t kBlockSize = 64;
        constexpr size_t kLengthFieldSize = 8;  // the message length in bits, big-endian, closes the last block
        constexpr size_t kWordsPerBlock = 16;
        constexpr size_t kScheduleSize = 80;
        constexpr uint8_t kPaddingMarker = 0x80;

        using State = std::array<uint32_t, 5>;

        constexpr State kInitialState = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

        // Round constants of the four groups of 20 rounds
        constexpr std::array<uint32_t, 4> kRoundConstants = {0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6};
        constexpr size_t kRoundsPerGroup = 20;

        constexpr uint32_t RotateLeft(uint32_t value, int bits) {
            return (value << bits) | (value >> (32 - bits));
        }

        void ProcessBlock(State& state, const uint8_t* block) {
            std::array<uint32_t, kScheduleSize> schedule{};
            for (size_t i = 0; i < kWordsPerBlock; ++i) {
                const uint8_t* word = block + i * 4;
                schedule[i] = (static_cast<uint32_t>(word[0]) << 24) | (static_cast<uint32_t>(word[1]) << 16) | (static_cast<uint32_t>(word[2]) << 8) | static_cast<uint32_t>(word[3]);
            }
            for (size_t i = kWordsPerBlock; i < kScheduleSize; ++i) {
                schedule[i] = RotateLeft(schedule[i - 3] ^ schedule[i - 8] ^ schedule[i - 14] ^ schedule[i - 16], 1);
            }

            uint32_t a = state[0];
            uint32_t b = state[1];
            uint32_t c = state[2];
            uint32_t d = state[3];
            uint32_t e = state[4];

            for (size_t i = 0; i < kScheduleSize; ++i) {
                const size_t group = i / kRoundsPerGroup;
                uint32_t mix = 0;
                if (group == 0) {
                    mix = (b & c) | (~b & d);
                } else if (group == 2) {
                    mix = (b & c) | (b & d) | (c & d);
                } else {
                    mix = b ^ c ^ d;
                }

                const uint32_t next = RotateLeft(a, 5) + mix + e + kRoundConstants[group] + schedule[i];
                e = d;
                d = c;
                c = RotateLeft(b, 30);
                b = a;
                a = next;
            }

            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
        }

    }  // namespace

    Sha1Digest Sha1(Span<const uint8_t> data) {
        State state = kInitialState;

        const size_t fullBlocks = data.size() / kBlockSize;
        for (size_t i = 0; i < fullBlocks; ++i) {
            ProcessBlock(state, data.data() + i * kBlockSize);
        }

        // The rest, the 0x80 marker and the length take one block, or two when the length does not fit after the marker
        std::array<uint8_t, kBlockSize * 2> tail{};
        const size_t remainder = data.size() - fullBlocks * kBlockSize;
        if (remainder > 0) {
            std::memcpy(tail.data(), data.data() + fullBlocks * kBlockSize, remainder);
        }
        tail[remainder] = kPaddingMarker;
        const size_t tailSize = (remainder + 1 + kLengthFieldSize <= kBlockSize) ? kBlockSize : kBlockSize * 2;

        const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8;
        for (size_t i = 0; i < kLengthFieldSize; ++i) {
            tail[tailSize - 1 - i] = static_cast<uint8_t>(bitLength >> (8 * i));
        }
        for (size_t offset = 0; offset < tailSize; offset += kBlockSize) {
            ProcessBlock(state, tail.data() + offset);
        }

        Sha1Digest digest{};
        for (size_t i = 0; i < state.size(); ++i) {
            for (size_t byte = 0; byte < 4; ++byte) {
                digest[i * 4 + byte] = static_cast<uint8_t>(state[i] >> (24 - 8 * byte));
            }
        }
        return digest;
    }

}  // namespace BECore
