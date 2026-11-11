#pragma once

namespace BECore {

    /// A point or a vector of two floats (positions in the engine are two floats in `TransformComponent`).
    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;

        constexpr Vec2() = default;
        constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

        [[nodiscard]] constexpr bool operator==(const Vec2&) const = default;

        [[nodiscard]] constexpr Vec2 operator+(const Vec2& other) const {
            return {x + other.x, y + other.y};
        }

        [[nodiscard]] constexpr Vec2 operator-(const Vec2& other) const {
            return {x - other.x, y - other.y};
        }

        [[nodiscard]] constexpr Vec2 operator*(float scale) const {
            return {x * scale, y * scale};
        }

        template <typename Serializer>
        void Serialize(Serializer& s) const {
            s.WriteAttribute("x", x);
            s.WriteAttribute("y", y);
        }

        template <typename Deserializer>
        void Deserialize(Deserializer& d) {
            d.ReadAttribute("x", x);
            d.ReadAttribute("y", y);
        }

        template <typename Visitor>
        bool VisitProperties(Visitor& v) {
            bool changed = false;
            changed |= v.Visit("x", x);
            changed |= v.Visit("y", y);
            return changed;
        }
    };

}  // namespace BECore
