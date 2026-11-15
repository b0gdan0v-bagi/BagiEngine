#pragma once

#include <BECore/Math/Vec2.h>
#include <cstdint>

namespace BECore {

    /// A cell, an offset or a size on an integer grid (two `int32_t`). It never mixes with `Vec2` implicitly: the
    /// conversions are explicit, and the one from `Vec2` truncates toward zero.
    struct IVec2 {
        int32_t x = 0;
        int32_t y = 0;

        constexpr IVec2() = default;
        constexpr IVec2(int32_t x_, int32_t y_) : x(x_), y(y_) {}
        constexpr explicit IVec2(const Vec2& v) : x(static_cast<int32_t>(v.x)), y(static_cast<int32_t>(v.y)) {}

        [[nodiscard]] constexpr explicit operator Vec2() const {
            return {static_cast<float>(x), static_cast<float>(y)};
        }

        [[nodiscard]] constexpr bool operator==(const IVec2&) const = default;

        [[nodiscard]] constexpr IVec2 operator+(const IVec2& other) const {
            return {x + other.x, y + other.y};
        }

        [[nodiscard]] constexpr IVec2 operator-(const IVec2& other) const {
            return {x - other.x, y - other.y};
        }

        [[nodiscard]] constexpr IVec2 operator-() const {
            return {-x, -y};
        }

        [[nodiscard]] constexpr IVec2 operator*(int32_t scale) const {
            return {x * scale, y * scale};
        }

        constexpr IVec2& operator+=(const IVec2& other) {
            x += other.x;
            y += other.y;
            return *this;
        }

        constexpr IVec2& operator-=(const IVec2& other) {
            x -= other.x;
            y -= other.y;
            return *this;
        }

        constexpr IVec2& operator*=(int32_t scale) {
            x *= scale;
            y *= scale;
            return *this;
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

    [[nodiscard]] constexpr IVec2 operator*(int32_t scale, const IVec2& v) {
        return v * scale;
    }

}  // namespace BECore
