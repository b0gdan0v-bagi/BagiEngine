#pragma once

namespace Core {

    class String {
    public:
        using SmallStrVector = eastl::fixed_vector<std::string_view, 4>;

        static SmallStrVector Split(std::string_view str, char delimiter);
    };

    struct StringViewHash {
        using is_transparent = void;

        size_t operator()(const char* p) const {
            return eastl::hash<const char*>{}(p);
        }
        size_t operator()(std::string_view s) const {
            return eastl::hash<eastl::string_view>{}({s.data(), s.size()});
        }
    };

}  // namespace Core
