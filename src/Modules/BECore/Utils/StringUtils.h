#pragma once

#include <BECore/Types/Types.h>
#include <charconv>
#include <cstdint>
#include <type_traits>

namespace BECore {

    class StringUtils {
    public:
        using SmallStrVector = SmallVector<StringView, 4>;

        static constexpr uint64_t hashOffset = 14695981039346656037ULL;
        static constexpr uint64_t hashPrime = 1099511628211ULL;

        /// Views into `str`, which must outlive them; an empty string gives no part, every delimiter ends a part (so "a," gives "a" and "").
        static SmallStrVector Split(StringView str, char delimiter);

        /// FNV-1a, 64 bit.
        static constexpr uint64_t GetHash(StringView sv) noexcept {

            uint64_t hash = hashOffset;
            for (char c : sv) {
                hash ^= static_cast<uint64_t>(c);
                hash *= hashPrime;
            }
            return hash;
        }

        /// The number the whole of @p text spells (`std::from_chars`: no spaces, no leading `+`, decimal); nullopt for anything else
        /// or a value out of the range of @p T. Never allocates.
        template <typename T>
        requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) static Optional<T> ParseNumber(StringView text) noexcept {
            T value{};
            const char* const end = text.data() + text.size();
            const auto [last, error] = std::from_chars(text.data(), end, value);
            if (error != std::errc{} || last != end || text.empty()) {
                return std::nullopt;
            }
            return value;
        }

        static constexpr uint64_t GetEmptyHash() noexcept {
            return hashOffset;
        }
    };

}  // namespace BECore
