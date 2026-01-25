#pragma once

namespace Math {

    struct Color {
        constexpr Color() = default;
        constexpr explicit Color(unsigned char r_, unsigned char g_, unsigned char b_, unsigned char a_) : r(r_), g(g_), b(b_), a(a_) {}

        static std::optional<Color> ParseColorFromString(eastl::string_view data);

        /**
         * @brief Serialize color components as XML attributes
         * @tparam Archive Archive type (XmlArchive, BinaryArchive, etc.)
         */
        template<typename Archive>
        void Serialize(Archive& archive) {
            archive.SerializeAttribute("r", r);
            archive.SerializeAttribute("g", g);
            archive.SerializeAttribute("b", b);
            archive.SerializeAttribute("a", a);
        }

        unsigned char r = 0;
        unsigned char g = 0;
        unsigned char b = 0;
        unsigned char a = 0;
    };
}  // namespace Math

