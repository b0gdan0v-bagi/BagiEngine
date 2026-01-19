#pragma once

#include <EASTL/string_view.h>
#include <string_view>

namespace BECore {

    class String {
    public:
        using SmallStrVector = eastl::fixed_vector<eastl::string_view, 4>;

        static constexpr uint64_t hashOffset = 14695981039346656037ULL;
        static constexpr uint64_t hashPrime = 1099511628211ULL;

        static SmallStrVector Split(eastl::string_view str, char delimiter);

        static constexpr uint64_t GetHash(eastl::string_view sv) noexcept {
            
            uint64_t hash = hashOffset;
            for (char c : sv) {
                hash ^= static_cast<uint64_t>(c);
                hash *= hashPrime;
            }
            return hash;
        }

        static constexpr uint64_t GetHash(std::string_view sv) noexcept {
            return GetHash(eastl::string_view{sv.data(), sv.size()});
        }

        static constexpr uint64_t GetEmptyHash() noexcept {
            return hashOffset;
        }
    };

}  // namespace BECore
