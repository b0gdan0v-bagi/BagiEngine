#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>

namespace BECore::Fuzz {

    /// Longest refusal a reader of untrusted bytes may give: the caller logs it after the name of what it read
    inline constexpr size_t kMaxRefusalBytes = 200;

    /// Whether @p text has a control character (a line break included): a log line made of it is more than one line
    inline bool HasControlCharacter(StringView text) {
        constexpr uint8_t kLastControlCharacter = 0x1F;
        constexpr uint8_t kDelete = 0x7F;
        for (const char c : text) {
            const auto byte = static_cast<uint8_t>(c);
            if (byte <= kLastControlCharacter || byte == kDelete) {
                return true;
            }
        }
        return false;
    }

    /// A message that may repeat the peer's text in a log or a reply: printable ASCII, not empty and at most @p maxBytes, whatever the peer sent
    inline bool IsTidyMessage(StringView message, size_t maxBytes = kMaxRefusalBytes) {
        if (message.empty() || message.size() > maxBytes) {
            return false;
        }
        for (const char letter : message) {
            if (letter < ' ' || letter > '~') {
                return false;
            }
        }
        return true;
    }

    /// FNV-1a over what a target saw, as one number: two reads that must agree compare their digests
    class FuzzDigest {
    public:
        void AddBytes(const void* data, size_t size) {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i) {
                _value = (_value ^ bytes[i]) * kFnvPrime;
            }
        }

        template <typename T>
        void Add(const T& value) {
            AddBytes(&value, sizeof(T));
        }

        /// The length first, so "ab" + "c" and "a" + "bc" differ
        void AddText(StringView text) {
            Add(text.size());
            AddBytes(text.data(), text.size());
        }

        uint64_t Value() const {
            return _value;
        }

    private:
        static constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ull;
        static constexpr uint64_t kFnvPrime = 0x100000001B3ull;

        uint64_t _value = kFnvOffset;
    };

}  // namespace BECore::Fuzz
