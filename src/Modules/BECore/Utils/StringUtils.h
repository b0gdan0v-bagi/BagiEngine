#pragma once

namespace BECore {

    class StringUtils {
    public:
        using SmallStrVector = eastl::fixed_vector<StringView, 4>;

        static constexpr uint64_t hashOffset = 14695981039346656037ULL;
        static constexpr uint64_t hashPrime = 1099511628211ULL;

        static SmallStrVector Split(StringView str, char delimiter);

        static constexpr uint64_t GetHash(StringView sv) noexcept {

            uint64_t hash = hashOffset;
            for (char c : sv) {
                hash ^= static_cast<uint64_t>(c);
                hash *= hashPrime;
            }
            return hash;
        }

        static constexpr uint64_t GetHash(std::string_view sv) noexcept {  // std: the callers that hold a std view
            return GetHash(FromStd(sv));
        }

        static constexpr uint64_t GetEmptyHash() noexcept {
            return hashOffset;
        }
    };

}  // namespace BECore
