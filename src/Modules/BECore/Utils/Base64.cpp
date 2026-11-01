#include "Base64.h"

namespace BECore {

    namespace {

        constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        constexpr char kPadding = '=';
        constexpr size_t kGroupBytes = 3;    // input bytes per group
        constexpr size_t kGroupChars = 4;    // output characters per group
        constexpr uint32_t kSixBits = 0x3F;  // one output character carries 6 bits

    }  // namespace

    eastl::string Base64Encode(eastl::span<const uint8_t> data) {
        eastl::string encoded;
        encoded.reserve((data.size() + kGroupBytes - 1) / kGroupBytes * kGroupChars);

        size_t index = 0;
        for (; index + kGroupBytes <= data.size(); index += kGroupBytes) {
            const uint32_t group = (static_cast<uint32_t>(data[index]) << 16) | (static_cast<uint32_t>(data[index + 1]) << 8) | data[index + 2];
            encoded.push_back(kAlphabet[(group >> 18) & kSixBits]);
            encoded.push_back(kAlphabet[(group >> 12) & kSixBits]);
            encoded.push_back(kAlphabet[(group >> 6) & kSixBits]);
            encoded.push_back(kAlphabet[group & kSixBits]);
        }

        // One or two bytes are left: 2 or 3 characters, then the padding up to a full group
        const size_t rest = data.size() - index;
        if (rest > 0) {
            uint32_t group = static_cast<uint32_t>(data[index]) << 16;
            if (rest == 2) {
                group |= static_cast<uint32_t>(data[index + 1]) << 8;
            }
            encoded.push_back(kAlphabet[(group >> 18) & kSixBits]);
            encoded.push_back(kAlphabet[(group >> 12) & kSixBits]);
            encoded.push_back(rest == 2 ? kAlphabet[(group >> 6) & kSixBits] : kPadding);
            encoded.push_back(kPadding);
        }

        return encoded;
    }

}  // namespace BECore
