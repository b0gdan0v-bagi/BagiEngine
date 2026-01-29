#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <optional>
#include <EASTL/string_view.h>

namespace Math {

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

    /**
     * @brief Color with reflection support (adds 8 bytes _typeMeta pointer)
     * 
     * Use this when you need:
     * - Automatic serialization via BE_REFLECT_FIELD
     * - Runtime type checking (Is<T>, Cast<T>)
     * - Factory pattern support
     * 
     * Note: Adds 8-byte overhead per instance for _typeMeta pointer.
     */
    struct ColorSerializable : public Color {
        BE_CLASS(ColorSerializable)

        BE_REFLECT_FIELD unsigned char r = 0;
        BE_REFLECT_FIELD unsigned char g = 0;
        BE_REFLECT_FIELD unsigned char b = 0;
        BE_REFLECT_FIELD unsigned char a = 0;

        constexpr ColorSerializable() = default;
        
        // Constructor from values
        constexpr ColorSerializable(unsigned char r_, unsigned char g_, unsigned char b_, unsigned char a_)
            : Color(r_, g_, b_, a_)
            , r(r_), g(g_), b(b_), a(a_)
        {}

        // Constructor from Color
        constexpr ColorSerializable(const Color& color)
            : Color(color)
            , r(color.r), g(color.g), b(color.b), a(color.a)
        {}

        // Assignment from Color
        ColorSerializable& operator=(const Color& color) {
            Color::operator=(color);
            r = color.r;
            g = color.g;
            b = color.b;
            a = color.a;
            return *this;
        }

        // Implicit conversion to Color
        operator Color() const {
            return Color{r, g, b, a};
        }
    };

}  // namespace Math

