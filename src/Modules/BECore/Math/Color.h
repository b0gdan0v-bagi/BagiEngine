#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <optional>
#include <EASTL/string_view.h>

namespace BECore {

    /**
     * @brief Lightweight POD color type (4 bytes, no vtable, no reflection overhead)
     * 
     * Use this for performance-critical code, arrays, or when reflection is not needed.
     */
    struct Color {
        unsigned char r = 0;
        unsigned char g = 0;
        unsigned char b = 0;
        unsigned char a = 0;

        constexpr Color() = default;
        constexpr Color(unsigned char r_, unsigned char g_, unsigned char b_, unsigned char a_) 
            : r(r_), g(g_), b(b_), a(a_) {}

        static std::optional<Color> ParseColorFromString(eastl::string_view data);

        // Manual serialization for lightweight types
        template<typename Serializer>
        void Serialize(Serializer& s) const {
            s.WriteAttribute("r", r);
            s.WriteAttribute("g", g);
            s.WriteAttribute("b", b);
            s.WriteAttribute("a", a);
        }

        template<typename Deserializer>
        void Deserialize(Deserializer& d) {
            d.ReadAttribute("r", r);
            d.ReadAttribute("g", g);
            d.ReadAttribute("b", b);
            d.ReadAttribute("a", a);
        }
    };
}  // namespace BECore

