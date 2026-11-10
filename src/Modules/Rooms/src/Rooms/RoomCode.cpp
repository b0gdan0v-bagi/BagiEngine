#include "RoomCode.h"

#include <Rooms/RoomLimits.h>

namespace BECore {

    namespace {

        constexpr size_t kBitsPerCodeChar = 5;
        constexpr uint64_t kCodeCharMask = (uint64_t{1} << kBitsPerCodeChar) - 1;
        static_assert(RoomLimits::kRoomCodeAlphabet.size() == (size_t{1} << kBitsPerCodeChar), "5 bits pick one character exactly");
        static_assert(RoomLimits::kRoomCodeLength * kBitsPerCodeChar <= 64, "a code comes from one word");

        constexpr eastl::string_view kHexDigits = "0123456789abcdef";
        constexpr size_t kHexDigitsPerWord = 16;

        void AppendHex(eastl::string& out, uint64_t word) {
            for (size_t i = 0; i < kHexDigitsPerWord; ++i) {
                const size_t shift = (kHexDigitsPerWord - 1 - i) * 4;
                out.push_back(kHexDigits[static_cast<size_t>((word >> shift) & 0xF)]);
            }
        }

    }  // namespace

    eastl::string GenerateRoomCode(const RandomFn& random) {
        const uint64_t word = random();
        eastl::string code;
        code.reserve(RoomLimits::kRoomCodeLength);
        for (size_t i = 0; i < RoomLimits::kRoomCodeLength; ++i) {
            code.push_back(RoomLimits::kRoomCodeAlphabet[static_cast<size_t>((word >> (i * kBitsPerCodeChar)) & kCodeCharMask)]);
        }
        return code;
    }

    std::optional<eastl::string> NormalizeRoomCode(eastl::string_view text) {
        if (text.size() != RoomLimits::kRoomCodeLength) {
            return std::nullopt;
        }
        eastl::string code;
        code.reserve(text.size());
        for (const char c : text) {
            const char upper = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
            if (RoomLimits::kRoomCodeAlphabet.find(upper) == eastl::string_view::npos) {
                return std::nullopt;
            }
            code.push_back(upper);
        }
        return code;
    }

    eastl::string GenerateToken(const RandomFn& random) {
        eastl::string token;
        token.reserve(RoomLimits::kTokenLength);
        AppendHex(token, random());
        AppendHex(token, random());
        return token;
    }

    bool IsWellFormedToken(eastl::string_view text) {
        if (text.size() != RoomLimits::kTokenLength) {
            return false;
        }
        for (const char c : text) {
            const bool digit = c >= '0' && c <= '9';
            const bool lowerHex = c >= 'a' && c <= 'f';
            if (!digit && !lowerHex) {
                return false;
            }
        }
        return true;
    }

}  // namespace BECore
