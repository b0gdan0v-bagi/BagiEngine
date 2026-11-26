#include "Base64.h"

namespace BECore {

    namespace {

        constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        constexpr char kPadding = '=';
        constexpr size_t kGroupBytes = 3;
        constexpr size_t kGroupChars = 4;
        constexpr uint32_t kSixBits = 0x3F;
        constexpr uint32_t kByte = 0xFF;
        constexpr size_t kMaxPadding = 2;
        constexpr uint8_t kNoValue = 0xFF;

        /// The six bits of each character, kNoValue for a character outside the alphabet
        constexpr auto kValues = [] {
            Array<uint8_t, 256> values{};
            for (uint8_t& value : values) {
                value = kNoValue;
            }
            for (uint8_t value = 0; kAlphabet[value] != '\0'; ++value) {
                values[static_cast<uint8_t>(kAlphabet[value])] = value;
            }
            return values;
        }();

    }  // namespace

    String Base64Encode(Span<const uint8_t> data) {
        String encoded;
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

    bool Base64Decode(StringView text, Vector<uint8_t>& data) {
        data.clear();
        if (text.size() % kGroupChars != 0) {
            return false;
        }
        size_t padding = 0;
        while (padding < kMaxPadding && padding < text.size() && text[text.size() - 1 - padding] == kPadding) {
            ++padding;
        }

        data.resize(text.size() / kGroupChars * kGroupBytes - padding);
        const size_t characters = text.size() - padding;
        uint32_t group = 0;
        size_t next = 0;
        for (size_t i = 0; i < characters; ++i) {
            const uint8_t value = kValues[static_cast<uint8_t>(text[i])];
            if (value == kNoValue) {
                data.clear();
                return false;
            }
            group = (group << 6) | value;
            if (i % kGroupChars == kGroupChars - 1) {
                data[next++] = static_cast<uint8_t>((group >> 16) & kByte);
                data[next++] = static_cast<uint8_t>((group >> 8) & kByte);
                data[next++] = static_cast<uint8_t>(group & kByte);
                group = 0;
            }
        }

        // A padded group holds 12 or 18 bits for 1 or 2 bytes
        if (padding > 0) {
            const uint32_t unused = padding == kMaxPadding ? 4 : 2;
            if ((group & ((1u << unused) - 1)) != 0) {
                data.clear();
                return false;
            }
            group >>= unused;
            if (padding == 1) {
                data[next++] = static_cast<uint8_t>((group >> 8) & kByte);
            }
            data[next] = static_cast<uint8_t>(group & kByte);
        }
        return true;
    }

}  // namespace BECore
