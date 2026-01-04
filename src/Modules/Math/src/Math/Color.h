#pragma once

#include <optional>
#include <string_view>

namespace Math {

    struct Color {
        constexpr explicit Color(unsigned char r_, unsigned char g_, unsigned char b_, unsigned char a_) : r(r_), g(g_), b(b_), a(a_) {}

        static std::optional<Color> ParseColorFromString(std::string_view data);

        unsigned char r = 0;
        unsigned char g = 0;
        unsigned char b = 0;
        unsigned char a = 0;
    };
}  // namespace Math

